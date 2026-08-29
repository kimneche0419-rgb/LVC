/* AI 어시스턴트 패널 — 로컬 Ollama와 대화 + 코드 적용.
 * 파이썬 버전(miniide/ai_panel.py)의 AIPanel 을 C로 이식.
 * 2번 기능: 코드 리뷰 / 버그 수정 / 계획 세우기 로 AI 역할을 나눔.
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
    GtkButton *review_btn;
    GtkButton *fix_btn;
    GtkButton *plan_btn;
    GtkButton *apply_btn;

    AiClient client;
    GPtrArray *messages;     /* ChatMessage* 배열 — Ollama 로 보낼 대화 기록 */
    GString *last_bot_response;
    gboolean is_requesting;

    /* 역할별 모델 — 시작 시 서버에 설치된 모델을 확인해 자동으로 고른다 */
    char model_light[AI_MAX_MODEL_LEN];  /* 일반 질문: 가장 가벼운 모델 */
    char model_code[AI_MAX_MODEL_LEN];   /* 코드 리뷰/버그 수정: 코딩 모델 */
    char model_plan[AI_MAX_MODEL_LEN];   /* 계획 세우기: 기획(지시) 모델 */

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
