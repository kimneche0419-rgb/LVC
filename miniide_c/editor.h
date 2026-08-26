/* 코드 에디터 위젯 — GtkTextView 기반, 간단한 문법 강조 포함.
 * 파이썬 버전(miniide/editor.py)의 CodeEditor 를 C로 이식.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <gtk/gtk.h>

typedef struct {
    GtkWidget *scrolled_window;  /* paned 에 붙일 최상위 위젯 */
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    void (*on_change)(void *user_data);
    void *on_change_data;
} CodeEditor;

/* CodeEditor 를 생성하고 GtkScrolledWindow 안에 담아 반환한다.
 * on_change 는 사용자가 타이핑할 때마다 호출됨(더티 표시 등에 사용). NULL 가능. */
CodeEditor *editor_new(void (*on_change)(void *user_data), void *on_change_data);

/* 에디터 전체 내용을 문자열로 반환 (호출자가 g_free 해야 함) */
char *editor_get_content(CodeEditor *editor);

/* 에디터 내용을 통째로 교체 (더티 표시 없이 — 파일 열기/AI 코드 적용용) */
void editor_set_content(CodeEditor *editor, const char *content);

#endif
