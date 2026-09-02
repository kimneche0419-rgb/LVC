#include "ai_panel.h"
#include "ui_lang.h"

#include <string.h>

/* 대화 기록을 서버로 보낼 때 포함할 최근 메시지 수 — 프롬프트가 짧을수록
 * 첫 응답까지 빠르다(토큰 처리 비용 절약). 오래된 기록은 화면에만 남는다. */
#define SEND_LAST_MESSAGES 6
#define MAX_HISTORY_MESSAGES 12

/* AI 역할 — 기능마다 다른 모델이 담당한다 (사용자 요청: 역할별 모델 분리).
 * 각 역할은 후보 목록의 앞쪽부터 서버에 설치된 모델을 찾아 사용한다. */
typedef enum {
    AI_ROLE_LIGHT,   /* 일반 질문 — 가벼운 모델로 빠른 응답 */
    AI_ROLE_CODE,    /* 코드 리뷰 / 버그 수정 — 코딩 특화 모델 */
    AI_ROLE_PLAN     /* 계획 세우기 — 지시 이해가 좋은 일반 모델 */
} AiRole;

/* 후보 순서 = 우선순위. 없는 모델은 건너뛰고 다음 후보로 자동 선택된다. */
static const char *LIGHT_MODELS[] = {"qwen2.5-coder:1.5b", "qwen2.5-coder:3b", NULL};
static const char *CODE_MODELS[]  = {"qwen2.5-coder:3b", "qwen2.5-coder:1.5b", NULL};
static const char *PLAN_MODELS[]  = {"qwen2.5:3b", "qwen2.5-coder:3b", "qwen2.5-coder:1.5b", NULL};

/* 역할별 최대 출력 토큰 — 답변 길이를 제한해 완료 시간을 줄인다 */
static const struct { AiRole role; int num_predict; } ROLE_OPTIONS[] = {
    {AI_ROLE_LIGHT, 600},
    {AI_ROLE_CODE,  1400},
    {AI_ROLE_PLAN,  900},
};

typedef struct {
    char *role;
    char *content;
} ChatMessage;

/* AI 응답에서 코드블록(```...```)을 뽑아내는 최소 파서.
 * 여는 ``` 다음 줄바꿈부터 닫는 ``` 직전까지를 코드로 본다. */
static char *extract_code_block(const char *text) {
    const char *open = strstr(text, "```");
    if (!open) return g_strdup(text);
    const char *after_fence = open + 3;
    const char *nl = strchr(after_fence, '\n');
    if (!nl) return g_strdup(text);
    const char *code_start = nl + 1;
    const char *close = strstr(code_start, "```");
    if (!close) return g_strdup(text);
    return g_strndup(code_start, (gsize)(close - code_start));
}

static void json_escape_append(GString *out, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if (*p < 0x20) {
                    /* 그 외 제어문자는 건너뜀 */
                } else {
                    g_string_append_c(out, (char)*p);
                }
        }
    }
}

static const char *role_model(AiPanel *panel, AiRole role) {
    switch (role) {
        case AI_ROLE_CODE:  return panel->model_code;
        case AI_ROLE_PLAN:  return panel->model_plan;
        default:            return panel->model_light;
    }
}

/* 역할별 시스템 프롬프트 — 코딩/기획 전문가 스킬을 요청마다 주입한다 */
static const char *role_system_prompt(AiRole role) {
    switch (role) {
        case AI_ROLE_CODE:  return tr(STR_SYS_CODE);
        case AI_ROLE_PLAN:  return tr(STR_SYS_PLAN);
        default:            return tr(STR_SYS_LIGHT);
    }
}

/* ---------------- 역할별 모델 설정 저장 (<config>/miniide/models.txt) ---------------- */

static void save_role_models(AiPanel *panel) {
    char *dir = g_build_filename(g_get_user_config_dir(), "miniide", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "models.txt", NULL);
    char *body = g_strdup_printf("light=%s\ncode=%s\nplan=%s\n",
                                 panel->model_light, panel->model_code, panel->model_plan);
    g_file_set_contents(path, body, -1, NULL);
    g_free(body);
    g_free(path);
    g_free(dir);
}

/* 저장된 역할별 모델 하나 읽기 (없으면 0) */
static gboolean load_saved_role_model(const char *key, char *out, size_t out_len) {
    char *path = g_build_filename(g_get_user_config_dir(), "miniide", "models.txt", NULL);
    gchar *content = NULL;
    gboolean found = FALSE;
    if (g_file_get_contents(path, &content, NULL, NULL) && content) {
        gchar **lines = g_strsplit(content, "\n", -1);
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "%s=", key);
        for (guint i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], prefix)) {
                g_strlcpy(out, lines[i] + strlen(prefix), out_len);
                found = TRUE;
                break;
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(path);
    return found;
}

