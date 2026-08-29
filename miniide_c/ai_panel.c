#include "ai_panel.h"
#include "ui_lang.h"

#include <string.h>

#define MAX_HISTORY_MESSAGES 30

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

static char *build_request_json(AiPanel *panel) {
    GString *out = g_string_new("{\"model\":\"");
    json_escape_append(out, panel->client.model);
    g_string_append(out, "\",\"messages\":[");
    for (guint i = 0; i < panel->messages->len; i++) {
        ChatMessage *m = g_ptr_array_index(panel->messages, i);
        if (i > 0) g_string_append_c(out, ',');
        g_string_append(out, "{\"role\":\"");
        json_escape_append(out, m->role);
        g_string_append(out, "\",\"content\":\"");
        json_escape_append(out, m->content);
        g_string_append(out, "\"}");
    }
    g_string_append(out, "],\"stream\":true}");
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
        char *ok = trf(STR_AI_CONNECTED_FMT, panel->client.model);
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
    ask(panel, prompt);
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
    ask(panel, prompt);
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
    ask(panel, prompt);
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
    ask(panel, copy);
    g_free(copy);
}

static void on_send_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_entry_activate(((AiPanel *)user_data)->entry, user_data);
}

/* ---------------- 서버 상태 확인 (시작 시 1회, 백그라운드) ---------------- */

typedef struct {
    AiPanel *panel;
    gboolean available;
} ServerCheckResult;

static gboolean server_check_idle(gpointer data) {
    ServerCheckResult *r = (ServerCheckResult *)data;
    char *text = r->available ? trf(STR_AI_CONNECTED_FMT, r->panel->client.model)
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

    /* 퀵 액션 버튼 줄 1: 역할 전환 (2번 기능) */
    GtkWidget *quick_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    panel->review_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_REVIEW)));
    panel->fix_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_FIX)));
    panel->plan_btn = GTK_BUTTON(gtk_button_new_with_label(tr(STR_BTN_PLAN)));
    g_signal_connect(panel->review_btn, "clicked", G_CALLBACK(on_review_clicked), panel);
    g_signal_connect(panel->fix_btn, "clicked", G_CALLBACK(on_fix_clicked), panel);
    g_signal_connect(panel->plan_btn, "clicked", G_CALLBACK(on_plan_clicked), panel);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->review_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->fix_btn), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(quick_row1), GTK_WIDGET(panel->plan_btn), FALSE, FALSE, 2);
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
    gtk_label_set_text(panel->status, tr(STR_AI_CONNECTING));
    GThread *check_thread = g_thread_new("ai-server-check", server_check_worker, panel);
    g_thread_unref(check_thread);
}
