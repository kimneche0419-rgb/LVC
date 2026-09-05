/* Mini IDE (C 버전) 진입점 — 헤더바, 3분할 레이아웃, 여러 파일 탭, 코드 실행.
 * 파이썬 버전(miniide/app.py)의 MiniIDEApp 구조를 C+GTK로 이식.
 * 3번 기능: 여러 파일을 탭으로 동시에 열기.
 * Linux/Windows 양쪽에서 빌드된다 (프로세스 실행은 g_spawn 으로 공통화).
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "editor.h"
#include "explorer.h"
#include "terminal.h"
#include "ai_panel.h"
#include "ui_lang.h"
#include "theme.h"
#include "recent.h"

typedef struct AppState AppState;

/* 탭 하나 = 열려 있는 파일 하나. 3번 기능(여러 파일 탭)의 핵심 단위. */
typedef struct {
    AppState *app;
    CodeEditor *editor;
    char *file_path;   /* NULL 이면 "제목 없음" (아직 저장 안 한 새 파일) */
    gboolean dirty;
    GtkLabel *tab_text_label;  /* 탭에 보이는 파일명 + 변경 표시(●) */
} EditorTab;

struct AppState {
    GtkWindow *window;
    GtkTextView *console;
    GtkNotebook *bottom_tabs;
    GtkNotebook *editor_tabs;   /* 3번 기능: 파일 탭 묶음 */
    GtkHeaderBar *header_bar;   /* 상단을 한 줄로 통합 — 메뉴+버튼+제목 */
    GtkMenuButton *menu_btn;    /* 헤더바 왼쪽 ☰ 메뉴 버튼 (언어 전환 시 팝업 재생성) */
    GtkWidget *lang_btn;        /* 한/영 전환 버튼 — 라벨에 '전환될 언어'를 표시 */
    GtkWidget *hb_new, *hb_open_file, *hb_open_folder, *hb_save, *hb_run;  /* 헤더바 아이콘 버튼 (언어 전환 시 툴팁 갱신) */
    GtkWidget *welcome_page;    /* 시작 화면 — 파일을 열면 사라지고 탭이 없으면 다시 나타난다 */

    Explorer *explorer;
    Terminal *terminal;
    AiPanel *ai_panel;
};

/* 시작 화면 관련 — 아래쪽에 정의 */
static void welcome_show(AppState *app);
static void welcome_hide(AppState *app);
static void open_file_dialog(AppState *app);

static void close_tab_button_clicked(GtkButton *btn, gpointer user_data);
static void add_new_tab(AppState *app, const char *path, const char *content, gboolean switch_to_it);

/* ---------------- 탭 조회 헬퍼 ---------------- */

static EditorTab *tab_for_page(GtkNotebook *nb, gint page_num) {
    GtkWidget *page = gtk_notebook_get_nth_page(nb, page_num);
    if (!page) return NULL;
    return (EditorTab *)g_object_get_data(G_OBJECT(page), "tab");
}

static EditorTab *current_tab(AppState *app) {
    gint page = gtk_notebook_get_current_page(app->editor_tabs);
    if (page < 0) return NULL;
    return tab_for_page(app->editor_tabs, page);
}

static EditorTab *find_tab_by_path(AppState *app, const char *path) {
    gint n = gtk_notebook_get_n_pages(app->editor_tabs);
    for (gint i = 0; i < n; i++) {
        EditorTab *tab = tab_for_page(app->editor_tabs, i);
        if (tab && tab->file_path && g_strcmp0(tab->file_path, path) == 0) return tab;
    }
    return NULL;
}

/* ---------------- 표시 갱신 ---------------- */

static void update_tab_label(EditorTab *tab) {
    const char *base = tab->file_path ? strrchr(tab->file_path, '/') : NULL;
    const char *name = tab->file_path ? (base ? base + 1 : tab->file_path) : tr(STR_UNTITLED);
    char text[300];
    snprintf(text, sizeof(text), "%s%s", name, tab->dirty ? " ●" : "");
    gtk_label_set_text(tab->tab_text_label, text);
}

static void update_title(AppState *app) {
    EditorTab *tab = current_tab(app);
    /* 헤더바 제목 아래 작은 글씨(subtitle) 자리에 현재 파일 경로를 보여준다 */
    if (!tab) {
        gtk_header_bar_set_subtitle(app->header_bar, NULL);
        return;
    }
    const char *name = tab->file_path ? tab->file_path : tr(STR_UNTITLED);
    char text[1200];
    snprintf(text, sizeof(text), "%s%s", name, tab->dirty ? " ●" : "");
    gtk_header_bar_set_subtitle(app->header_bar, text);
}

static void log_console(AppState *app, const char *text, gboolean is_error) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(app->console);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert_with_tags_by_name(buf, &end, text, -1, is_error ? "err" : "ok", NULL);
    gtk_text_buffer_insert(buf, &end, "\n", -1);
    gtk_text_buffer_get_end_iter(buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_buffer_place_cursor(buf, &end);
    gtk_text_view_scroll_mark_onscreen(app->console, mark);
}

/* ---------------- 저장 / 열기 / 새 탭 ---------------- */