static int role_num_predict(AiRole role) {
    for (size_t i = 0; i < sizeof(ROLE_OPTIONS) / sizeof(ROLE_OPTIONS[0]); i++) {
        if (ROLE_OPTIONS[i].role == role) return ROLE_OPTIONS[i].num_predict;
    }
    return 800;
}

/* 서버에 설치된 모델 목록에서 역할별 모델을 고른다 (첫 응답 속도와 품질 균형).
 * 서버에 연결할 수 없으면 각 역할의 첫 후보로 초기화한다. */
static void resolve_role_models(AiPanel *panel) {
    char *tags = ai_client_tags_body(&panel->client);

    /* 역할별 선택 결과 — 기본은 각 역할의 첫 후보 */
    char light[AI_MAX_MODEL_LEN], code[AI_MAX_MODEL_LEN], plan[AI_MAX_MODEL_LEN];
    char saved[AI_MAX_MODEL_LEN];
    g_strlcpy(light, LIGHT_MODELS[0], sizeof(light));
    g_strlcpy(code, CODE_MODELS[0], sizeof(code));
    g_strlcpy(plan, PLAN_MODELS[0], sizeof(plan));

    if (tags) {
        for (int i = 0; LIGHT_MODELS[i]; i++)
            if (ai_client_body_has_model(tags, LIGHT_MODELS[i])) { g_strlcpy(light, LIGHT_MODELS[i], sizeof(light)); break; }
        for (int i = 0; CODE_MODELS[i]; i++)
            if (ai_client_body_has_model(tags, CODE_MODELS[i])) { g_strlcpy(code, CODE_MODELS[i], sizeof(code)); break; }
        for (int i = 0; PLAN_MODELS[i]; i++)
            if (ai_client_body_has_model(tags, PLAN_MODELS[i])) { g_strlcpy(plan, PLAN_MODELS[i], sizeof(plan)); break; }

        /* 사용자가 설정 대화상자에서 고른 모델이 저장되어 있고 아직 설치되어 있으면 그것을 우선한다 */
        if (load_saved_role_model("light", saved, sizeof(saved)) && ai_client_body_has_model(tags, saved)) {
            g_strlcpy(light, saved, sizeof(light));
        }
        if (load_saved_role_model("code", saved, sizeof(saved)) && ai_client_body_has_model(tags, saved)) {
            g_strlcpy(code, saved, sizeof(code));
        }
        if (load_saved_role_model("plan", saved, sizeof(saved)) && ai_client_body_has_model(tags, saved)) {
            g_strlcpy(plan, saved, sizeof(plan));
        }
        g_free(tags);
    }

    g_strlcpy(panel->model_light, light, sizeof(panel->model_light));
    g_strlcpy(panel->model_code, code, sizeof(panel->model_code));
    g_strlcpy(panel->model_plan, plan, sizeof(panel->model_plan));
}

static char *build_request_json(AiPanel *panel, AiRole role) {
    GString *out = g_string_new("{\"model\":\"");
    json_escape_append(out, role_model(panel, role));
    g_string_append(out, "\",\"messages\":[");

    /* 역할별 시스템 프롬프트를 항상 맨 앞에 주입 (전문가 스킬) */
    g_string_append(out, "{\"role\":\"system\",\"content\":\"");
    json_escape_append(out, role_system_prompt(role));
    g_string_append(out, "\"}");

    /* 최근 SEND_LAST_MESSAGES 개만 보낸다 — 프롬프트가 짧아질수록 응답이 빨라진다 */
    guint start = panel->messages->len > SEND_LAST_MESSAGES
                  ? panel->messages->len - SEND_LAST_MESSAGES : 0;
    for (guint i = start; i < panel->messages->len; i++) {
        ChatMessage *m = g_ptr_array_index(panel->messages, i);
        g_string_append_c(out, ',');
        g_string_append(out, "{\"role\":\"");
        json_escape_append(out, m->role);
        g_string_append(out, "\",\"content\":\"");
        json_escape_append(out, m->content);
        g_string_append(out, "\"}");
    }
    /* num_predict: 역할별 최대 출력 토큰 — 답변이 알아서 간결해져 완료가 빨라진다 */
    g_string_append_printf(out, "],\"stream\":true,\"options\":{\"num_predict\":%d}}",
                           role_num_predict(role));
    return g_string_free(out, FALSE);
}

