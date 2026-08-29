/* Linux 용 터미널 구현 — VTE(gnome-terminal 과 같은 라이브러리)로
 * 실제 셸 프로세스를 붙인다. 색상/커서 이동 등 완전한 터미널 렌더링을 지원한다.
 * (Windows 용은 conpty_terminal.c — 같은 인터페이스를 구현한다)
 */
#include "terminal.h"

#include <vte/vte.h>

struct Terminal {
    GtkWidget *box;      /* 노트북 탭에 붙일 최상위 위젯 */
    VteTerminal *vte;
    char cwd[1024];
};

static void spawn_shell(Terminal *terminal) {
    const char *shell = g_getenv("SHELL");
    if (!shell) shell = "/bin/bash";
    char *argv[] = { (char *)shell, NULL };

    /* VTE 가 pty 생성/fork/exec 을 전부 대신 해준다 — 색상, 커서 이동 같은
     * 완전한 터미널 렌더링이 되는 진짜 셸이 화면에 뜬다. */
    vte_terminal_spawn_async(
        terminal->vte, VTE_PTY_DEFAULT,
        terminal->cwd, argv, NULL,
        G_SPAWN_DEFAULT, NULL, NULL, NULL,
        -1, NULL, NULL, NULL);
}

Terminal *terminal_new(void) {
    Terminal *terminal = g_new0(Terminal, 1);
    char *cwd = g_get_current_dir();
    g_strlcpy(terminal->cwd, cwd, sizeof(terminal->cwd));
    g_free(cwd);

    terminal->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *vte_widget = vte_terminal_new();
    terminal->vte = VTE_TERMINAL(vte_widget);
    vte_terminal_set_scrollback_lines(terminal->vte, 5000);
    vte_terminal_set_color_background(terminal->vte, &(GdkRGBA){0.047, 0.047, 0.047, 1.0});
    vte_terminal_set_color_foreground(terminal->vte, &(GdkRGBA){0.831, 0.831, 0.831, 1.0});
    vte_terminal_set_font_scale(terminal->vte, 1.0);

    /* VTE 표준 패턴: 터미널 위젯 + 그 자체의 vadjustment 를 공유하는 스크롤바 */
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(row), vte_widget, TRUE, TRUE, 0);
    GtkWidget *scrollbar = gtk_scrollbar_new(
        GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte_widget)));
    gtk_box_pack_start(GTK_BOX(row), scrollbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(terminal->box), row, TRUE, TRUE, 0);

    spawn_shell(terminal);

    return terminal;
}

GtkWidget *terminal_get_box(Terminal *terminal) {
    return terminal->box;
}

void terminal_set_cwd(Terminal *terminal, const char *folder) {
    g_strlcpy(terminal->cwd, folder, sizeof(terminal->cwd));
    if (terminal->vte) {
        char *quoted = g_shell_quote(folder);
        char *cmd = g_strdup_printf("cd %s\n", quoted);
        vte_terminal_feed_child(terminal->vte, cmd, -1);
        g_free(cmd);
        g_free(quoted);
    }
}

void terminal_focus_input(Terminal *terminal) {
    gtk_widget_grab_focus(GTK_WIDGET(terminal->vte));
}