static gboolean confirm_discard_tab(AppState *app, EditorTab *tab) {
    if (!tab->dirty) return TRUE;
    GtkWidget *dialog = gtk_message_dialog_new(
        app->window, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "%s", tr(STR_CONFIRM_DISCARD));
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

static void save_tab(AppState *app, EditorTab *tab);

static void save_tab_as(AppState *app, EditorTab *tab) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        tr(STR_SAVE_AS_TITLE), app->window, GTK_FILE_CHOOSER_ACTION_SAVE,
        tr(STR_BTN_CANCEL), GTK_RESPONSE_CANCEL, tr(STR_BTN_SAVE), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_free(tab->file_path);
        tab->file_path = g_strdup(path);
        g_free(path);
        gtk_widget_destroy(dialog);
        recent_add(tab->file_path);  /* 새 이름으로 저장한 파일도 최근 목록에 */
        save_tab(app, tab);
        return;
    }
    gtk_widget_destroy(dialog);
}

static void save_tab(AppState *app, EditorTab *tab) {
    if (!tab->file_path) {
        save_tab_as(app, tab);
        return;
    }
    char *content = editor_get_content(tab->editor);
    GError *error = NULL;
    if (!g_file_set_contents(tab->file_path, content, -1, &error)) {
        char *msg = trf(STR_SAVE_ERR_FMT, error->message);
        GtkWidget *dialog = gtk_message_dialog_new(
            app->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(msg);
        g_error_free(error);
    } else {
        tab->dirty = FALSE;
        update_tab_label(tab);
        update_title(app);
        char *msg = trf(STR_SAVED_FMT, tab->file_path);
        log_console(app, msg, FALSE);
        g_free(msg);
    }
    g_free(content);
}

static void save_file(AppState *app) {
    EditorTab *tab = current_tab(app);
    if (tab) save_tab(app, tab);
}

static void save_file_as(AppState *app) {
    EditorTab *tab = current_tab(app);
    if (tab) save_tab_as(app, tab);
}

static void new_file(AppState *app) {
    welcome_hide(app);
    add_new_tab(app, NULL, "", TRUE);
}

static const char *TEXT_EXTENSIONS[] = {
    ".c", ".h", ".py", ".txt", ".md", ".json", ".csv", ".log",
    ".html", ".css", ".js", ".xml", ".yaml", ".yml", ".ini", ".cpp", ".java", NULL
};

static gboolean is_text_file(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return FALSE;
    for (int i = 0; TEXT_EXTENSIONS[i]; i++) {
        if (g_ascii_strcasecmp(dot, TEXT_EXTENSIONS[i]) == 0) return TRUE;
    }
    return FALSE;
}

static void load_file_into_editor(const char *path, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!is_text_file(path)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            app->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "%s", tr(STR_UNSUPPORTED));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* 이미 열려 있는 파일이면 새 탭을 만들지 않고 그 탭으로 전환만 한다 */
    EditorTab *existing = find_tab_by_path(app, path);
    if (existing) {
        gint n = gtk_notebook_get_n_pages(app->editor_tabs);
        for (gint i = 0; i < n; i++) {
            if (tab_for_page(app->editor_tabs, i) == existing) {
                gtk_notebook_set_current_page(app->editor_tabs, i);
                break;
            }
        }
        return;
    }

    char *content = NULL;
    gsize len = 0;
    GError *error = NULL;
    if (!g_file_get_contents(path, &content, &len, &error)) {
        char *msg = trf(STR_OPEN_ERR_FMT, error->message);
        GtkWidget *dialog = gtk_message_dialog_new(
            app->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
        g_free(msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_error_free(error);
        return;
    }

    welcome_hide(app);
    add_new_tab(app, path, content, TRUE);
    recent_add(path);  /* 최근 파일 목록 맨 앞에 기록 */
    g_free(content);
}

/* ---------------- 파일 직접 열기 (단일 파일 대화상자) ---------------- */

static void open_file_dialog(AppState *app) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        tr(STR_FILE_SELECT), app->window, GTK_FILE_CHOOSER_ACTION_OPEN,
        tr(STR_BTN_CANCEL), GTK_RESPONSE_CANCEL, tr(STR_BTN_OPEN), GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, tr(STR_FILE_FILTER_NAME));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_destroy(dialog);
        if (path) {
            load_file_into_editor(path, app);
            g_free(path);
        }
        return;
    }
    gtk_widget_destroy(dialog);
}

/* ---------------- 시작 화면 (웰컴 페이지) ---------------- */

/* 웰컴 페이지의 최근 파일 항목 — 버튼에 경로를 달아둔다 */
static void on_welcome_recent_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    const char *path = g_object_get_data(G_OBJECT(btn), "path");
    if (path) load_file_into_editor(path, app);
}

static void on_welcome_new(GtkButton *btn, gpointer user_data) { (void)btn; new_file((AppState *)user_data); }
static void on_welcome_open_file(GtkButton *btn, gpointer user_data) { (void)btn; open_file_dialog((AppState *)user_data); }
static void on_welcome_open_folder(GtkButton *btn, gpointer user_data) {
    (void)btn;
    explorer_open_folder_dialog(((AppState *)user_data)->explorer);
}