static void push_message(AiPanel *panel, const char *role, const char *content) {
    ChatMessage *m = g_new0(ChatMessage, 1);
    m->role = g_strdup(role);
    m->content = g_strdup(content);
    g_ptr_array_add(panel->messages, m);
    while (panel->messages->len > MAX_HISTORY_MESSAGES) {
        g_ptr_array_remove_index(panel->messages, 0);
    }
}

static void append_display(AiPanel *panel, const char *tag, const char *text) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(panel->display);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    if (tag) {
        gtk_text_buffer_insert_with_tags_by_name(buf, &end, text, -1, tag, NULL);
    } else {
        gtk_text_buffer_insert(buf, &end, text, -1);
    }
    gtk_text_buffer_get_end_iter(buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_buffer_place_cursor(buf, &end);
    gtk_text_view_scroll_mark_onscreen(panel->display, mark);
}

/* ---------------- 백그라운드 요청 처리 ---------------- */

typedef struct {
    AiPanel *panel;
    char *messages_json;
    GString *full_response;
    char error[256];
    gboolean failed;
} AiJob;

typedef struct {
    AiPanel *panel;
    char *chunk;
} ChunkMsg;

static gboolean insert_chunk_idle(gpointer data) {
    ChunkMsg *cm = (ChunkMsg *)data;
    append_display(cm->panel, "bot_text", cm->chunk);
    g_free(cm->chunk);
    g_free(cm);
    return G_SOURCE_REMOVE;
}

static void on_chunk_from_worker(const char *chunk, void *user_data) {
    AiJob *job = (AiJob *)user_data;
    g_string_append(job->full_response, chunk);
    ChunkMsg *cm = g_new0(ChunkMsg, 1);
    cm->panel = job->panel;
    cm->chunk = g_strdup(chunk);
    g_idle_add(insert_chunk_idle, cm);
}

static gboolean finalize_job_idle(gpointer data) {
    AiJob *job = (AiJob *)data;
    AiPanel *panel = job->panel;

    if (job->failed) {
        char *msg = trf(STR_AI_ERROR_FMT, job->error);
        append_display(panel, "error_text", msg);
        g_free(msg);
    } else {
        g_string_assign(panel->last_bot_response, job->full_response->str);
        push_message(panel, "assistant", job->full_response->str);
        append_display(panel, NULL, "\n\n");
    }

    panel->is_requesting = FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(panel->send_btn), TRUE);
    if (job->failed) {
        gtk_label_set_text(panel->status, tr(STR_AI_CONN_FAIL_SHORT));
    } else {
        char *ok = trf(STR_AI_MODELS_FMT, panel->model_light, panel->model_code, panel->model_plan);
        gtk_label_set_text(panel->status, ok);
        g_free(ok);
    }

    g_string_free(job->full_response, TRUE);
    g_free(job->messages_json);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer request_worker(gpointer data) {
    AiJob *job = (AiJob *)data;
    char error[256] = {0};
    int rc = ai_client_chat_stream(&job->panel->client, job->messages_json,
                                    on_chunk_from_worker, job, error, sizeof(error));
    if (rc != 0) {
        job->failed = TRUE;
        g_strlcpy(job->error, error, sizeof(job->error));
    }
    g_idle_add(finalize_job_idle, job);
    return NULL;
}

static void ask(AiPanel *panel, const char *query, AiRole role) {
    if (panel->is_requesting) return;
    if (!query || query[0] == '\0') return;

    panel->is_requesting = TRUE;
    gtk_widget_set_sensitive(GTK_WIDGET(panel->send_btn), FALSE);
    gtk_label_set_text(panel->status, tr(STR_AI_BUSY));

    append_display(panel, "user_tag", tr(STR_AI_USER_TAG));
    char with_nl[8200];
    snprintf(with_nl, sizeof(with_nl), "%s\n\n", query);
    append_display(panel, "user_text", with_nl);
    append_display(panel, "bot_tag", tr(STR_AI_BOT_TAG));

    push_message(panel, "user", query);

    AiJob *job = g_new0(AiJob, 1);
    job->panel = panel;
    job->messages_json = build_request_json(panel, role);
    job->full_response = g_string_new("");

    GThread *thread = g_thread_new("ai-request", request_worker, job);
    g_thread_unref(thread);
}

