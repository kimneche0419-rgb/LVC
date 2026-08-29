/* Ollama 서버와 통신하는 클라이언트 (libcurl 기반).
 *
 * 파이썬 버전(miniide/ai_client.py)의 OllamaClient 를 C로 이식.
 * 표준 라이브러리만 쓴 파이썬과 달리, C에서는 HTTP 통신에 libcurl을 쓴다.
 */
#ifndef AI_CLIENT_H
#define AI_CLIENT_H

#include <stddef.h>

#define AI_DEFAULT_BASE_URL "http://localhost:11434"
#define AI_DEFAULT_MODEL "qwen2.5-coder:3b"
#define AI_MAX_URL_LEN 256
#define AI_MAX_MODEL_LEN 64

typedef struct {
    char base_url[AI_MAX_URL_LEN];
    char model[AI_MAX_MODEL_LEN];
} AiClient;

/* base_url / model 이 NULL 이면 기본값(또는 OLLAMA_HOST 환경변수)을 사용 */
void ai_client_init(AiClient *client, const char *base_url, const char *model);

/* 서버가 살아있는지 확인 (GET /api/tags). 살아있으면 1, 아니면 0 */
int ai_client_is_available(AiClient *client);

/* /api/tags 응답 본문 전체를 반환 (g_free 로 해제). 실패 시 NULL.
 * 역할별 모델 자동 선택에 쓴다 — 서버에 어떤 모델이 설치됐는지 목록이다. */
char *ai_client_tags_body(AiClient *client);

/* tags 본문에서 특정 모델이 설치되어 있는지 확인. 있으면 1 */
int ai_client_body_has_model(const char *tags_body, const char *model);

/* 스트리밍 응답의 각 조각(chunk)이 도착할 때마다 호출되는 콜백.
 * user_data 는 ai_client_chat_stream 호출 시 전달한 값 그대로 전달됨.
 */
typedef void (*AiChunkCallback)(const char *chunk, void *user_data);

/* messages_json 은 이미 만들어진 Ollama /api/chat 요청 바디(JSON 문자열).
 * 스트리밍 응답을 받아 chunk 단위로 on_chunk 콜백을 호출한다.
 * 성공 시 0, 실패 시 -1을 반환하고 error_out(버퍼)에 오류 메시지를 채운다.
 */
int ai_client_chat_stream(AiClient *client, const char *messages_json,
                           AiChunkCallback on_chunk, void *user_data,
                           char *error_out, size_t error_out_len);

#endif
