#include "ui_lang.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* 문자열 테이블 — [id][언어]. UI_LANG_KO=0, UI_LANG_EN=1 순서.
 * 새 문구는 UiStrId 열거형(ui_lang.h)과 여기 테이블에 함께 추가한다. */
static const char *STRINGS[STR_COUNT][2] = {
    /* 공통 버튼 */
    [STR_BTN_CANCEL]  = {"취소", "Cancel"},
    [STR_BTN_SAVE]    = {"저장", "Save"},
    [STR_BTN_OPEN]    = {"열기", "Open"},

    /* main.c — 파일/탭 */
    [STR_UNTITLED]           = {"제목 없음", "Untitled"},
    [STR_NO_FILE_OPEN]       = {"  (열린 파일 없음)", "  (no file open)"},
    [STR_CONFIRM_DISCARD]    = {"저장하지 않은 변경사항이 있습니다. 닫으면 사라집니다. 계속할까요?",
                                "You have unsaved changes. They will be lost. Continue?"},
    [STR_SAVE_AS_TITLE]      = {"다른 이름으로 저장", "Save As"},
    [STR_SAVE_ERR_FMT]       = {"파일 저장 중 오류가 발생했습니다.\n%s",
                                "Error while saving the file.\n%s"},
    [STR_SAVED_FMT]          = {"저장 완료: %s", "Saved: %s"},
    [STR_UNSUPPORTED]        = {"이 형식은 편집을 지원하지 않습니다.",
                                "Editing this file type is not supported."},
    [STR_OPEN_ERR_FMT]       = {"파일을 여는 중 오류가 발생했습니다.\n%s",
                                "Error while opening the file.\n%s"},
    [STR_AI_APPLIED_CONSOLE] = {"AI가 제안한 코드가 에디터에 적용되었습니다. (Ctrl+S로 저장하세요)",
                                "AI-suggested code was applied to the editor. (Ctrl+S to save)"},
    [STR_SPAWN_ERR_FMT]      = {"명령 실행 실패: %s", "Failed to run command: %s"},

    /* main.c — 실행 */
    [STR_RUN_UNSUPPORTED] = {"실행 불가: .py 또는 .c 파일만 지원합니다.",
                             "Cannot run: only .py and .c files are supported."},
    [STR_EXIT_CODE_FMT]   = {"(종료 코드: %d)", "(exit code: %d)"},
    [STR_RUN_SAVE_FIRST]  = {"먼저 .py 또는 .c 파일을 저장한 뒤 실행해주세요.",
                             "Save a .py or .c file first, then run."},
    [STR_RUN_FMT]         = {"▶ 실행: %s", "▶ Run: %s"},
    [STR_PY_NOT_FOUND]    = {"python 을 찾을 수 없습니다. Python 설치 후 다시 시도해주세요.",
                             "python was not found. Install Python and try again."},

    /* main.c — 메뉴 */
    [STR_MENU_FILE]        = {"파일", "File"},
    [STR_MENU_NEW]         = {"새 파일 (Ctrl+N)", "New File (Ctrl+N)"},
    [STR_MENU_OPEN_FILE]   = {"파일 열기 (Ctrl+O)", "Open File (Ctrl+O)"},
    [STR_MENU_OPEN_FOLDER] = {"폴더 열기 (Ctrl+Shift+O)", "Open Folder (Ctrl+Shift+O)"},
    [STR_MENU_SAVE]        = {"파일 저장 (Ctrl+S)", "Save File (Ctrl+S)"},
    [STR_MENU_SAVE_AS]     = {"다른 이름으로 저장", "Save As"},
    [STR_MENU_CLOSE_TAB]   = {"탭 닫기 (Ctrl+W)", "Close Tab (Ctrl+W)"},
    [STR_MENU_QUIT]        = {"종료", "Quit"},
    [STR_MENU_RUN]         = {"실행", "Run"},
    [STR_MENU_RUN_FILE]    = {"파일 실행 (F5)", "Run File (F5)"},
    [STR_MENU_TERMINAL]    = {"터미널 열기 (Ctrl+`)", "Open Terminal (Ctrl+`)"},
    [STR_MENU_VIEW]        = {"보기", "View"},
    [STR_MENU_LANGUAGE]    = {"언어 / Language", "Language / 언어"},
    [STR_LANG_KO_LABEL]    = {"한국어", "Korean (한국어)"},
    [STR_LANG_EN_LABEL]    = {"English", "English"},
    [STR_WINDOW_TITLE]     = {"Mini IDE (C) — 로컬 AI (Ollama)",
                              "Mini IDE (C) — Local AI (Ollama)"},

    /* main.c — 툴바 (짧은 라벨) */
    [STR_TB_NEW]         = {"＋ 새 파일", "＋ New"},
    [STR_TB_OPEN_FILE]   = {"📄 파일 열기", "📄 Open File"},
    [STR_TB_OPEN_FOLDER] = {"📂 폴더 열기", "📂 Open Folder"},
    [STR_TB_SAVE]        = {"💾 저장", "💾 Save"},
    [STR_TB_RUN]         = {"▶ 실행", "▶ Run"},

    /* main.c — 파일 열기 대화상자 / 최근 파일 */
    [STR_FILE_SELECT]      = {"파일 선택", "Select File"},
    [STR_FILE_FILTER_NAME] = {"텍스트/코드 파일", "Text / code files"},
    [STR_MENU_RECENT]   = {"최근 파일", "Recent Files"},
    [STR_RECENT_EMPTY]  = {"(최근 파일 없음)", "(no recent files)"},

    /* main.c — 시작 화면(웰컴 페이지) */
    [STR_WELCOME_TAB]         = {"시작", "Start"},
    [STR_WELCOME_TITLE]       = {"Mini IDE", "Mini IDE"},
    [STR_WELCOME_SUB]         = {"아래에서 시작할 방법을 고르거나, 최근 파일을 열어보세요",
                                 "Pick a way to start, or open a recent file"},
    [STR_WELCOME_NEW]         = {"🆕  새 파일 만들기", "🆕  New File"},
    [STR_WELCOME_OPEN_FILE]   = {"📄  파일 열기", "📄  Open File"},
    [STR_WELCOME_OPEN_FOLDER] = {"📂  폴더 열기", "📂  Open Folder"},
    [STR_WELCOME_RECENT]      = {"최근 파일", "Recent Files"},

    /* ai_panel.c */
    [STR_AI_HEADER]        = {" 🤖 AI Assistant (로컬)", " 🤖 AI Assistant (local)"},
    [STR_AI_CONNECTING]    = {"연결 확인 중...", "Checking connection..."},
    [STR_AI_CONNECTED_FMT] = {"✅ Ollama 연결됨 (%s)", "✅ Ollama connected (%s)"},
    [STR_AI_MODELS_FMT]    = {"✅ Ollama — 질문 %s · 코딩 %s · 기획 %s",
                              "✅ Ollama — chat %s · code %s · plan %s"},
    [STR_AI_CONN_FAIL]     = {"⚠ Ollama 서버에 연결할 수 없음 — 'ollama serve' 실행 필요",
                              "⚠ Cannot reach the Ollama server — run 'ollama serve' first"},
    [STR_AI_CONN_FAIL_SHORT] = {"⚠ Ollama 연결 실패", "⚠ Ollama connection failed"},
    [STR_AI_BUSY]          = {"⚡ 생성 중... (로컬 모델은 느릴 수 있어요)",
                              "⚡ Generating... (local models can be slow)"},
    [STR_AI_ERROR_FMT]     = {"[오류] %s\nOllama 서버가 실행 중인지 확인해주세요 (ollama serve).\n\n",
                              "[Error] %s\nCheck that the Ollama server is running (ollama serve).\n\n"},
    [STR_AI_USER_TAG]      = {"🙋 사용자\n", "🙋 You\n"},
    [STR_AI_BOT_TAG]       = {"🤖 응답\n", "🤖 Response\n"},
    [STR_BTN_REVIEW]       = {"코드 리뷰", "Review Code"},
    [STR_BTN_FIX]          = {"버그 수정", "Fix Bugs"},
    [STR_BTN_PLAN]         = {"계획 세우기", "Make a Plan"},
    [STR_BTN_APPLY]        = {"⬇ 코드 적용", "⬇ Apply Code"},
    [STR_BTN_RUN]          = {"RUN", "RUN"},
    [STR_AI_NO_CODE]       = {"[안내] 에디터에 코드가 없습니다.\n\n",
                              "[Notice] There is no code in the editor.\n\n"},
    [STR_AI_PLAN_HINT]     = {"[안내] 계획을 세우려면 무엇을 만들지 입력창에 적거나, "
                              "에디터에 코드를 먼저 열어주세요.\n\n",
                              "[Notice] To make a plan, type what to build in the input box, "
                              "or open some code in the editor first.\n\n"},
    [STR_AI_NO_ANSWER]     = {"[안내] 적용할 AI 답변이 없습니다. 먼저 질문해보세요.\n\n",
                              "[Notice] There is no AI response to apply. Ask something first.\n\n"},
    [STR_AI_APPLY_CONFIRM] = {"AI가 제안한 코드로 현재 에디터 내용을 덮어씁니다.\n"
                              "저장하지 않은 원래 내용은 사라집니다. 계속할까요?",
                              "This overwrites the editor with the AI-suggested code.\n"
                              "Unsaved changes will be lost. Continue?"},
    [STR_AI_APPLIED_NOTICE] = {"[안내] AI 코드가 에디터에 적용되었습니다.\n\n",
                               "[Notice] AI code was applied to the editor.\n\n"},
    [STR_PROMPT_REVIEW]    = {"다음 코드를 리뷰하고 개선할 점을 알려줘:\n```\n%s\n```",
                              "Review the following code and suggest improvements:\n```\n%s\n```"},
    [STR_PROMPT_FIX]       = {"다음 코드에서 버그를 찾아 수정해줘:\n```\n%s\n```",
                              "Find and fix bugs in the following code:\n```\n%s\n```"},
    [STR_PROMPT_PLAN_TYPED] = {"다음 요청을 구현하기 위한 단계별 계획을 세워줘 "
                               "(코드는 아직 작성하지 말고 계획만): %s",
                               "Make a step-by-step plan to implement the following request "
                               "(plan only, no code yet): %s"},
    [STR_PROMPT_PLAN_CODE] = {"다음 코드를 개선하기 위한 단계별 계획을 세워줘 "
                              "(코드는 아직 작성하지 말고 계획만):\n```\n%s\n```",
                              "Make a step-by-step plan to improve the following code "
                              "(plan only, no code yet):\n```\n%s\n```"},

    /* explorer.c */
    [STR_BTN_OPEN_FOLDER] = {"📂 폴더 열기", "📂 Open Folder"},
    [STR_FOLDER_SELECT]   = {"폴더 선택", "Select Folder"},
    [STR_COL_NAME]        = {"이름", "Name"},
};