/* ---------------- 퀵 액션 (2번 기능: 역할 전환) ---------------- */

static void on_review_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AiPanel *panel = (AiPanel *)user_data;
    char *code = panel->get_code ? panel->get_code(panel->get_code_data) : NULL;
    if (!code || code[0] == '\0') {
        append_display(panel, "system_text", tr(STR_AI_NO_CODE));
        g_free(code);
        return;
    }
    char *prompt = trf(STR_PROMPT_REVIEW, code);
    ask(panel, prompt, AI_ROLE_CODE);
    g_free(prompt);
    g_free(code);
}

static void on_fix_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AiPanel *panel = (AiPanel *)user_data;
    char *code = panel->get_code ? panel->get_code(panel->get_code_data) : NULL;
    if (!code || code[0] == '\0') {
        append_display(panel, "system_text", tr(STR_AI_NO_CODE));
        g_free(code);
        return;
    }
    char *prompt = trf(STR_PROMPT_FIX, code);
    ask(panel, prompt, AI_ROLE_CODE);
    g_free(prompt);
    g_free(code);
}

/* 계획 세우기 — 새 기능(2번). 입력창에 적은 요청을 바탕으로 구현 계획을 세워달라고 요청.
 * 입력창이 비어 있으면 현재 코드를 바탕으로 개선 계획을 세워달라고 요청. */
static void on_plan_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AiPanel *panel = (AiPanel *)user_data;
    const char *typed = gtk_entry_get_text(panel->entry);

    char *prompt;
    if (typed && typed[0] != '\0') {
        prompt = trf(STR_PROMPT_PLAN_TYPED, typed);
        gtk_entry_set_text(panel->entry, "");
    } else {
        char *code = panel->get_code ? panel->get_code(panel->get_code_data) : NULL;
        if (!code || code[0] == '\0') {
            append_display(panel, "system_text", tr(STR_AI_PLAN_HINT));
            g_free(code);
            return;
        }
        prompt = trf(STR_PROMPT_PLAN_CODE, code);
        g_free(code);
    }
    ask(panel, prompt, AI_ROLE_PLAN);
    g_free(prompt);
}

static void on_apply_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AiPanel *panel = (AiPanel *)user_data;
    if (panel->last_bot_response->len == 0) {
        append_display(panel, "system_text", tr(STR_AI_NO_ANSWER));
        return;
    }
    if (!panel->apply_code) return;

    GtkWidget *dialog = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "%s", tr(STR_AI_APPLY_CONFIRM));
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) return;

    char *code = extract_code_block(panel->last_bot_response->str);
    panel->apply_code(code, panel->apply_code_data);
    g_free(code);
    append_display(panel, "system_text", tr(STR_AI_APPLIED_NOTICE));
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    AiPanel *panel = (AiPanel *)user_data;
    const char *text = gtk_entry_get_text(entry);
    if (!text || text[0] == '\0') return;
    char *copy = g_strdup(text);
    gtk_entry_set_text(entry, "");
    ask(panel, copy, AI_ROLE_LIGHT);  /* 자유 질문은 가벼운 모델로 빠르게 */
    g_free(copy);
}

static void on_send_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_entry_activate(((AiPanel *)user_data)->entry, user_data);
}

/* ---------------- 역할별 모델 설정 대화상자 ---------------- */

typedef struct {
    AiPanel *panel;
    char *tags;   /* /api/tags 본문. NULL 이면 서버에 연결 못 함 */
} ModelsDlgJob;

