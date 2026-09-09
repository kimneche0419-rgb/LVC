/* 한/영 언어 전환 모듈 — 모든 UI 문구를 한 곳에서 관리한다.
 * 사용법: 소스 코드에 한국어 문자열을 직접 쓰지 않고 tr(STR_XXX) 로 받아 쓴다.
 * 언어는 ui_lang_set/toggle 로 바꾸고, 바뀌면 등록된 콜백(UI 갱신용)이 호출된다.
 * 선택은 사용자 설정 폴더의 miniide/lang.txt 에 저장되어 다음 실행에도 유지된다.
 */
#ifndef UI_LANG_H
#define UI_LANG_H

#include <glib.h>

typedef enum {
    UI_LANG_KO = 0,
    UI_LANG_EN = 1
} UiLang;

/* 문구 식별자 — ui_lang.c 의 문자열 테이블과 1:1 대응. */
typedef enum {
    /* 공통 버튼 */
    STR_BTN_CANCEL, STR_BTN_SAVE, STR_BTN_OPEN,

    /* main.c — 파일/탭 */
    STR_UNTITLED, STR_NO_FILE_OPEN, STR_CONFIRM_DISCARD,
    STR_SAVE_AS_TITLE, STR_SAVE_ERR_FMT, STR_SAVED_FMT,
    STR_UNSUPPORTED, STR_OPEN_ERR_FMT, STR_AI_APPLIED_CONSOLE,
    STR_SPAWN_ERR_FMT,

    /* main.c — 실행 */
    STR_RUN_UNSUPPORTED, STR_EXIT_CODE_FMT, STR_RUN_SAVE_FIRST,
    STR_RUN_FMT, STR_PY_NOT_FOUND,

    /* main.c — 메뉴 */
    STR_MENU_FILE, STR_MENU_NEW, STR_MENU_OPEN_FILE, STR_MENU_OPEN_FOLDER, STR_MENU_SAVE,
    STR_MENU_SAVE_AS, STR_MENU_CLOSE_TAB, STR_MENU_QUIT,
    STR_MENU_RUN, STR_MENU_RUN_FILE, STR_MENU_TERMINAL,
    STR_MENU_VIEW, STR_MENU_LANGUAGE, STR_LANG_KO_LABEL, STR_LANG_EN_LABEL,
    STR_WINDOW_TITLE,

    /* main.c — 툴바 (짧은 라벨) */
    STR_TB_NEW, STR_TB_OPEN_FILE, STR_TB_OPEN_FOLDER, STR_TB_SAVE, STR_TB_RUN,

    /* main.c — 파일 열기 대화상자 / 최근 파일 */
    STR_FILE_SELECT, STR_FILE_FILTER_NAME, STR_MENU_RECENT, STR_RECENT_EMPTY,

    /* main.c — 시작 화면(웰컴 페이지) */
    STR_WELCOME_TAB, STR_WELCOME_TITLE, STR_WELCOME_SUB,
    STR_WELCOME_NEW, STR_WELCOME_OPEN_FILE, STR_WELCOME_OPEN_FOLDER,
    STR_WELCOME_RECENT,

    /* ai_panel.c */
    STR_AI_HEADER, STR_AI_CONNECTING,
    STR_AI_CONN_FAIL, STR_AI_CONN_FAIL_SHORT, STR_AI_BUSY,
    STR_AI_ERROR_FMT, STR_AI_USER_TAG, STR_AI_BOT_TAG,
    STR_BTN_APPLY, STR_BTN_RUN,
    STR_AI_NO_CODE, STR_AI_NO_ANSWER,
    STR_AI_APPLY_CONFIRM, STR_AI_APPLIED_NOTICE,

    /* ai_panel.c — 모델 설정 */
    STR_MODEL_BTN, STR_MODEL_DIALOG_TITLE,

    /* ai_panel.c — 스킬 메뉴 (ai_skills) */
    STR_SKILL_BTN,
    STR_SKILL_CAT_QUALITY, STR_SKILL_CAT_PLAN, STR_SKILL_CAT_LEARN, STR_SKILL_CAT_TOOLS,
    STR_SKILL_REVIEW, STR_SKILL_FIX, STR_SKILL_REFACTOR, STR_SKILL_PERF, STR_SKILL_TEST,
    STR_SKILL_IMPL_PLAN, STR_SKILL_REQ, STR_SKILL_STRUCT,
    STR_SKILL_EXPLAIN_CODE, STR_SKILL_EXPLAIN_CONCEPT,
    STR_SKILL_REGEX, STR_SKILL_SHELL,
    STR_SKILL_NEED_TEXT,
    STR_PROMPT_SKILL_REVIEW, STR_PROMPT_SKILL_FIX, STR_PROMPT_SKILL_REFACTOR,
    STR_PROMPT_SKILL_PERF, STR_PROMPT_SKILL_TEST,
    STR_PROMPT_SKILL_IMPL_PLAN, STR_PROMPT_SKILL_REQ, STR_PROMPT_SKILL_STRUCT,
    STR_PROMPT_SKILL_EXPLAIN_CODE, STR_PROMPT_SKILL_EXPLAIN_CONCEPT,
    STR_PROMPT_SKILL_REGEX, STR_PROMPT_SKILL_SHELL,

    /* explorer.c */
    STR_BTN_OPEN_FOLDER, STR_FOLDER_SELECT, STR_COL_NAME,

    STR_COUNT
} UiStrId;

/* 현재 언어의 문구를 반환 (절대 수정하지 말 것) */
const char *tr(UiStrId id);

/* 현재 언어의 printf 형식 문자열로 새 버퍼를 만들어 반환 (g_free 로 해제) */
char *trf(UiStrId id, ...);

UiLang ui_lang_get(void);
void   ui_lang_set(UiLang lang);   /* 저장 + 변경 콜백 호출 */
UiLang ui_lang_toggle(void);
const char *ui_lang_name(UiLang lang);  /* 메뉴에 표시할 이름 ("한국어"/"English") */

/* 언어가 바뀔 때 호출될 콜백 — UI 문구 일괄 갱신용 */
typedef void (*UiLangChangedCb)(void *user_data);
void ui_lang_on_changed(UiLangChangedCb cb, void *user_data);

/* 시작 시 저장된 언어를 불러온다 (파일이 없으면 한국어) */
void ui_lang_init(void);

#endif
