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
    [STR_AI_CONN_FAIL]     = {"⚠ Ollama 서버에 연결할 수 없음 — 'ollama serve' 실행 필요",
                              "⚠ Cannot reach the Ollama server — run 'ollama serve' first"},
    [STR_AI_CONN_FAIL_SHORT] = {"⚠ Ollama 연결 실패", "⚠ Ollama connection failed"},
    [STR_AI_BUSY]          = {"⚡ 생성 중... (로컬 모델은 느릴 수 있어요)",
                              "⚡ Generating... (local models can be slow)"},
    [STR_AI_ERROR_FMT]     = {"[오류] %s\nOllama 서버가 실행 중인지 확인해주세요 (ollama serve).\n\n",
                              "[Error] %s\nCheck that the Ollama server is running (ollama serve).\n\n"},
    [STR_AI_USER_TAG]      = {"🙋 사용자\n", "🙋 You\n"},
    [STR_AI_BOT_TAG]       = {"🤖 응답\n", "🤖 Response\n"},
    [STR_BTN_APPLY]        = {"⬇ 코드 적용", "⬇ Apply Code"},
    [STR_BTN_RUN]          = {"RUN", "RUN"},
    [STR_AI_NO_CODE]       = {"[안내] 에디터에 코드가 없습니다.\n\n",
                              "[Notice] There is no code in the editor.\n\n"},
    [STR_AI_NO_ANSWER]     = {"[안내] 적용할 AI 답변이 없습니다. 먼저 질문해보세요.\n\n",
                              "[Notice] There is no AI response to apply. Ask something first.\n\n"},
    [STR_AI_APPLY_CONFIRM] = {"AI가 제안한 코드로 현재 에디터 내용을 덮어씁니다.\n"
                              "저장하지 않은 원래 내용은 사라집니다. 계속할까요?",
                              "This overwrites the editor with the AI-suggested code.\n"
                              "Unsaved changes will be lost. Continue?"},
    [STR_AI_APPLIED_NOTICE] = {"[안내] AI 코드가 에디터에 적용되었습니다.\n\n",
                               "[Notice] AI code was applied to the editor.\n\n"},

    /* ai_panel.c — 모델 설정 */
    [STR_MODEL_BTN]          = {"⚙ 모델", "⚙ Models"},
    [STR_MODEL_DIALOG_TITLE] = {"모델 설정", "Model Settings"},

    /* ai_panel.c — 스킬 메뉴 (ai_skills) */
    [STR_SKILL_BTN] = {"🛠 스킬", "🛠 Skills"},
    [STR_SKILL_CAT_QUALITY] = {"코딩 품질", "Code Quality"},
    [STR_SKILL_CAT_PLAN]    = {"기획·계획", "Planning"},
    [STR_SKILL_CAT_LEARN]   = {"학습·설명", "Learn & Explain"},
    [STR_SKILL_CAT_TOOLS]   = {"자동화·도구", "Automation & Tools"},
    [STR_SKILL_REVIEW]   = {"코드 리뷰", "Code Review"},
    [STR_SKILL_FIX]      = {"버그 찾기·수정", "Find & Fix Bugs"},
    [STR_SKILL_REFACTOR] = {"리팩토링 제안", "Refactoring"},
    [STR_SKILL_PERF]     = {"성능 개선", "Performance"},
    [STR_SKILL_TEST]     = {"테스트 코드 작성", "Write Tests"},
    [STR_SKILL_IMPL_PLAN]       = {"구현 계획 세우기", "Implementation Plan"},
    [STR_SKILL_REQ]             = {"요구사항 정리", "Organize Requirements"},
    [STR_SKILL_STRUCT]          = {"파일 구조 설계", "Design File Structure"},
    [STR_SKILL_EXPLAIN_CODE]    = {"코드 설명", "Explain Code"},
    [STR_SKILL_EXPLAIN_CONCEPT] = {"개념 설명", "Explain Concept"},
    [STR_SKILL_REGEX] = {"정규식 생성", "Generate Regex"},
    [STR_SKILL_SHELL] = {"셸 스크립트 생성", "Generate Shell Script"},
    [STR_SKILL_NEED_TEXT] = {"[안내] 입력창에 요청을 먼저 적어주세요.\n\n",
                             "[Notice] Type your request in the input box first.\n\n"},

    [STR_PROMPT_SKILL_REVIEW] = {
        "다음 코드를 리뷰한다. 먼저 코드를 읽고 문제점을 모두 찾은 뒤, "
        "심각도가 높은 순서로 번호를 붙여 나열한다. "
        "각 항목은 (문제)→(왜 문제인지)→(고치는 방법) 순서로 설명한다:\n```\n%s\n```",
        "Review the following code. First read the code and find every issue, then list "
        "them numbered by severity. For each: (problem) → (why it is a problem) → "
        "(how to fix):\n```\n%s\n```"},
    [STR_PROMPT_SKILL_FIX] = {
        "다음 코드에서 버그를 찾아 수정한다. 단계: (1) 버그 후보를 모두 찾는다 "
        "(2) 각 버그의 원인을 한 줄로 설명한다 (3) 수정된 전체 코드를 ``` 블록으로 준다:\n```\n%s\n```",
        "Find and fix bugs in the following code. Steps: (1) find every bug candidate "
        "(2) explain each cause in one line (3) give the full corrected code in a "
        "``` block:\n```\n%s\n```"},
    [STR_PROMPT_SKILL_REFACTOR] = {
        "다음 코드를 리팩토링한다. 규칙: 동작은 그대로 유지하고 가독성만 높인다. "
        "단계: (1) 나쁜 점을 짧게 나열 (2) 리팩토링된 전체 코드를 ``` 블록으로 "
        "(3) 무엇이 어떻게 바뀌었는지 요약:\n```\n%s\n```",
        "Refactor the following code. Rule: keep behavior identical, improve readability "
        "only. Steps: (1) list the bad parts briefly (2) give the refactored full code in "
        "a ``` block (3) summarize what changed:\n```\n%s\n```"},
    [STR_PROMPT_SKILL_PERF] = {
        "다음 코드의 성능을 개선한다. 단계: (1) 느리거나 비효율적인 부분을 찾아 설명 "
        "(2) 개선된 전체 코드를 ``` 블록으로 (3) 어떤 이유로 빨라지는지 항목별로 설명:\n```\n%s\n```",
        "Improve the performance of the following code. Steps: (1) find and explain slow "
        "or wasteful parts (2) give the improved full code in a ``` block (3) explain why "
        "each change makes it faster:\n```\n%s\n```"},
    [STR_PROMPT_SKILL_TEST] = {
        "다음 코드에 대한 테스트 코드를 작성한다. 단계: (1) 테스트할 핵심 동작을 나열 "
        "(2) 정상 케이스와 경계 케이스를 모두 담은 테스트 코드를 ``` 블록으로 "
        "(3) 같은 언어의 표준 테스트 프레임워크를 쓴다:\n```\n%s\n```",
        "Write tests for the following code. Steps: (1) list the key behaviors to test "
        "(2) give tests covering both normal and edge cases in a ``` block (3) use the "
        "language's standard test framework:\n```\n%s\n```"},
    [STR_PROMPT_SKILL_IMPL_PLAN] = {
        "다음 요청을 구현하기 위한 단계별 계획을 세운다. 각 단계에는 "
        "(1) 무엇을 할지 (2) 어디를 고칠지 (파일·위치) (3) 완료 기준을 담는다. "
        "마지막에 리스크와 주의점을 3줄 이내로 덧붙인다. 코드는 아직 쓰지 않는다: %s",
        "Make a step-by-step plan to implement the following request. Each step must "
        "include (1) what to do (2) where (file/location) (3) the done-criteria. End with "
        "risks and notes in 3 lines or fewer. No code yet: %s"},
    [STR_PROMPT_SKILL_REQ] = {
        "다음 요청을 분석해 정리된 요구사항을 만든다. 형식: (1) 한 문장 요약 "
        "(2) 필수 기능 목록 (3) 있으면 좋은 기능 (4) 예외 상황·주의점. "
        "애매한 부분은 '확인 필요'로 표시한다: %s",
        "Analyze the following request and produce organized requirements. Format: "
        "(1) one-sentence summary (2) must-have features (3) nice-to-have features "
        "(4) edge cases and cautions. Mark ambiguous parts as 'needs clarification': %s"},
    [STR_PROMPT_SKILL_STRUCT] = {
        "다음 요청을 구현하는 프로그램의 파일 구조를 설계한다. 형식: (1) 파일 트리 "
        "(2) 각 파일의 역할 한 줄씩 (3) 파일 간 의존 관계. "
        "작은 프로젝트 규모로 유지한다: %s",
        "Design the file structure for a program implementing the following request. "
        "Format: (1) file tree (2) one line per file describing its role (3) dependencies "
        "between files. Keep it small-project sized: %s"},
    [STR_PROMPT_SKILL_EXPLAIN_CODE] = {
        "다음 코드를 설명한다. 형식: (1) 전체가 무엇을 하는 코드인지 한 문장 "
        "(2) 부분별로 어떻게 동작하는지 순서대로 (3) 초보자가 헷갈릴 만한 부분 짚기. "
        "쉬운 말로 쓴다:\n```\n%s\n```",
        "Explain the following code. Format: (1) one sentence on what it does overall "
        "(2) how each part works, in order (3) point out parts beginners find confusing. "
        "Use plain language:\n```\n%s\n```"},
    [STR_PROMPT_SKILL_EXPLAIN_CONCEPT] = {
        "다음 개념을 설명한다. 형식: (1) 한 문장 정의 (2) 일상 비유 (3) 간단한 예 "
        "(4) 자주 하는 실수. 쉬운 말로 쓴다: %s",
        "Explain the following concept. Format: (1) one-sentence definition (2) an "
        "everyday analogy (3) a simple example (4) common mistakes. Use plain "
        "language: %s"},
    [STR_PROMPT_SKILL_REGEX] = {
        "다음 요청을 만족하는 정규식을 만든다. 형식: (1) 정규식을 ``` 블록으로 "
        "(2) 부분별로 무엇을 뜻하는지 설명 (3) 매치되는 예와 매치되지 않는 예를 각각 3개씩: %s",
        "Create a regular expression satisfying the following request. Format: (1) the "
        "regex in a ``` block (2) explain what each part means (3) three matching and "
        "three non-matching examples: %s"},
    [STR_PROMPT_SKILL_SHELL] = {
        "다음 요청을 수행하는 셸 스크립트를 만든다. 형식: (1) 스크립트를 ``` 블록으로 "
        "(각 단계마다 주석) (2) 사용법 한 줄 (3) 위험하거나 되돌릴 수 없는 명령이 있으면 경고. "
        "Windows 환경이면 bash 대신 cmd나 PowerShell을 쓴다: %s",
        "Write a shell script that performs the following request. Format: (1) the script "
        "in a ``` block with a comment per step (2) one-line usage (3) warn about any "
        "dangerous or irreversible commands. On Windows use cmd or PowerShell instead of "
        "bash: %s"},

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