/* 백그라운드에서 모델 목록을 받은 뒤 메인 스레드에서 대화상자를 연다 */
static gboolean models_dlg_idle(gpointer data) {
    ModelsDlgJob *j = (ModelsDlgJob *)data;
    AiPanel *panel = j->panel;
    GPtrArray *names = ai_client_parse_model_names(j->tags);

    if (!j->tags || names->len == 0) {
        append_display(panel, "system_text", tr(STR_AI_CONN_FAIL_SHORT));
        append_display(panel, NULL, "\n");
    } else {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            tr(STR_MODEL_DIALOG_TITLE),
            GTK_WINDOW(gtk_widget_get_toplevel(panel->box)),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            tr(STR_BTN_CANCEL), GTK_RESPONSE_CANCEL,
            tr(STR_BTN_SAVE), GTK_RESPONSE_OK, NULL);

        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

        const char *labels[3] = { tr(STR_MODEL_ROLE_LIGHT), tr(STR_MODEL_ROLE_CODE), tr(STR_MODEL_ROLE_PLAN) };
        const char *currents[3] = { panel->model_light, panel->model_code, panel->model_plan };
        GtkWidget *combos[3];
        for (int r = 0; r < 3; r++) {
            GtkWidget *lbl = gtk_label_new(labels[r]);
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(grid), lbl, 0, r, 1, 1);

            combos[r] = gtk_combo_box_text_new();
            gint active_idx = 0;
            for (guint i = 0; i < names->len; i++) {
                const char *nm = g_ptr_array_index(names, i);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combos[r]), nm);
                if (g_strcmp0(nm, currents[r]) == 0) active_idx = (gint)i;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(combos[r]), active_idx);
            gtk_grid_attach(GTK_GRID(grid), combos[r], 1, r, 1, 1);
        }
        gtk_widget_show_all(dialog);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combos[0]));
            if (sel) { g_strlcpy(panel->model_light, sel, sizeof(panel->model_light)); g_free(sel); }
            sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combos[1]));
            if (sel) { g_strlcpy(panel->model_code, sel, sizeof(panel->model_code)); g_free(sel); }
            sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combos[2]));
            if (sel) { g_strlcpy(panel->model_plan, sel, sizeof(panel->model_plan)); g_free(sel); }

            save_role_models(panel);
            char *ok = trf(STR_AI_MODELS_FMT, panel->model_light, panel->model_code, panel->model_plan);
            gtk_label_set_text(panel->status, ok);
            g_free(ok);
        }
        gtk_widget_destroy(dialog);
    }

    g_ptr_array_free(names, TRUE);
    g_free(j->tags);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer models_dlg_worker(gpointer data) {
    ModelsDlgJob *j = (ModelsDlgJob *)data;
    j->tags = ai_client_tags_body(&j->panel->client);
    g_idle_add(models_dlg_idle, j);
    return NULL;
}

static void on_model_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ModelsDlgJob *j = g_new0(ModelsDlgJob, 1);
    j->panel = (AiPanel *)user_data;
    GThread *th = g_thread_new("ai-models-dlg", models_dlg_worker, j);
    g_thread_unref(th);
}

/* ---------------- 서버 상태 확인 (시작 시 1회, 백그라운드) ---------------- */

typedef struct {
    AiPanel *panel;
    gboolean available;
} ServerCheckResult;

static gboolean server_check_idle(gpointer data) {
    ServerCheckResult *r = (ServerCheckResult *)data;
    char *text = r->available
        ? trf(STR_AI_MODELS_FMT, r->panel->model_light, r->panel->model_code, r->panel->model_plan)
        : (char *)tr(STR_AI_CONN_FAIL);
    gtk_label_set_text(r->panel->status, text);
    if (r->available) g_free(text);
    g_free(r);
    return G_SOURCE_REMOVE;
}

static gpointer server_check_worker(gpointer data) {
    AiPanel *panel = (AiPanel *)data;
    ServerCheckResult *r = g_new0(ServerCheckResult, 1);
    r->panel = panel;
    r->available = ai_client_is_available(&panel->client);
    /* 서버가 살아 있으면 설치된 모델 목록을 보고 역할별 모델을 확정한다 */
    if (r->available) resolve_role_models(panel);
    g_idle_add(server_check_idle, r);
    return NULL;
}

/* ---------------- 생성 ---------------- */

