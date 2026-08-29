#include "theme.h"

#include <gtk/gtk.h>

/* VS Code 팔레트 — 에디터(editor.c #1e1e1e)와 색을 맞춘다.
 *   배경 #1e1e1e / 사이드바 #252526 / 상단바 #2d2d2d / 포인트 #0e639c
 * GTK 는 부모 위젯의 색이 자식으로 상속되지 않는 경우가 많아서
 * 위젯 종류별로 일일이 지정하는 것이 정석이다. */
static const char *DARK_CSS =
    /* 기본 바탕 */
    "window, dialog { background-color: #252526; color: #d4d4d4; }"
    "label { color: #d4d4d4; }"

    /* 헤더바 — 상단을 한 줄로 통합한 바 */
    "headerbar { background-color: #2d2d2d; border-bottom: 1px solid #1b1b1c; min-height: 44px; }"
    "headerbar:titlebar label { color: #d4d4d4; }"

    /* 헤더바 아이콘 버튼 — 테두리 없는 플랫 스타일 */
    ".hb-btn { background-color: transparent; color: #d4d4d4; border: none;"
    "          padding: 5px; border-radius: 4px; }"
    ".hb-btn:hover { background-color: #3c3c3c; }"
    ".hb-btn:active { background-color: #0e639c; color: #ffffff; }"
    /* 실행(▶) 버튼 — 강조색 */
    ".accent-btn, button.accent-btn { background-color: #0e639c; color: #ffffff; border: none;"
    "                                  padding: 5px; border-radius: 4px; }"
    ".accent-btn:hover { background-color: #1177bb; }"

    /* ☰ 팝업 메뉴 */
    "menu { background-color: #2d2d2d; color: #d4d4d4; }"
    "menu menuitem { padding: 4px 10px; }"
    "menu menuitem:hover { background-color: #0e639c; color: #ffffff; }"
    "separator { background-color: #3c3c3c; }"
    "menu separator { background-color: #454545; }"

    /* 일반 버튼 (대화상자 등) */
    "button { background-color: #3a3d41; color: #d4d4d4;"
    "         border: 1px solid #4a4d51; padding: 4px 10px; border-radius: 3px; }"
    "button:hover { background-color: #45494e; }"
    "button:active { background-color: #0e639c; color: #ffffff; border-color: #0e639c; }"
    "button:disabled { color: #6a6a6a; }"

    /* 시작 화면/기타 평평한 텍스트 버튼 */
    ".flat-btn { background-color: transparent; color: #d4d4d4; border: none;"
    "            padding: 6px 10px; border-radius: 4px; }"
    ".flat-btn:hover { background-color: #3c3c3c; }"
    ".flat-btn:active { background-color: #094771; }"

    /* 노트북(탭 묶음) — 활성 탭은 밝게, 윗모서리 둥글게 */
    "notebook header { background-color: #252526; border-bottom: 1px solid #1b1b1c; }"
    "notebook header tabs { background-color: #252526; }"
    "notebook tab { background-color: #252526; padding: 3px 9px;"
    "               border-radius: 6px 6px 0 0; margin: 3px 1px 0 1px; }"
    "notebook tab:checked { background-color: #1e1e1e; }"
    "notebook tab label { color: #9d9d9d; padding: 0 4px; }"
    "notebook tab:checked label { color: #ffffff; }"

    /* 파일 트리(사이드바) — VS Code 탐색기 색 */
    "treeview { background-color: #252526; color: #d4d4d4; }"
    "treeview:hover { background-color: #2a2d2e; }"
    "treeview:selected { background-color: #094771; color: #ffffff; }"

    /* 입력창 / 콘솔 */
    "entry, textview text { background-color: #3c3c3c; color: #d4d4d4; caret-color: #ffffff; }"
    "entry:focus { border-color: #0e639c; }"
    "textview text selection { background-color: #264f78; color: #ffffff; }"

    /* 스크롤바 — 가늘고 어둡게 */
    "scrollbar { background-color: #1e1e1e; }"
    "scrollbar slider { background-color: #4a4a4a; border-radius: 3px; min-width: 8px; min-height: 8px; }"
    "scrollbar slider:hover { background-color: #5a5a5a; }"

    /* 패널 구분선 */
    "paned separator { background-color: #1b1b1c; min-width: 2px; min-height: 2px; }"

    /* 시작 화면(웰컴 페이지) — 큰 제목/링크용 */
    ".welcome-title { color: #ffffff; font-size: 26px; font-weight: bold; opacity: 0.95; }"
    ".welcome-sub { color: #9d9d9d; font-size: 13px; }"
    ".welcome-section { color: #cccccc; font-size: 13px; font-weight: bold; }";

void theme_apply(void) {
    /* GTK 자체 테마에도 다크 선호를 알려 메뉴/대화상자의 기본 위젯이 어둡게 그려지게 한다 */
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GdkScreen *screen = gdk_screen_get_default();
    if (!screen) return;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, DARK_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