static UiLang current_lang = UI_LANG_KO;
static UiLangChangedCb changed_cb = NULL;
static void *changed_cb_data = NULL;

const char *tr(UiStrId id) {
    if (id < 0 || id >= STR_COUNT) return "(?)";
    return STRINGS[id][current_lang];
}

char *trf(UiStrId id, ...) {
    va_list ap;
    va_start(ap, id);
    char *out = g_strdup_vprintf(tr(id), ap);
    va_end(ap);
    return out;
}

UiLang ui_lang_get(void) { return current_lang; }

const char *ui_lang_name(UiLang lang) {
    return lang == UI_LANG_KO ? "한국어" : "English";
}

/* 언어 선택을 사용자 설정 폴더(<config>/miniide/lang.txt)에 저장 — 다음 실행에도 유지 */
static void lang_save(UiLang lang) {
    char *dir = g_build_filename(g_get_user_config_dir(), "miniide", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "lang.txt", NULL);
    char body[16];
    snprintf(body, sizeof(body), "%d\n", (int)lang);
    g_file_set_contents(path, body, -1, NULL);
    g_free(path);
    g_free(dir);
}

void ui_lang_set(UiLang lang) {
    if (lang == current_lang) return;
    current_lang = lang;
    lang_save(lang);
    if (changed_cb) changed_cb(changed_cb_data);
}

UiLang ui_lang_toggle(void) {
    ui_lang_set(current_lang == UI_LANG_KO ? UI_LANG_EN : UI_LANG_KO);
    return current_lang;
}

void ui_lang_on_changed(UiLangChangedCb cb, void *user_data) {
    changed_cb = cb;
    changed_cb_data = user_data;
}

void ui_lang_init(void) {
    char *path = g_build_filename(g_get_user_config_dir(), "miniide", "lang.txt", NULL);
    char *content = NULL;
    if (g_file_get_contents(path, &content, NULL, NULL)) {
        int v = atoi(content);
        if (v == UI_LANG_KO || v == UI_LANG_EN) current_lang = (UiLang)v;
        g_free(content);
    }
    g_free(path);
}