AiPanel *ai_panel_new(AiPanelGetCodeCb get_code, void *get_code_data,
                       AiPanelApplyCodeCb apply_code, void *apply_code_data) {
    AiPanel *panel = g_new0(AiPanel, 1);
    panel->get_code = get_code;
    panel->get_code_data = get_code_data;
    panel->apply_code = apply_code;
    panel->apply_code_data = apply_code_data;
    panel->messages = g_ptr_array_new();
    panel->last_bot_response = g_string_new("");
    ai_client_init(&panel->client, NULL, NULL);
    /* 기본값으로 먼저 채워두고, 서버 확인이 끝나면 실제 설치된 모델로 교체된다 */
    resolve_role_models(panel);

    panel->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    panel->header = gtk_label_new(tr(STR_AI_HEADER));
    gtk_widget_set_halign(panel->header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(panel->box), panel->header, FALSE, FALSE, 4);

    panel->display = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(panel->display, FALSE);
    gtk_text_view_set_wrap_mode(panel->display, GTK_WRAP_WORD);
    gtk_text_view_set_monospace(panel->display, TRUE);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview { background-color: #1e1e1e; color: #d4d4d4; }"
        "textview text { background-color: #1e1e1e; color: #d4d4d4; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(panel->display)),
                                    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(panel->display);
    gtk_text_buffer_create_tag(buf, "user_tag", "foreground", "#569cd6", NULL);
    gtk_text_buffer_create_tag(buf, "user_text", "foreground", "#ce9178", NULL);
    gtk_text_buffer_create_tag(buf, "bot_tag", "foreground", "#4ec9b0", NULL);
    gtk_text_buffer_create_tag(buf, "bot_text", "foreground", "#d4d4d4", NULL);
    gtk_text_buffer_create_tag(buf, "error_text", "foreground", "#f44747", NULL);
    gtk_text_buffer_create_tag(buf, "system_text", "foreground", "#dcdcaa", NULL);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(panel->display));
    gtk_box_pack_start(GTK_BOX(panel->box), scroll, TRUE, TRUE, 0);

    /* 퀵 액션 버튼 줄 1: 역할 전환 (2번 기능) + 모델 설정 */
    GtkWidget *quick_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    panel->review_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_REVIEW)));
    panel->fix_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_FIX)));
    panel->plan_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_PLAN)));
    panel->model_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_MODEL_BTN)));
    g_signal_connect(panel->review_btn, "clicked", G_CALLBACK(on_review_clicked), panel);
    g_signal_connect(panel->fix_btn, "clicked", G_CALLBACK(on_fix_clicked), panel);
    g_signal_connect(panel->plan_btn, "clicked", G_CALLBACK(on_plan_clicked), panel);
    g_signal_connect(panel->model_btn, "clicked", G_CALLBACK(on_model_btn_clicked), panel);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->review_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->fix_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->plan_btn), FALSE, FALSE, 2);
    gtk_box_pack_end(GTK_BOX(quick_row1), GTK_WIDGET(panel->model_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(panel->box), quick_row1, FALSE, FALSE, 4);

    /* 퀵 액션 버튼 줄 2: 코드 적용 (1번 기능) */
    GtkWidget *quick_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    panel->apply_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_APPLY)));
    g_signal_connect(panel->apply_btn, "clicked", G_CALLBACK(on_apply_clicked), panel);
    gtk_box_pack_end(GTK_BOX(quick_row2), GTK_WIDGET(panel->apply_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(panel->box), quick_row2, FALSE, FALSE, 4);

    /* 입력창 + 전송 버튼 */
    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    panel->entry = GTK_ENTRY(gtk_entry_new());
    g_signal_connect(panel->entry, "activate", G_CALLBACK(on_entry_activate), panel);
    gtk_box_pack_start(GTK_BOX(input_row), GTK_WIDGET(panel->entry), TRUE, TRUE, 4);

    panel->send_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_RUN)));
    g_signal_connect(panel->send_btn, "clicked", G_CALLBACK(on_send_clicked), panel);
    gtk_box_pack_start(GTK_BOX(input_row), GTK_WIDGET(panel->send_btn), FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(panel->box), input_row, FALSE, FALSE, 6);

    panel->status = GTK_LABEL(gtk_label_new(tr(STR_AI_CONNECTING)));
    gtk_widget_set_halign(GTK_WIDGET(panel->status), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(panel->box), GTK_WIDGET(panel->status), FALSE, FALSE, 4);

    GThread *check_thread = g_thread_new("ai-server-check", server_check_worker, panel);
    g_thread_unref(check_thread);

    return panel;
}

/* 언어 전환 시 — 정적 문구를 다시 쓰고 서버 상태를 새 언어로 다시 확인한다 */
void ai_panel_refresh_language(AiPanel *panel) {
    gtk_label_set_text(GTK_LABEL(panel->header), tr(STR_AI_HEADER));
    gtk_button_set_label(panel->review_btn, tr(STR_BTN_REVIEW));
    gtk_button_set_label(panel->fix_btn, tr(STR_BTN_FIX));
    gtk_button_set_label(panel->plan_btn, tr(STR_BTN_PLAN));
    gtk_button_set_label(panel->apply_btn, tr(STR_BTN_APPLY));
    gtk_button_set_label(panel->send_btn, tr(STR_BTN_RUN));
    gtk_button_set_label(panel->model_btn, tr(STR_MODEL_BTN));
    gtk_label_set_text(panel->status, tr(STR_AI_CONNECTING));
    GThread *check_thread = g_thread_new("ai-server-check", server_check_worker, panel);
    g_thread_unref(check_thread);
}