/* 웰컴 페이지용 평평한 버튼 한 개 만들기 — 텍스트 왼쪽 정렬 */
static GtkWidget *welcome_button(AppState *app, const char *text, GCallback cb) {
    GtkWidget *btn = gtk_button_new_with_label(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat-btn");
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    gtk_widget_set_focus_on_click(btn, FALSE);
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(btn));
    if (child) gtk_widget_set_halign(child, GTK_ALIGN_START);
    g_signal_connect(btn, "clicked", cb, app);
    return btn;
}

static GtkWidget *build_welcome_page(AppState *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(box, 48);
    gtk_widget_set_margin_bottom(box, 48);
    gtk_widget_set_margin_start(box, 48);
    gtk_widget_set_margin_end(box, 48);

    GtkWidget *title = gtk_label_new(tr(STR_WELCOME_TITLE));
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "welcome-title");
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    GtkWidget *sub = gtk_label_new(tr(STR_WELCOME_SUB));
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "welcome-sub");
    gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 14);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(actions), welcome_button(app, tr(STR_WELCOME_NEW), G_CALLBACK(on_welcome_new)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), welcome_button(app, tr(STR_WELCOME_OPEN_FILE), G_CALLBACK(on_welcome_open_file)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), welcome_button(app, tr(STR_WELCOME_OPEN_FOLDER), G_CALLBACK(on_welcome_open_folder)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 10);

    GtkWidget *section = gtk_label_new(tr(STR_WELCOME_RECENT));
    gtk_style_context_add_class(gtk_widget_get_style_context(section), "welcome-section");
    gtk_widget_set_halign(section, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), section, FALSE, FALSE, 12);

    char **recents = recent_get();
    if (!recents || !recents[0]) {
        GtkWidget *empty = gtk_label_new(tr(STR_RECENT_EMPTY));
        gtk_style_context_add_class(gtk_widget_get_style_context(empty), "welcome-sub");
        gtk_widget_set_halign(empty, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), empty, FALSE, FALSE, 0);
    } else {
        for (int i = 0; recents[i]; i++) {
            char *base = g_path_get_basename(recents[i]);
            GtkWidget *btn = welcome_button(app, base, G_CALLBACK(on_welcome_recent_clicked));
            gtk_widget_set_tooltip_text(btn, recents[i]);
            g_object_set_data_full(G_OBJECT(btn), "path", g_strdup(recents[i]), g_free);
            gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
            g_free(base);
        }
    }
    g_strfreev(recents);

    return box;
}

static void welcome_show(AppState *app) {
    if (app->welcome_page) return;
    app->welcome_page = build_welcome_page(app);
    GtkWidget *label = gtk_label_new(tr(STR_WELCOME_TAB));
    gtk_notebook_append_page(app->editor_tabs, app->welcome_page, label);
    gtk_widget_show_all(app->welcome_page);
    gtk_notebook_set_current_page(app->editor_tabs, gtk_notebook_get_n_pages(app->editor_tabs) - 1);
    update_title(app);
}

static void welcome_hide(AppState *app) {
    if (!app->welcome_page) return;
    gint n = gtk_notebook_get_n_pages(app->editor_tabs);
    for (gint i = 0; i < n; i++) {
        if (gtk_notebook_get_nth_page(app->editor_tabs, i) == app->welcome_page) {
            gtk_notebook_remove_page(app->editor_tabs, i);
            break;
        }
    }
    app->welcome_page = NULL;
}

static void on_folder_opened(const char *folder, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (app->terminal) terminal_set_cwd(app->terminal, folder);
}

static void on_editor_changed(void *user_data) {
    EditorTab *tab = (EditorTab *)user_data;
    tab->dirty = TRUE;
    update_tab_label(tab);
    if (current_tab(tab->app) == tab) update_title(tab->app);
}

/* 탭을 닫는다. 마지막 탭이면 빈 탭 하나를 새로 열어 항상 탭이 1개 이상 있게 한다. */
static void close_tab(AppState *app, EditorTab *tab) {
    if (!confirm_discard_tab(app, tab)) return;

    gint n = gtk_notebook_get_n_pages(app->editor_tabs);
    gint page_num = -1;
    for (gint i = 0; i < n; i++) {
        if (tab_for_page(app->editor_tabs, i) == tab) { page_num = i; break; }
    }
    if (page_num < 0) return;

    gtk_notebook_remove_page(app->editor_tabs, page_num);
    g_free(tab->file_path);
    g_free(tab->editor);  /* CodeEditor 위젯 자체는 notebook 이 이미 파괴함; 래퍼 구조체만 해제 */
    g_free(tab);

    if (gtk_notebook_get_n_pages(app->editor_tabs) == 0) {
        welcome_show(app);   /* 마지막 탭을 닫으면 시작 화면으로 돌아간다 */
    } else {
        update_title(app);
    }
}

static void close_tab_button_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    EditorTab *tab = (EditorTab *)user_data;
    close_tab(tab->app, tab);
}

static void on_editor_tabs_switch_page(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer user_data) {
    (void)nb; (void)page; (void)page_num;
    update_title((AppState *)user_data);
}

