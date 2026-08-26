#include "editor.h"

#include <string.h>

/* 간단한 파이썬/C 공용 키워드 강조용 목록 (파이썬 버전의 PY_KEYWORDS 참고) */
static const char *KEYWORDS[] = {
    "if", "else", "elif", "for", "while", "def", "return", "class",
    "import", "from", "as", "try", "except", "finally", "with",
    "int", "char", "float", "double", "void", "struct", "typedef",
    "const", "static", "include", "define", "switch", "case", "break",
    "continue", "true", "false", "True", "False", "None", "NULL",
    NULL
};

static void apply_highlight(CodeEditor *editor) {
    GtkTextBuffer *buf = editor->buffer;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gtk_text_buffer_remove_tag_by_name(buf, "keyword", &start, &end);
    gtk_text_buffer_remove_tag_by_name(buf, "string", &start, &end);
    gtk_text_buffer_remove_tag_by_name(buf, "comment", &start, &end);

    char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    size_t len = strlen(text);

    /* 키워드 강조: 단어 경계 기준으로 간단히 탐색 */
    for (int k = 0; KEYWORDS[k]; k++) {
        const char *kw = KEYWORDS[k];
        size_t kw_len = strlen(kw);
        size_t i = 0;
        while (i < len) {
            const char *found = strstr(text + i, kw);
            if (!found) break;
            size_t pos = (size_t)(found - text);
            gboolean left_ok = (pos == 0) || !(g_ascii_isalnum(text[pos - 1]) || text[pos - 1] == '_');
            gboolean right_ok = (pos + kw_len >= len) ||
                                 !(g_ascii_isalnum(text[pos + kw_len]) || text[pos + kw_len] == '_');
            if (left_ok && right_ok) {
                GtkTextIter s, e;
                gtk_text_buffer_get_iter_at_offset(buf, &s, (gint)pos);
                gtk_text_buffer_get_iter_at_offset(buf, &e, (gint)(pos + kw_len));
                gtk_text_buffer_apply_tag_by_name(buf, "keyword", &s, &e);
            }
            i = pos + kw_len;
        }
    }

    /* 한 줄 주석: // 또는 # 부터 줄 끝까지 */
    for (size_t i = 0; i < len; i++) {
        if ((text[i] == '#') || (text[i] == '/' && i + 1 < len && text[i + 1] == '/')) {
            size_t j = i;
            while (j < len && text[j] != '\n') j++;
            GtkTextIter s, e;
            gtk_text_buffer_get_iter_at_offset(buf, &s, (gint)i);
            gtk_text_buffer_get_iter_at_offset(buf, &e, (gint)j);
            gtk_text_buffer_apply_tag_by_name(buf, "comment", &s, &e);
            i = j;
        }
    }

    /* 문자열: "..." 구간 (아주 단순화 — 이스케이프 무시) */
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '"') {
            size_t j = i + 1;
            while (j < len && text[j] != '"' && text[j] != '\n') j++;
            if (j < len && text[j] == '"') {
                GtkTextIter s, e;
                gtk_text_buffer_get_iter_at_offset(buf, &s, (gint)i);
                gtk_text_buffer_get_iter_at_offset(buf, &e, (gint)(j + 1));
                gtk_text_buffer_apply_tag_by_name(buf, "string", &s, &e);
                i = j;
            }
        }
    }

    g_free(text);
}

static void on_buffer_changed(GtkTextBuffer *buf, gpointer user_data) {
    (void)buf;
    CodeEditor *editor = (CodeEditor *)user_data;
    apply_highlight(editor);
    if (editor->on_change) editor->on_change(editor->on_change_data);
}

CodeEditor *editor_new(void (*on_change)(void *user_data), void *on_change_data) {
    CodeEditor *editor = g_new0(CodeEditor, 1);
    editor->on_change = on_change;
    editor->on_change_data = on_change_data;

    editor->text_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(editor->text_view, GTK_WRAP_NONE);
    gtk_text_view_set_monospace(editor->text_view, TRUE);
    gtk_text_view_set_left_margin(editor->text_view, 8);
    gtk_text_view_set_top_margin(editor->text_view, 6);

    /* 어두운 배경 + 밝은 글자 (파이썬 버전과 비슷한 다크 테마) */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview { background-color: #1e1e1e; color: #d4d4d4; caret-color: #ffffff; }"
        "textview text { background-color: #1e1e1e; color: #d4d4d4; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(editor->text_view)),
                                    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    editor->buffer = gtk_text_view_get_buffer(editor->text_view);
    gtk_text_buffer_create_tag(editor->buffer, "keyword", "foreground", "#569cd6", NULL);
    gtk_text_buffer_create_tag(editor->buffer, "string", "foreground", "#ce9178", NULL);
    gtk_text_buffer_create_tag(editor->buffer, "comment", "foreground", "#6a9955", NULL);

    g_signal_connect(editor->buffer, "changed", G_CALLBACK(on_buffer_changed), editor);

    editor->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(editor->scrolled_window), GTK_WIDGET(editor->text_view));

    return editor;
}

char *editor_get_content(CodeEditor *editor) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(editor->buffer, &start, &end);
    return gtk_text_buffer_get_text(editor->buffer, &start, &end, FALSE);
}

void editor_set_content(CodeEditor *editor, const char *content) {
    /* 프로그램적으로 내용을 바꿀 때는 changed 시그널이 굳이 dirty 처리를
     * 하지 않아도 되지만, 문법 강조는 그대로 걸리도록 signal 은 유지한다. */
    gtk_text_buffer_set_text(editor->buffer, content ? content : "", -1);
}
