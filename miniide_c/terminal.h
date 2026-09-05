/* 진짜 터미널 위젯 — 플랫폼별 구현을 하나의 인터페이스로 제공.
 * Linux   : terminal.c        (VTE — gnome-terminal 이 쓰는 라이브러리)
 * Windows : conpty_terminal.c (ConPTY — Windows 10+ 공식 유사 터미널 API)
 * 구조체 정의는 각 구현 파일 안에 있고(불투명 타입), 빌드 시 OS 에 맞는
 * 소스 파일 하나만 링크된다.
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <gtk/gtk.h>

typedef struct Terminal Terminal;

Terminal *terminal_new(void);

/* 노트북 탭 등에 붙일 최상위 위젯 */
GtkWidget *terminal_get_box(Terminal *terminal);

/* 폴더 열기 등으로 작업 폴더가 바뀔 때 호출 — 실행 중인 셸에 cd 명령을 보낸다 */
void terminal_set_cwd(Terminal *terminal, const char *folder);

void terminal_focus_input(Terminal *terminal);

#endif
