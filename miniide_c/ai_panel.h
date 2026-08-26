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

    AiClient client;
    GPtrArray *messages;     /* ChatMessage* 배열 — Ollama 로 보낼 대화 기록 */
    GString *last_bot_response;
    gboolean is_requesting;

    AiPanelGetCodeCb get_code;
    void *get_code_data;
    AiPanelApplyCodeCb apply_code;
    void *apply_code_data;
} AiPanel;

AiPanel *ai_panel_new(AiPanelGetCodeCb get_code, void *get_code_data,
                       AiPanelApplyCodeCb apply_code, void *apply_code_data);

#endif