static void add_new_tab(AppState *app, const char *path, const char *content, gboolean switch_to_it) {
    EditorTab *tab = g_new0(EditorTab, 1);
    tab->app = app;
    tab->file_path = path ? g_strdup(path) : NULL;
    tab->dirty = FALSE;
    tab->editor = editor_new(on_editor_changed, tab);
    editor_set_content(tab->editor, content ? content : "");

    /* 탭 라벨: 파일명 + 닫기(×) 버튼 */
    GtkWidget *label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    tab->tab_text_label = GTK_LABEL(gtk_label_new(""));
    gtk_box_pack_start(GTK_BOX(label_box), GTK_WIDGET(tab->tab_text_label), FALSE, FALSE, 0);

    GtkWidget *close_btn = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(close_tab_button_clicked), tab);
    gtk_box_pack_start(GTK_BOX(label_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(label_box);

    update_tab_label(tab);

    g_object_set_data(G_OBJECT(tab->editor->scrolled_window), "tab", tab);
    gint page_num = gtk_notebook_append_page(app->editor_tabs, tab->editor->scrolled_window, label_box);
    gtk_widget_show_all(tab->editor->scrolled_window);

    if (switch_to_it) gtk_notebook_set_current_page(app->editor_tabs, page_num);
    update_title(app);
}

/* ---------------- AI 패널과 연결하는 다리(bridge) 함수 ---------------- */

static char *bridge_get_code(void *user_data) {
    AppState *app = (AppState *)user_data;
    EditorTab *tab = current_tab(app);
    return tab ? editor_get_content(tab->editor) : g_strdup("");  /* AiPanel 이 g_free 함 */
}

static void bridge_apply_code(const char *code, void *user_data) {
    AppState *app = (AppState *)user_data;
    EditorTab *tab = current_tab(app);
    if (!tab) return;
    editor_set_content(tab->editor, code);
    tab->dirty = TRUE;
    update_tab_label(tab);
    update_title(app);
    log_console(app, tr(STR_AI_APPLIED_CONSOLE), FALSE);
}

/* ---------------- 코드 실행 (.py 는 python3, .c 는 gcc 컴파일 후 실행) ---------------- */

typedef struct {
    AppState *app;
    char *file_path;
} RunJob;

static gboolean run_done_idle(gpointer data) {
    RunJob *job = (RunJob *)data;
    g_free(job->file_path);
    g_free(job);
    return G_SOURCE_REMOVE;
}

typedef struct {
    AppState *app;
    char *output;
    gboolean is_error;
} RunOutputMsg;

static gboolean run_output_idle(gpointer data) {
    RunOutputMsg *msg = (RunOutputMsg *)data;
    log_console(msg->app, msg->output, msg->is_error);
    g_free(msg->output);
    g_free(msg);
    return G_SOURCE_REMOVE;
}

static void post_output(AppState *app, const char *text, gboolean is_error) {
    RunOutputMsg *msg = g_new0(RunOutputMsg, 1);
    msg->app = app;
    msg->output = g_strdup(text);
    msg->is_error = is_error;
    g_idle_add(run_output_idle, msg);
}

/* argv 를 셸을 거치지 않고 직접 실행하고, stdout+stderr 를 out 에 모은다.
 * g_spawn_sync 는 GLib 의 멀티 OS 프로세스 실행 함수 — Linux 와 Windows 에서
 * 같은 코드로 동작한다 (fork+exec / CreateProcess 를 내부에서 처리).
 * 파일 경로에 특수문자가 있어도 셸 해석이 없으므로 안전하다
 * (파이썬 버전의 subprocess.run([...]) — 리스트 인자 — 와 같은 방식).
 * 인자는 const 가 없는 배열을 요구하므로 호출부에서 복사본을 쓴다. */
static int run_argv_capture(char *argv[], GString *out) {
    char *std_out = NULL, *std_err = NULL;
    gint status = -1;
    GError *error = NULL;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &std_out, &std_err, &status, &error)) {
        char *msg = trf(STR_SPAWN_ERR_FMT, error->message);
        g_string_append(out, msg);
        g_free(msg);
        g_error_free(error);
        g_free(std_out);
        g_free(std_err);
        return -1;
    }

    if (std_out) g_string_append(out, std_out);
    if (std_err) g_string_append(out, std_err);
    g_free(std_out);
    g_free(std_err);

#ifdef G_OS_WIN32
    return status;   /* Windows: status 가 곧 종료 코드 */
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/* 플랫폼별 파이썬 실행 파일 이름 — Windows 는 python, Linux 는 python3 */
static const char *python_cmd(void) {
#ifdef G_OS_WIN32
    return "python";
#else
    return "python3";
#endif
}

