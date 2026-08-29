#include "ai_client.h"

#include <curl/curl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* curl 이 데이터를 받을 때마다 호출되는 콜백에 넘겨줄 상태.
 * 서버가 NDJSON(줄바꿈으로 구분된 JSON)을 보내므로, 완성된 한 줄이 모일
 * 때까지 buf 에 이어붙였다가 줄바꿈이 나오면 그 줄만 파싱한다. */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    AiChunkCallback on_chunk;
    void *user_data;
} StreamState;

static void stream_state_append(StreamState *s, const char *data, size_t n) {
    if (s->len + n + 1 > s->cap) {
        size_t new_cap = (s->cap == 0) ? 1024 : s->cap * 2;
        while (new_cap < s->len + n + 1) new_cap *= 2;
        s->buf = realloc(s->buf, new_cap);
        s->cap = new_cap;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

/* JSON 문자열 값 안의 이스케이프(\", \\, \n, \t, \uXXXX 등)를 최소한으로 풀어서
 * out 에 담는다. 반환값은 실제로 쓴 바이트 수. */
static size_t json_unescape(const char *src, size_t src_len, char *out, size_t out_cap) {
    size_t oi = 0;
    for (size_t i = 0; i < src_len && oi + 1 < out_cap; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < src_len) {
            char next = src[i + 1];
            switch (next) {
                case 'n': out[oi++] = '\n'; i++; break;
                case 't': out[oi++] = '\t'; i++; break;
                case 'r': i++; break;  /* 캐리지리턴은 버림 */
                case '"': out[oi++] = '"'; i++; break;
                case '\\': out[oi++] = '\\'; i++; break;
                case '/': out[oi++] = '/'; i++; break;
                case 'u':
                    /* 유니코드 이스케이프는 건너뜀 (MVP: ASCII 범위만 정확히 지원) */
                    i += 5;
                    break;
                default: out[oi++] = next; i++; break;
            }
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    return oi;
}

/* 한 줄의 NDJSON(예: {"message":{"role":"assistant","content":"..."},"done":false})
 * 에서 message.content 문자열만 뽑아 콜백으로 넘긴다. */
static void handle_line(const char *line, size_t line_len, StreamState *s) {
    const char *key = "\"content\":\"";
    const char *found = NULL;
    /* line 이 널 종료되어 있지 않을 수 있으므로 memmem 대신 수동 탐색 */
    size_t key_len = strlen(key);
    for (size_t i = 0; i + key_len <= line_len; i++) {
        if (memcmp(line + i, key, key_len) == 0) {
            found = line + i + key_len;
            break;
        }
    }
    if (!found) return;

    const char *end = found;
    const char *line_end = line + line_len;
    while (end < line_end) {
        if (*end == '\\') {
            end += 2; /* 이스케이프된 문자는 건너뜀 */
            continue;
        }
        if (*end == '"') break;
        end++;
    }
    if (end >= line_end) return; /* 닫는 따옴표를 못 찾음: 손상된 줄 */

    size_t raw_len = (size_t)(end - found);
    char unescaped[4096];
    if (raw_len >= sizeof(unescaped)) raw_len = sizeof(unescaped) - 1;
    size_t out_len = json_unescape(found, raw_len, unescaped, sizeof(unescaped));
    if (out_len > 0 && s->on_chunk) {
        s->on_chunk(unescaped, s->user_data);
    }
}

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    StreamState *s = (StreamState *)userdata;
    size_t total = size * nmemb;
    stream_state_append(s, ptr, total);

    /* 버퍼 안에서 완성된 줄(개행 포함)들을 모두 처리하고, 남은 조각은 buf 에 유지 */
    char *start = s->buf;
    char *nl;
    while ((nl = memchr(start, '\n', (s->buf + s->len) - start)) != NULL) {
        size_t line_len = (size_t)(nl - start);
        if (line_len > 0) handle_line(start, line_len, s);
        start = nl + 1;
    }
    size_t remaining = (size_t)((s->buf + s->len) - start);
    memmove(s->buf, start, remaining);
    s->len = remaining;
    if (s->len < s->cap) s->buf[s->len] = '\0';

    return total;
}

void ai_client_init(AiClient *client, const char *base_url, const char *model) {
    const char *env_host = getenv("OLLAMA_HOST");
    const char *b = base_url ? base_url : (env_host ? env_host : AI_DEFAULT_BASE_URL);
    const char *m = model ? model : AI_DEFAULT_MODEL;
    snprintf(client->base_url, AI_MAX_URL_LEN, "%s", b);
    snprintf(client->model, AI_MAX_MODEL_LEN, "%s", m);
    /* 끝의 슬래시 제거 */
    size_t len = strlen(client->base_url);
    if (len > 0 && client->base_url[len - 1] == '/') {
        client->base_url[len - 1] = '\0';
    }
}

int ai_client_is_available(AiClient *client) {
    char *body = ai_client_tags_body(client);
    if (!body) return 0;
    g_free(body);
    return 1;
}

/* /api/tags 본문을 그대로 모으기만 하는 콜백용 상태 */
typedef struct {
    GString *body;
} TagsFetchState;

static size_t tags_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    TagsFetchState *s = (TagsFetchState *)userdata;
    g_string_append_len(s->body, ptr, size * nmemb);
    return size * nmemb;
}

char *ai_client_tags_body(AiClient *client) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char url[AI_MAX_URL_LEN + 32];
    snprintf(url, sizeof(url), "%s/api/tags", client->base_url);

    TagsFetchState state = {0};
    state.body = g_string_new("");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tags_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || status != 200) {
        g_string_free(state.body, TRUE);
        return NULL;
    }
    return g_string_free(state.body, FALSE);
}

int ai_client_body_has_model(const char *tags_body, const char *model) {
    if (!tags_body || !model) return 0;
    /* /api/tags 응답의 각 항목은 "name":"qwen2.5-coder:3b" 형태 —
     * JSON 파서 없이 해당 패턴이 있는지만 확인한다. */
    char needle[96];
    snprintf(needle, sizeof(needle), "\"name\":\"%s\"", model);
    return strstr(tags_body, needle) != NULL ? 1 : 0;
}

int ai_client_chat_stream(AiClient *client, const char *messages_json,
                           AiChunkCallback on_chunk, void *user_data,
                           char *error_out, size_t error_out_len) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error_out) snprintf(error_out, error_out_len, "curl 초기화 실패");
        return -1;
    }

    char url[AI_MAX_URL_LEN + 32];
    snprintf(url, sizeof(url), "%s/api/chat", client->base_url);

    StreamState state = {0};
    state.on_chunk = on_chunk;
    state.user_data = user_data;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, messages_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(state.buf);

    if (res != CURLE_OK) {
        if (error_out) {
            snprintf(error_out, error_out_len, "Ollama 호출 실패: %s", curl_easy_strerror(res));
        }
        return -1;
    }
    return 0;
}
