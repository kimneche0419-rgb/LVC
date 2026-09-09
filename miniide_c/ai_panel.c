#include "ai_panel.h"
#include "ui_lang.h"
#include "ai_skills.h"

#include <string.h>

/* 대화 기록을 서버로 보낼 때 포함할 최근 메시지 수 — 프롬프트가 짧을수록
 * 첫 응답까지 빠르다(토큰 처리 비용 절약). 오래된 기록은 화면에만 남는다. */
#define SEND_LAST_MESSAGES 6
#define MAX_HISTORY_MESSAGES 12

/* 단일 모델 정책 — 모든 요청(질문/리뷰/수정/계획)이 DeepSeek 하나로 동작한다.
 * 후보는 서버에 설치되어 있는지 확인해 사용한다. */
static const char *AI_MODELS[] = {"deepseek-coder-v2:16b-lite-instruct", NULL};

/* 최대 출력 토큰 — 답변 길이를 제한해 완료 시간을 줄인다 */
#define AI_NUM_PREDICT 1400

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

/* ---------------- 모델 설정 저장 (<config>/miniide/models.txt) ---------------- */

static void save_model(AiPanel *panel) {
    char *dir = g_build_filename(g_get_user_config_dir(), "miniide", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "models.txt", NULL);
    char *body = g_strdup_printf("model=%s\n", panel->model);
    g_file_set_contents(path, body, -1, NULL);
    g_free(body);
    g_free(path);
    g_free(dir);
}

/* 저장된 모델 설정 읽기 (없으면 0) */
static gboolean load_saved_model(char *out, size_t out_len) {
    const char *key = "model";
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

/* 서버에 설치된 모델 목록에서 사용 모델을 확정한다.
 * 서버에 연결할 수 없으면 첫 후보(DeepSeek)로 초기화한다. */
static void resolve_model(AiPanel *panel) {
    char *tags = ai_client_tags_body(&panel->client);

    char model[AI_MAX_MODEL_LEN], saved[AI_MAX_MODEL_LEN];
    g_strlcpy(model, AI_MODELS[0], sizeof(model));

    if (tags) {
        for (int i = 0; AI_MODELS[i]; i++)
            if (ai_client_body_has_model(tags, AI_MODELS[i])) { g_strlcpy(model, AI_MODELS[i], sizeof(model)); break; }

        /* 사용자가 설정 대화상자에서 고른 모델이 저장되어 있고 아직 설치되어 있으면 그것을 우선한다 */
        if (load_saved_model(saved, sizeof(saved)) && ai_client_body_has_model(tags, saved)) {
            g_strlcpy(model, saved, sizeof(model));
        }
        g_free(tags);
    }

    g_strlcpy(panel->model, model, sizeof(panel->model));
}

static char *build_request_json(AiPanel *panel) {
    GString *out = g_string_new("{\"model\":\"");
    json_escape_append(out, panel->model);
    g_string_append(out, "\",\"messages\":[");

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
    /* num_predict: 최대 출력 토큰 — 답변이 알아서 간결해져 완료가 빨라진다 */
    g_string_append_printf(out, "],\"stream\":true,\"options\":{\"num_predict\":%d}}",
                           AI_NUM_PREDICT);
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
        gtk_label_set_text(panel->status, "");
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

static void ask(AiPanel *panel, const char *query) {
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
    job->messages_json = build_request_json(panel);
    job->full_response = g_string_new("");

    GThread *thread = g_thread_new("ai-request", request_worker, job);
    g_thread_unref(thread);
}

/* ---------------- 스킬 실행 ---------------- */

/* 메뉴 항목 → 스킬 연결용. 스킬 포인터는 정적 표를 가리키므로 안정적이다. */
typedef struct {
    AiPanel *panel;
    const AiSkill *skill;
} SkillRef;

/* 스킬 하나 실행 — CODE형은 에디터 코드를, TEXT형은 입력창 요청을 프롬프트에 넣는다 */
static void on_skill_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    SkillRef *ref = (SkillRef *)user_data;
    AiPanel *panel = ref->panel;
    const AiSkill *skill = ref->skill;

    char *prompt;
    if (skill->input == AI_SKILL_INPUT_CODE) {
        char *code = panel->get_code ? panel->get_code(panel->get_code_data) : NULL;
        if (!code || code[0] == '\0') {
            append_display(panel, "system_text", tr(STR_AI_NO_CODE));
            g_free(code);
            return;
        }
        prompt = trf(skill->prompt, code);
        g_free(code);
    } else {
        const char *typed = gtk_entry_get_text(panel->entry);
        if (!typed || typed[0] == '\0') {
            append_display(panel, "system_text", tr(STR_SKILL_NEED_TEXT));
            return;
        }
        char *copy = g_strdup(typed);
        gtk_entry_set_text(panel->entry, "");
        prompt = trf(skill->prompt, copy);
        g_free(copy);
    }

    ask(panel, prompt);
    g_free(prompt);
}

/* 스킬 메뉴 조립 — 언어 전환 시 새 언어로 다시 만든다. */
static void build_skill_menu(AiPanel *panel) {
    GtkWidget *old = GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(panel->skill_btn)));
    if (old) gtk_widget_destroy(old);

    GtkWidget *menu = gtk_menu_new();
    for (int c = 0; c < ai_skill_cat_count(); c++) {
        GtkWidget *sub = gtk_menu_new();
        for (int i = 0; i < ai_skill_count((AiSkillCat)c); i++) {
            const AiSkill *skill = ai_skill_at((AiSkillCat)c, i);
            GtkWidget *item = gtk_menu_item_new_with_label(tr(skill->name));
            SkillRef *ref = g_new(SkillRef, 1);
            ref->panel = panel;
            ref->skill = skill;
            g_signal_connect(item, "activate", G_CALLBACK(on_skill_activate), ref);
            /* 항목이 사라질 때 ref 도 함께 해제 — 메뉴 재조립 시 누수 방지 */
            g_object_set_data_full(G_OBJECT(item), "skill-ref", ref, g_free);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), item);
        }
        GtkWidget *cat_item = gtk_menu_item_new_with_label(ai_skill_cat_name((AiSkillCat)c));
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cat_item), sub);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), cat_item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(panel->skill_btn), menu);
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
    ask(panel, copy);
    g_free(copy);
}