static gpointer run_worker(gpointer data) {
    RunJob *job = (RunJob *)data;
    const char *path = job->file_path;
    const char *dot = strrchr(path, '.');
    GString *out = g_string_new("");
    int exit_code = -1;

    if (dot && g_ascii_strcasecmp(dot, ".py") == 0) {
        char *argv[] = { (char *)python_cmd(), (char *)path, NULL };
        exit_code = run_argv_capture(argv, out);
        if (exit_code == -1) {
            post_output(job->app, tr(STR_PY_NOT_FOUND), TRUE);
        }
    } else if (dot && g_ascii_strcasecmp(dot, ".c") == 0) {
        char bin_path[1200];
#ifdef G_OS_WIN32
        snprintf(bin_path, sizeof(bin_path), "%s.exe", path);
#else
        snprintf(bin_path, sizeof(bin_path), "%s.out", path);
#endif
        char *compile_argv[] = { (char *)"gcc", (char *)path, "-o", bin_path, NULL };
        int compile_rc = run_argv_capture(compile_argv, out);
        if (compile_rc == 0) {
            char *run_argv[] = { bin_path, NULL };
            exit_code = run_argv_capture(run_argv, out);
        } else {
            exit_code = compile_rc;
        }
    } else {
        post_output(job->app, tr(STR_RUN_UNSUPPORTED), TRUE);
        g_idle_add(run_done_idle, job);
        g_string_free(out, TRUE);
        return NULL;
    }

    if (out->len > 0) post_output(job->app, out->str, FALSE);
    char *code_msg = trf(STR_EXIT_CODE_FMT, exit_code);
    post_output(job->app, code_msg, FALSE);
    g_free(code_msg);
    g_string_free(out, TRUE);

    g_idle_add(run_done_idle, job);
    return NULL;
}

static void run_current_file(AppState *app) {
    EditorTab *tab = current_tab(app);
    if (!tab || !tab->file_path) {
        GtkWidget *dialog = gtk_message_dialog_new(
            app->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "%s", tr(STR_RUN_SAVE_FIRST));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    save_tab(app, tab);
    gtk_notebook_set_current_page(app->bottom_tabs, 0);

    char *msg = trf(STR_RUN_FMT, tab->file_path);
    log_console(app, msg, FALSE);
    g_free(msg);

    RunJob *job = g_new0(RunJob, 1);
    job->app = app;
    job->file_path = g_strdup(tab->file_path);
    GThread *thread = g_thread_new("run-file", run_worker, job);
    g_thread_unref(thread);
}

/* ---------------- 메뉴 ---------------- */

static void on_menu_new(GtkMenuItem *item, gpointer user_data) { (void)item; new_file((AppState *)user_data); }
static void on_menu_open_file(GtkMenuItem *item, gpointer user_data) {
    (void)item; open_file_dialog((AppState *)user_data);
}
static void on_menu_open_folder(GtkMenuItem *item, gpointer user_data) {
    (void)item; explorer_open_folder_dialog(((AppState *)user_data)->explorer);
}
static void on_menu_save(GtkMenuItem *item, gpointer user_data) { (void)item; save_file((AppState *)user_data); }
static void on_menu_save_as(GtkMenuItem *item, gpointer user_data) { (void)item; save_file_as((AppState *)user_data); }
static void on_menu_close_tab(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    AppState *app = (AppState *)user_data;
    EditorTab *tab = current_tab(app);
    if (tab) close_tab(app, tab);
}
static void on_menu_quit(GtkMenuItem *item, gpointer user_data) { (void)item; (void)user_data; gtk_main_quit(); }
static void on_menu_run(GtkMenuItem *item, gpointer user_data) { (void)item; run_current_file((AppState *)user_data); }
static void on_menu_terminal(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    AppState *app = (AppState *)user_data;
    gtk_notebook_set_current_page(app->bottom_tabs, 1);
    terminal_focus_input(app->terminal);
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    gtk_main_quit();
    return FALSE;
}

/* ---------------- 최근 파일 하위 메뉴 ---------------- */

static void on_recent_item_activated(GtkMenuItem *item, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    const char *path = g_object_get_data(G_OBJECT(item), "path");
    if (path) load_file_into_editor(path, app);
}

/* 하위 메뉴가 열릴 때마다 최신 목록으로 다시 채운다 */
static void on_recent_menu_show(GtkWidget *submenu, gpointer user_data) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(submenu));
    for (GList *l = children; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    AppState *app = (AppState *)user_data;
    char **recents = recent_get();
    if (!recents || !recents[0]) {
        GtkWidget *empty = gtk_menu_item_new_with_label(tr(STR_RECENT_EMPTY));
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), empty);
    } else {
        for (int i = 0; recents[i]; i++) {
            char *base = g_path_get_basename(recents[i]);
            GtkWidget *mi = gtk_menu_item_new_with_label(base);
            g_object_set_data_full(G_OBJECT(mi), "path", g_strdup(recents[i]), g_free);
            g_signal_connect(mi, "activate", G_CALLBACK(on_recent_item_activated), app);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), mi);
            g_free(base);
        }
    }
    g_strfreev(recents);
    gtk_widget_show_all(submenu);
}

/* ---------------- 언어 전환 (한/영) ---------------- */

static void rebuild_header_menu(AppState *app);  /* 아래 build_header_menu 와 함께 정의 */
static void headerbar_refresh_language(AppState *app);

static void on_language_changed(void *user_data) {
    AppState *app = (AppState *)user_data;

    /* 헤더바 ☰ 메뉴를 새 언어로 다시 만든다 */
    rebuild_header_menu(app);
    headerbar_refresh_language(app);

    /* 나머지 정적 문구 일괄 갱신 */
    gtk_header_bar_set_title(app->header_bar, tr(STR_WINDOW_TITLE));
    update_title(app);
    gint n = gtk_notebook_get_n_pages(app->editor_tabs);
    for (gint i = 0; i < n; i++) {
        EditorTab *tab = tab_for_page(app->editor_tabs, i);
        if (tab) update_tab_label(tab);
    }
    ai_panel_refresh_language(app->ai_panel);
    explorer_refresh_language(app->explorer);

    /* 시작 화면 문구도 새 언어로 */
    if (app->welcome_page) {
        welcome_hide(app);
        welcome_show(app);
    }
}

