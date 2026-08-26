/* 진짜 터미널 위젯 — VTE(gnome-terminal 이 쓰는 것과 같은 라이브러리)로
 * 실제 셸 프로세스를 붙인다. 색상/커서 이동 등 완전한 터미널 렌더링을 지원한다.
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <gtk/gtk.h>
#include <vte/vte.h>

typedef struct {
    GtkWidget *box;      /* 노트북 탭에 붙일 최상위 위젯 */
    VteTerminal *vte;
    char cwd[1024];
} Terminal;

Terminal *terminal_new(void);

/* 폴더 열기 등으로 작업 폴더가 바뀔 때 호출 — 실행 중인 셸에 cd 명령을 보낸다 */
void terminal_set_cwd(Terminal *terminal, const char *folder);

void terminal_focus_input(Terminal *terminal);

#endif