static void on_send_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_entry_activate(((AiPanel *)user_data)->entry, user_data);
}

/* ---------------- 모델 설정 대화상자 ---------------- */

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
        GtkWidget *combo = gtk_combo_box_text_new();
        gtk_container_set_border_width(GTK_CONTAINER(combo), 10);
        gtk_box_pack_start(GTK_BOX(content), combo, FALSE, FALSE, 0);

        gint active_idx = 0;
        for (guint i = 0; i < names->len; i++) {
            const char *nm = g_ptr_array_index(names, i);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), nm);
            if (g_strcmp0(nm, panel->model) == 0) active_idx = (gint)i;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active_idx);
        gtk_widget_show_all(dialog);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
            if (sel) { g_strlcpy(panel->model, sel, sizeof(panel->model)); g_free(sel); }

            save_model(panel);
            gtk_label_set_text(panel->status, "");
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
    /* 성공 문구는 표시하지 않는다 — 실패(⚠) 안내만 보여준다 */
    gtk_label_set_text(r->panel->status, r->available ? "" : tr(STR_AI_CONN_FAIL));
    g_free(r);
    return G_SOURCE_REMOVE;
}

static gpointer server_check_worker(gpointer data) {
    AiPanel *panel = (AiPanel *)data;
    ServerCheckResult *r = g_new0(ServerCheckResult, 1);
    r->panel = panel;
    r->available = ai_client_is_available(&panel->client);
    /* 서버가 살아 있으면 설치된 모델 목록을 보고 사용 모델을 확정한다 */
    if (r->available) resolve_model(panel);
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
    resolve_model(panel);

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

    /* 퀵 액션 버튼 줄 1: 스킬 메뉴 + 모델 설정 */
    GtkWidget *quick_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    panel->skill_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(panel->skill_btn), tr(STR_SKILL_BTN));
    build_skill_menu(panel);
    panel->model_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_MODEL_BTN)));
    g_signal_connect(panel->model_btn, "clicked", G_CALLBACK(on_model_btn_clicked), panel);
    gtk_box_pack_start(GTK_BOX(quick_row1), panel->skill_btn, FALSE, FALSE, 2);
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
    gtk_button_set_label(GTK_BUTTON(panel->skill_btn), tr(STR_SKILL_BTN));
    build_skill_menu(panel);
    gtk_button_set_label(panel->apply_btn, tr(STR_BTN_APPLY));
    gtk_button_set_label(panel->send_btn, tr(STR_BTN_RUN));
    gtk_button_set_label(panel->model_btn, tr(STR_MODEL_BTN));
    gtk_label_set_text(panel->status, tr(STR_AI_CONNECTING));
    GThread *check_thread = g_thread_new("ai-server-check", server_check_worker, panel);
    g_thread_unref(check_thread);
}