static void on_menu_lang_ko(GtkMenuItem *item, gpointer user_data) {
    (void)item; (void)user_data;
    ui_lang_set(UI_LANG_KO);
}

static void on_menu_lang_en(GtkMenuItem *item, gpointer user_data) {
    (void)item; (void)user_data;
    ui_lang_set(UI_LANG_EN);
}

/* ☰ 메뉴 내용 — 파일/실행/보기 세 하위 메뉴를 한 세로 메뉴에 담는다.
 * 헤더바의 메뉴 버튼 팝업으로 쓰인다 (예전 메뉴바를 대체). */
static GtkWidget *build_header_menu(AppState *app) {
    GtkWidget *menu = gtk_menu_new();

    /* 파일 메뉴 */
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_label(tr(STR_MENU_FILE));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);

    GtkWidget *new_item = gtk_menu_item_new_with_label(tr(STR_MENU_NEW));
    GtkWidget *open_file_item = gtk_menu_item_new_with_label(tr(STR_MENU_OPEN_FILE));
    GtkWidget *open_item = gtk_menu_item_new_with_label(tr(STR_MENU_OPEN_FOLDER));
    GtkWidget *save_item = gtk_menu_item_new_with_label(tr(STR_MENU_SAVE));
    GtkWidget *save_as_item = gtk_menu_item_new_with_label(tr(STR_MENU_SAVE_AS));
    GtkWidget *close_tab_item = gtk_menu_item_new_with_label(tr(STR_MENU_CLOSE_TAB));
    GtkWidget *quit_item = gtk_menu_item_new_with_label(tr(STR_MENU_QUIT));

    /* 최근 파일 하위 메뉴 — 열릴 때마다(on_recent_menu_show) 최신 목록으로 채운다 */
    GtkWidget *recent_menu = gtk_menu_new();
    g_signal_connect(recent_menu, "show", G_CALLBACK(on_recent_menu_show), app);
    GtkWidget *recent_item = gtk_menu_item_new_with_label(tr(STR_MENU_RECENT));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent_item), recent_menu);

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), recent_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_as_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), close_tab_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);
    g_signal_connect(new_item, "activate", G_CALLBACK(on_menu_new), app);
    g_signal_connect(open_file_item, "activate", G_CALLBACK(on_menu_open_file), app);
    g_signal_connect(open_item, "activate", G_CALLBACK(on_menu_open_folder), app);
    g_signal_connect(save_item, "activate", G_CALLBACK(on_menu_save), app);
    g_signal_connect(save_as_item, "activate", G_CALLBACK(on_menu_save_as), app);
    g_signal_connect(close_tab_item, "activate", G_CALLBACK(on_menu_close_tab), app);
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_menu_quit), app);

    /* 실행 메뉴 */
    GtkWidget *run_menu = gtk_menu_new();
    GtkWidget *run_item = gtk_menu_item_new_with_label(tr(STR_MENU_RUN));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(run_item), run_menu);
    GtkWidget *run_file_item = gtk_menu_item_new_with_label(tr(STR_MENU_RUN_FILE));
    GtkWidget *terminal_item = gtk_menu_item_new_with_label(tr(STR_MENU_TERMINAL));
    gtk_menu_shell_append(GTK_MENU_SHELL(run_menu), run_file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(run_menu), terminal_item);
    g_signal_connect(run_file_item, "activate", G_CALLBACK(on_menu_run), app);
    g_signal_connect(terminal_item, "activate", G_CALLBACK(on_menu_terminal), app);

    /* 보기 메뉴 — 언어 전환 */
    GtkWidget *view_menu = gtk_menu_new();
    GtkWidget *view_item = gtk_menu_item_new_with_label(tr(STR_MENU_VIEW));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_item), view_menu);

    GtkWidget *lang_menu = gtk_menu_new();
    GtkWidget *lang_item = gtk_menu_item_new_with_label(tr(STR_MENU_LANGUAGE));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(lang_item), lang_menu);

    GtkWidget *ko_item = gtk_menu_item_new_with_label(ui_lang_name(UI_LANG_KO));
    GtkWidget *en_item = gtk_menu_item_new_with_label(ui_lang_name(UI_LANG_EN));
    gtk_menu_shell_append(GTK_MENU_SHELL(lang_menu), ko_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(lang_menu), en_item);
    g_signal_connect(ko_item, "activate", G_CALLBACK(on_menu_lang_ko), app);
    g_signal_connect(en_item, "activate", G_CALLBACK(on_menu_lang_en), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), lang_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), run_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), view_item);

    gtk_widget_show_all(menu);
    return menu;
}

