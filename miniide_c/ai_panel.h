/* AI 어시스턴트 패널 — 로컬 Ollama와 대화 + 코드 적용.
 * 파이썬 버전(miniide/ai_panel.py)의 AIPanel 을 C로 이식.
 * 단일 모델(DeepSeek)로 모든 요청을 처리하고, 스킬 메뉴(ai_skills)로
 * 코딩/기획/학습/자동화 프롬프트를 골라 쓴다.
 */
#ifndef AI_PANEL_H
#define AI_PANEL_H

#include <gtk/gtk.h>
#include "ai_client.h"

typedef char *(*AiPanelGetCodeCb)(void *user_data);
typedef void (*AiPanelApplyCodeCb)(const char *code, void *user_data);

typedef struct {
    GtkWidget *box;
    GtkTextView *display;
    GtkEntry *entry;
    GtkLabel *status;
    GtkButton *send_btn;

    /* 언어 전환 시 문구를 다시 쓰기 위해 보관하는 위젯들 */
    GtkWidget *header;
    GtkWidget *skill_btn;   /* 스킬 메뉴 버튼 (분야별 하위 메뉴) */
    GtkButton *apply_btn;
    GtkButton *model_btn;   /* 모델 설정 대화상자 열기 */

    AiClient client;
    GPtrArray *messages;     /* ChatMessage* 배열 — Ollama 로 보낼 대화 기록 */
    GString *last_bot_response;
    gboolean is_requesting;

    /* 사용 모델 — 단일 모델(DeepSeek). 시작 시 서버에 설치되어 있는지 확인한다 */
    char model[AI_MAX_MODEL_LEN];

    AiPanelGetCodeCb get_code;
    void *get_code_data;
    AiPanelApplyCodeCb apply_code;
    void *apply_code_data;
} AiPanel;

AiPanel *ai_panel_new(AiPanelGetCodeCb get_code, void *get_code_data,
                       AiPanelApplyCodeCb apply_code, void *apply_code_data);

/* 언어 전환 시 정적 문구(제목/버튼)를 현재 언어로 다시 쓰고 상태를 재확인한다 */
void ai_panel_refresh_language(AiPanel *panel);

#endif