/* 언어 전환 시 호출 — 메뉴 버튼의 팝업을 새로 만들어 갈아끼운다 */
static void rebuild_header_menu(AppState *app) {
    GtkWidget *old = GTK_WIDGET(gtk_menu_button_get_popup(app->menu_btn));
    GtkWidget *menu = build_header_menu(app);
    gtk_menu_button_set_popup(app->menu_btn, menu);
    if (old) gtk_widget_destroy(old);
}

/* ---------------- 헤더바 (상단을 한 줄로 통합) ---------------- */

static void on_hb_new(GtkButton *btn, gpointer user_data) { (void)btn; new_file((AppState *)user_data); }
static void on_hb_open_file(GtkButton *btn, gpointer user_data) { (void)btn; open_file_dialog((AppState *)user_data); }
static void on_hb_open_folder(GtkButton *btn, gpointer user_data) {
    (void)btn;
    explorer_open_folder_dialog(((AppState *)user_data)->explorer);
}
static void on_hb_save(GtkButton *btn, gpointer user_data) { (void)btn; save_file((AppState *)user_data); }
static void on_hb_run(GtkButton *btn, gpointer user_data) { (void)btn; run_current_file((AppState *)user_data); }

static void on_lang_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    ui_lang_toggle();  /* 바뀌면 on_language_changed 가 언어 버튼 라벨도 갱신한다 */
}

/* 아이콘만 있는 헤더바 버튼 한 개 — 툴팁으로 기능을 알려준다 */
static GtkWidget *make_hb_btn(const char *icon_name, const char *tooltip, GCallback cb,
                              AppState *app, GtkWidget **store) {
    GtkWidget *btn = gtk_button_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "hb-btn");
    gtk_widget_set_focus_on_click(btn, FALSE);
    g_signal_connect(btn, "clicked", cb, app);
    if (store) *store = btn;
    return btn;
}

/* 언어 버튼 라벨 — '지금 누르면 바뀔 언어'를 보여준다 */
static void lang_btn_refresh(AppState *app) {
    const char *label = ui_lang_get() == UI_LANG_KO ? "EN" : "한";
    gtk_button_set_label(GTK_BUTTON(app->lang_btn), label);
    gtk_widget_set_tooltip_text(app->lang_btn, tr(STR_MENU_LANGUAGE));
}

static GtkHeaderBar *build_header_bar(AppState *app) {
    GtkHeaderBar *bar = GTK_HEADER_BAR(gtk_header_bar_new());
    gtk_header_bar_set_show_close_button(bar, TRUE);
    gtk_header_bar_set_title(bar, tr(STR_WINDOW_TITLE));

    /* 왼쪽: ☰ 메뉴 + 새 파일/파일 열기/폴더 열기 */
    app->menu_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_direction(app->menu_btn, GTK_ARROW_DOWN);
    GtkWidget *menu_icon = gtk_image_new_from_icon_name("open-menu", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(app->menu_btn), menu_icon);
    gtk_menu_button_set_popup(app->menu_btn, build_header_menu(app));
    gtk_header_bar_pack_start(bar, GTK_WIDGET(app->menu_btn));

    gtk_header_bar_pack_start(bar, make_hb_btn("document-new", tr(STR_MENU_NEW), G_CALLBACK(on_hb_new), app, &app->hb_new));
    gtk_header_bar_pack_start(bar, make_hb_btn("document-open", tr(STR_MENU_OPEN_FILE), G_CALLBACK(on_hb_open_file), app, &app->hb_open_file));
    gtk_header_bar_pack_start(bar, make_hb_btn("folder-open", tr(STR_MENU_OPEN_FOLDER), G_CALLBACK(on_hb_open_folder), app, &app->hb_open_folder));

    /* 오른쪽: 언어 전환 + 저장 + 실행(강조색). pack_end 은 먼저 넣은 것이 더 오른쪽에 붙는다 */
    app->lang_btn = gtk_button_new_with_label("EN");
    gtk_style_context_add_class(gtk_widget_get_style_context(app->lang_btn), "hb-btn");
    gtk_widget_set_focus_on_click(app->lang_btn, FALSE);
    g_signal_connect(app->lang_btn, "clicked", G_CALLBACK(on_lang_btn_clicked), app);
    gtk_header_bar_pack_end(bar, app->lang_btn);

    gtk_header_bar_pack_end(bar, make_hb_btn("document-save", tr(STR_MENU_SAVE), G_CALLBACK(on_hb_save), app, &app->hb_save));

    GtkWidget *run_btn = make_hb_btn("media-playback-start", tr(STR_MENU_RUN_FILE), G_CALLBACK(on_hb_run), app, &app->hb_run);
    gtk_style_context_add_class(gtk_widget_get_style_context(run_btn), "accent-btn");
    gtk_header_bar_pack_end(bar, run_btn);

    lang_btn_refresh(app);
    return bar;
}

/* 언어 전환 시 헤더바 버튼 툴팁 갱신 */
static void headerbar_refresh_language(AppState *app) {
    gtk_widget_set_tooltip_text(app->hb_new, tr(STR_MENU_NEW));
    gtk_widget_set_tooltip_text(app->hb_open_file, tr(STR_MENU_OPEN_FILE));
    gtk_widget_set_tooltip_text(app->hb_open_folder, tr(STR_MENU_OPEN_FOLDER));
    gtk_widget_set_tooltip_text(app->hb_save, tr(STR_MENU_SAVE));
    gtk_widget_set_tooltip_text(app->hb_run, tr(STR_MENU_RUN_FILE));
    lang_btn_refresh(app);
}

/* ---------------- 단축키 ---------------- */

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;

    if (ctrl && event->keyval == GDK_KEY_n) { new_file(app); return TRUE; }
    if (ctrl && event->keyval == GDK_KEY_o) {
        if (shift) explorer_open_folder_dialog(app->explorer);
        else       open_file_dialog(app);
        return TRUE;
    }
    if (ctrl && event->keyval == GDK_KEY_s) { save_file(app); return TRUE; }
    if (ctrl && event->keyval == GDK_KEY_w) {
        EditorTab *tab = current_tab(app);
        if (tab) close_tab(app, tab);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5) { run_current_file(app); return TRUE; }
    if (ctrl && event->keyval == GDK_KEY_grave) {
        gtk_notebook_set_current_page(app->bottom_tabs, 1);
        terminal_focus_input(app->terminal);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    theme_apply();   /* VS Code 스타일 다크 테마 — 위젯 만들기 전에 적용 */
    ui_lang_init();

    AppState *app = g_new0(AppState, 1);
    ui_lang_on_changed(on_language_changed, app);  /* 언어 바뀌면 UI 문구 일괄 갱신 */

    app->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_title(app->window, tr(STR_WINDOW_TITLE));
    gtk_window_set_default_size(app->window, 1180, 760);
    g_signal_connect(app->window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);

    /* 상단을 헤더바 하나로 통합 — ☰ 메뉴 + 아이콘 버튼 + 제목/파일경로 */
    app->header_bar = build_header_bar(app);
    gtk_window_set_titlebar(app->window, GTK_WIDGET(app->header_bar));

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root_box);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root_box), paned, TRUE, TRUE, 0);

    /* [좌측] 파일 탐색기 */
    app->explorer = explorer_new(app->window, load_file_into_editor, app, on_folder_opened, app);
    gtk_paned_pack1(GTK_PANED(paned), app->explorer->box, FALSE, TRUE);
    gtk_widget_set_size_request(app->explorer->box, 180, -1);

    /* [중앙] 파일 탭 + 하단(OUTPUT/TERMINAL) — 현재 파일 경로는 헤더바 subtitle 에 표시 */
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* 노트북(OUTPUT/TERMINAL) — 파이썬 버전처럼 아래쪽에, 에디터 탭은 남은 공간 전체 */
    GtkWidget *paned_v = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(center_box), paned_v, TRUE, TRUE, 0);

    /* 3번 기능: 파일 탭 묶음 (탭이 여러 개면 자동으로 스크롤 화살표가 생김) */
    app->editor_tabs = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_scrollable(app->editor_tabs, TRUE);
    g_signal_connect(app->editor_tabs, "switch-page", G_CALLBACK(on_editor_tabs_switch_page), app);
    gtk_paned_pack1(GTK_PANED(paned_v), GTK_WIDGET(app->editor_tabs), TRUE, TRUE);

    app->bottom_tabs = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_paned_pack2(GTK_PANED(paned_v), GTK_WIDGET(app->bottom_tabs), FALSE, TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(app->bottom_tabs), -1, 200);

    /* OUTPUT 탭 */
    GtkWidget *output_scroll = gtk_scrolled_window_new(NULL, NULL);
    app->console = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(app->console, FALSE);
    gtk_text_view_set_monospace(app->console, TRUE);
    GtkTextBuffer *console_buf = gtk_text_view_get_buffer(app->console);
    gtk_text_buffer_create_tag(console_buf, "err", "foreground", "#f44747", NULL);
    gtk_text_buffer_create_tag(console_buf, "ok", "foreground", "#4ec9b0", NULL);
    gtk_container_add(GTK_CONTAINER(output_scroll), GTK_WIDGET(app->console));
    gtk_notebook_append_page(app->bottom_tabs, output_scroll, gtk_label_new("OUTPUT"));

    /* TERMINAL 탭 (Linux: VTE, Windows: ConPTY — 같은 인터페이스) */
    app->terminal = terminal_new();
    gtk_notebook_append_page(app->bottom_tabs, terminal_get_box(app->terminal), gtk_label_new("TERMINAL"));

    /* [우측] AI 패널 */
    app->ai_panel = ai_panel_new(bridge_get_code, app, bridge_apply_code, app);

    /* GtkPaned 는 자식이 2개뿐이라, 중앙+우측을 감싸는 안쪽 paned 를 하나 더 둔다
     * (파이썬 tkinter 의 PanedWindow 는 자식 3개를 한 번에 받지만 GTK 는 그렇지 않음) */
    GtkWidget *inner_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(inner_paned), center_box, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(inner_paned), app->ai_panel->box, FALSE, TRUE);
    gtk_widget_set_size_request(app->ai_panel->box, 320, -1);
    gtk_paned_pack2(GTK_PANED(paned), inner_paned, TRUE, TRUE);

    /* 시작할 때는 빈 탭 대신 시작 화면(웰컴 페이지)을 보여준다 */
    welcome_show(app);

    gtk_widget_show_all(GTK_WIDGET(app->window));
    gtk_main();
    return 0;
}
