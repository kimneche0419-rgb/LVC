/* Windows 용 터미널 구현 — ConPTY(Windows 10 1809+ 공식 유사 터미널 API).
 * (Linux 용은 terminal.c — 같은 인터페이스를 구현한다)
 *
 * 구조:
 *   [입력] GtkTextView 키 입력/IME 조합 → 입력 파이프 → ConPTY → powershell
 *   [출력] powershell → ConPTY(VT 이스케이프 시퀀스로 변환) → 출력 파이프
 *          → 읽기 스레드 → g_idle_add → VT 파서 → GtkTextView 렌더링
 *
 * VT 처리는 컬러(SGR), 줄 지우기, 커서 이동, \r 덮어쓰기 등 자주 쓰이는
 * 것 위주로 구현했다 (vim 처럼 화면 전체를 쓰는 TUI 까지는 지원하지 않는다).
 * 한글 입력은 IME 조합이 끝난 텍스트를 UTF-8 그대로 파이프에 쓰고,
 * 시작 시 콘솔 코드페이지를 65001(UTF-8)로 맞춰 왕복 모두 UTF-8 로 흐르게 한다.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shlobj.h>

#include "terminal.h"
#include "ui_lang.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

struct Terminal {
    GtkWidget *box;
    GtkWidget *view;         /* GtkTextView — 터미널 화면 */
    GtkTextBuffer *buf;
    GtkTextMark *cursor;     /* VT 커서 위치(논리적) — \r/커서이동 으로 움직인다 */

    HPCON hpc;
    HANDLE in_write;         /* 우리 → ConPTY 입력 */
    HANDLE out_read;         /* ConPTY 출력 → 우리 */
    HANDLE hproc;            // powershell 프로세스 (자식 관리용)
    GThread *reader;
    volatile LONG alive;     /* 읽기 스레드 생존 플래그 */
};

/* ---------------- SGR 컬러 태그 ---------------- */

/* xterm 기본 8색 + 밝은 8색 (VSCode 팔레트 비슷하게) */
static const char *FG_COLORS[16] = {
    "#666d8a", "#f14c4c", "#23d18b", "#f5f643",
    "#3b8eea", "#d670d6", "#29b8db", "#e6e6e6",
    "#8a8fa8", "#ff6f6f", "#5af78e", "#fffa5a",
    "#6fb3ff", "#ff8bff", "#67e3f0", "#ffffff",
};

static void create_color_tags(Terminal *t) {
    GtkTextBuffer *buf = t->buf;
    gtk_text_buffer_create_tag(buf, "cstd", "foreground", "#d4d4d4", NULL);
    char name[16];
    for (int i = 0; i < 16; i++) {
        snprintf(name, sizeof(name), "c%d", i);
        gtk_text_buffer_create_tag(buf, name, "foreground", FG_COLORS[i], NULL);
    }
}

/* ---------------- 진단 로그 ---------------- */

/* 터미널 문제(검은 화면 등)의 원인 파악용 — <TEMP>\miniide-terminal.log 에 기록.
 * 항상 켜 두지만 한 줄짜리 사실만 적는다. 원인 확인 후에도 남겨둔다. */
static void term_log(const char *fmt, ...) {
    char *path = g_build_filename(g_get_tmp_dir(), "miniide-terminal.log", NULL);
    FILE *f = fopen(path, "a");
    g_free(path);
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

/* ---------------- 콘솔 입출력 ---------------- */

/* 입력 파이프에 UTF-8 바이트를 쓴다 (프로세스가 죽었으면 조용히 무시) */
static void conpty_write(Terminal *t, const char *data, gsize len) {
    if (!t->in_write) return;
    DWORD written = 0;
    WriteFile(t->in_write, data, (DWORD)len, &written, NULL);
}

typedef struct {
    Terminal *t;
    char *data;   /* g_strdup 로 복사한 출력 청크 (UTF-8, 시퀀스 중간에서 잘릴 수 있음) */
    gsize len;
} OutChunk;

/* ---------------- VT 파서 + 렌더러 (메인 스레드에서만 GTK 호출) ---------------- */

/* 현재 SGR 색 태그 이름 (t 에 없으면 매번 만들지 않도록 t->tag 를 쓰고 싶지만
 * 파서가 상태 없이 돌아가도록 파싱 컨텍스트를 묶었다) */
typedef struct {
    Terminal *t;
    const char *tag;         /* 현재 색 태그 */
    int pending_params[8];   /* CSI 파라미터 해석 중 */
    int n_params;
    int in_escape;           /* 0:평문 1:ESC 받음 2:CSI 파라미터 진행 */
} VtCtx;

/* 커서(마크) 위치에 한 글자 삽입 — 그 자리에 글이 있으면 덮어쓴다(\r 갱신용) */
static void vt_putc(VtCtx *ctx, gunichar ch) {
    GtkTextBuffer *buf = ctx->t->buf;
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_mark(buf, &at, ctx->t->cursor);

    /* gunichar → UTF-8 */
    char utf8[8];
    gint n = g_unichar_to_utf8(ch, utf8);

    /* 커서 오른쪽에 글자가 있으면 지우고(덮어쓰기), 줄 끝이면 그냥 삽입 */
    if (!gtk_text_iter_ends_line(&at)) {
        GtkTextIter next = at;
        gtk_text_iter_forward_char(&next);
        gtk_text_buffer_delete(buf, &at, &next);
        gtk_text_buffer_get_iter_at_mark(buf, &at, ctx->t->cursor);
    }
    gtk_text_buffer_insert_with_tags_by_name(buf, &at, utf8, n, ctx->tag, NULL);
}

static void vt_place_cursor_iter(Terminal *t, GtkTextIter *iter) {
    gtk_text_buffer_move_mark(t->buf, t->cursor, iter);
}

static void vt_cursor_line_start(Terminal *t) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    gtk_text_iter_set_line_offset(&it, 0);
    vt_place_cursor_iter(t, &it);
}

static void vt_cursor_next_line(Terminal *t) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    if (gtk_text_iter_ends_line(&it)) {
        gtk_text_buffer_insert(t->buf, &it, "\n", 1);
        gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    } else {
        gtk_text_iter_forward_line(&it);
    }
    gtk_text_iter_set_line_offset(&it, 0);
    vt_place_cursor_iter(t, &it);
}

static void vt_cursor_forward(Terminal *t, int n) {
    GtkTextIter it;
    for (int i = 0; i < n; i++) {
        gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
        if (gtk_text_iter_ends_line(&it)) {
            gtk_text_buffer_insert(t->buf, &it, " ", 1);
            gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
        }
        gtk_text_iter_forward_char(&it);
        vt_place_cursor_iter(t, &it);
    }
}

static void vt_cursor_back(Terminal *t, int n) {
    GtkTextIter it;
    for (int i = 0; i < n; i++) {
        gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
        if (gtk_text_iter_get_line_offset(&it) > 0) {
            gtk_text_iter_backward_char(&it);
            vt_place_cursor_iter(t, &it);
        }
    }
}

static void vt_cursor_up(Terminal *t, int n) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    for (int i = 0; i < n; i++) gtk_text_iter_backward_line(&it);
    gtk_text_iter_set_line_offset(&it, 0);
    vt_place_cursor_iter(t, &it);
}

static void vt_cursor_down(Terminal *t, int n) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    for (int i = 0; i < n; i++) gtk_text_iter_forward_line(&it);
    gtk_text_iter_set_line_offset(&it, 0);
    vt_place_cursor_iter(t, &it);
}

static int csi_param(VtCtx *ctx, int idx, int def) {
    if (idx < ctx->n_params && ctx->pending_params[idx] > 0) return ctx->pending_params[idx];
    return def;
}

/* CSI 마지막 글자 처리: J(화면 지우기) K(줄 지우기) m(색) A-D(커서 이동) … */
static void vt_csi_final(VtCtx *ctx, char final) {
    Terminal *t = ctx->t;
    switch (final) {
        case 'J': {
            GtkTextIter from, to;
            gtk_text_buffer_get_iter_at_mark(t->buf, &from, t->cursor);
            gtk_text_buffer_get_end_iter(t->buf, &to);
            if (ctx->n_params == 1 && ctx->pending_params[0] == 2) {
                /* 2J: 화면 전체 지우기 */
                gtk_text_buffer_get_start_iter(t->buf, &from);
            }
            gtk_text_buffer_delete(t->buf, &from, &to);
            break;
        }
        case 'K': {
            int mode = ctx->n_params ? ctx->pending_params[0] : 0;
            GtkTextIter from, to;
            gtk_text_buffer_get_iter_at_mark(t->buf, &from, t->cursor);
            to = from;
            if (mode == 0) {                       /* 커서 → 줄 끝 */
                if (!gtk_text_iter_ends_line(&to)) gtk_text_iter_forward_to_line_end(&to);
            } else if (mode == 1) {                /* 줄 시작 → 커서 */
                gtk_text_iter_set_line_offset(&from, 0);
            } else {                               /* 줄 전체 */
                gtk_text_iter_set_line_offset(&from, 0);
                if (!gtk_text_iter_ends_line(&to)) gtk_text_iter_forward_to_line_end(&to);
            }
            gtk_text_buffer_delete(t->buf, &from, &to);
            break;
        }
        case 'm': {
            int color = -1;
            for (int i = 0; i < ctx->n_params || i == 0; i++) {
                int p = csi_param(ctx, i, 0);
                if (p == 0) ctx->tag = "cstd";
                else if (p >= 30 && p <= 37) color = p - 30;
                else if (p >= 90 && p <= 97) color = p - 90 + 8;
                else if (p == 39) ctx->tag = "cstd";
                if (i >= ctx->n_params) break;
            }
            if (color >= 0) {
                static char tagname[16];  /* 파서가 메인 스레드에서만 도므로 안전 */
                snprintf(tagname, sizeof(tagname), "c%d", color);
                ctx->tag = tagname;
            }
            break;
        }
        case 'A': vt_cursor_up(t, csi_param(ctx, 0, 1)); break;
        case 'B': vt_cursor_down(t, csi_param(ctx, 0, 1)); break;
        case 'C': vt_cursor_forward(t, csi_param(ctx, 0, 1)); break;
        case 'D': vt_cursor_back(t, csi_param(ctx, 0, 1)); break;
        default: break;  /* H(위치이동) h/l(모드) 등 — 무시 */
    }
}

/* 출력 청크 하나를 파싱해 화면에 반영 */
static gboolean out_chunk_idle(gpointer data) {
    OutChunk *oc = (OutChunk *)data;
    Terminal *t = oc->t;

    VtCtx ctx = { t, "cstd", {0}, 0, 0 };

    const char *p = oc->data;
    const char *end = p + oc->len;
    while (p < end) {
        if (ctx.in_escape == 0 && *p == '\x1b') {
            ctx.in_escape = 1;
            p++;
            continue;
        }
        if (ctx.in_escape == 1) {
            if (*p == '[') {
                ctx.in_escape = 2;
                ctx.n_params = 0;
                memset(ctx.pending_params, 0, sizeof(ctx.pending_params));
            } else {
                ctx.in_escape = 0;  /* ESC 단독 시퀀스는 무시 */
            }
            p++;
            continue;
        }
        if (ctx.in_escape == 2) {
            if (*p >= '0' && *p <= '9') {
                if (ctx.n_params == 0) ctx.n_params = 1;
                int i = ctx.n_params - 1;
                if (i < 8) ctx.pending_params[i] = ctx.pending_params[i] * 10 + (*p - '0');
            } else if (*p == ';') {
                if (ctx.n_params < 8) {
                    ctx.pending_params[ctx.n_params] = 0;
                    ctx.n_params++;
                }
            } else if (*p >= 0x40 && *p <= 0x7e) {
                vt_csi_final(&ctx, *p);
                ctx.in_escape = 0;
            } else if (*p != '?' && *p != ' ') {
                ctx.in_escape = 0;  /* 알 수 없는 시퀀스 — 버림 */
            }
            p++;
            continue;
        }

        /* 평문 처리 */
        if (*p == '\r') {
            vt_cursor_line_start(t);
        } else if (*p == '\n') {
            vt_cursor_next_line(t);
        } else if (*p == '\b') {
            vt_cursor_back(t, 1);
        } else if (*p == '\t') {
            vt_cursor_forward(t, 8);
        } else if ((unsigned char)*p >= 0x20) {
            const char *q = p;
            gunichar ch = g_utf8_get_char_validated(p, (gssize)(end - p));
            if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                /* 청크 경계에서 UTF-8 이 잘린 경우 — 다음 청크에서 이어지므로 멈춘다 */
                break;
            }
            vt_putc(&ctx, ch);
            p = q + g_unichar_to_utf8(ch, NULL);
            continue;
        }
        /* 그 외 제어문자는 무시 */
        p++;
    }

    /* 화면 맨 아래로 스크롤 */
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(t->buf, &it, t->cursor);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(t->view), &it, 0.0, FALSE, 0.0, 0.0);

    g_free(oc->data);
    g_free(oc);
    return G_SOURCE_REMOVE;
}

/* 읽기 스레드: ConPTY 출력 파이프를 계속 읽어 메인 스레드로 넘긴다.
 * 차단 ReadFile 대신 PeekNamedPipe 폴링 방식 — 스레드에서 차단 읽기를
 * 하면 ConPTY 출력을 놓치는 문제가 있어(mingw 환경에서 재현 확인)
 * 진단에서 검증된 Peek 방식을 쓴다. */
static gpointer reader_thread(gpointer data) {
    Terminal *t = (Terminal *)data;
    char buf[4096];
    DWORD n = 0;
    unsigned long total = 0;
    gboolean logged_first = FALSE;
    while (t->alive) {
        DWORD avail = 0;
        if (!PeekNamedPipe(t->out_read, NULL, 0, NULL, &avail, NULL)) {
            term_log("reader: peek fail %lu", GetLastError());
            break;
        }
        if (avail == 0) {
            Sleep(30);  /* 데이터 없으면 잠깐 쉬고 재시도 */
            continue;
        }
        if (!ReadFile(t->out_read, buf, sizeof(buf), &n, NULL) || n == 0) break;
        if (!logged_first) {
            char hex[80];
            int hl = 0;
            for (DWORD i = 0; i < n && i < 20 && hl < (int)sizeof(hex) - 4; i++) {
                hl += sprintf(hex + hl, "%02x ", (unsigned char)buf[i]);
            }
            term_log("reader: first chunk %lu bytes: %s", (unsigned long)n, hex);
            logged_first = TRUE;
        }
        total += n;
        OutChunk *oc = g_new0(OutChunk, 1);
        oc->t = t;
        oc->data = g_memdup2(buf, n);
        oc->len = n;
        g_idle_add(out_chunk_idle, oc);
    }
    term_log("reader: exit (alive=%d, total=%lu)", (int)t->alive, total);
    InterlockedExchange(&t->alive, 0);
    return NULL;
}

/* ---------------- 입력 처리 ---------------- */

/* IME 조합이 끝난 텍스트가 버퍼에 삽입되려 할 때 가로채 파이프로 보낸다.
 * (한글 입력이 GTK 조합 창을 거쳐 정상 동작하도록 editable 을 유지한 채로,
 *  실제 버퍼 삽입은 막는다 — 화면에 뜨는 글자는 ConPTY 에서 다시 에코되어 온다) */
static void on_insert_text(GtkTextBuffer *buf, GtkTextIter *location, const char *text,
                           gint len, gpointer user_data) {
    (void)buf; (void)location; (void)len;
    Terminal *t = (Terminal *)user_data;
    if (len > 0) conpty_write(t, text, (gsize)len);
    g_signal_stop_emission_by_name(buf, "insert-text");
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    Terminal *t = (Terminal *)user_data;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;
    guint k = event->keyval;

    if (ctrl && !shift) {
        if (k >= GDK_KEY_a && k <= GDK_KEY_z) {
            char c = (char)(k - GDK_KEY_a + 1);   /* Ctrl+글자 → 제어문자 (Ctrl+C=3 등) */
            conpty_write(t, &c, 1);
            return TRUE;
        }
    }
    switch (k) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            conpty_write(t, "\r", 1);
            return TRUE;
        case GDK_KEY_BackSpace:
            conpty_write(t, "\x7f", 1);
            return TRUE;
        case GDK_KEY_Tab:
            conpty_write(t, "\t", 1);
            return TRUE;
        case GDK_KEY_Left:  conpty_write(t, "\x1b[D", 3); return TRUE;
        case GDK_KEY_Right: conpty_write(t, "\x1b[C", 3); return TRUE;
        case GDK_KEY_Up:    conpty_write(t, "\x1b[A", 3); return TRUE;
        case GDK_KEY_Down:  conpty_write(t, "\x1b[B", 3); return TRUE;
        case GDK_KEY_Home:  conpty_write(t, "\x1b[H", 3); return TRUE;
        case GDK_KEY_End:   conpty_write(t, "\x1b[F", 3); return TRUE;
        case GDK_KEY_Delete:  conpty_write(t, "\x1b[3~", 4); return TRUE;
        case GDK_KEY_Page_Up: conpty_write(t, "\x1b[5~", 4); return TRUE;
        case GDK_KEY_Page_Down: conpty_write(t, "\x1b[6~", 4); return TRUE;
        default: return FALSE;  /* 인쇄 가능한 글자/IME 는 insert-text 경유 */
    }
}

/* ---------------- ConPTY 프로세스 시작 ---------------- */

/* PowerShell 7(pwsh.exe)의 전체 경로를 찾는다. 못 찾으면 NULL.
 * 1) PATH 검색 — 단 WindowsApps 실행 별칭은 제외. MSIX 별칭은 별도
 *    프로세스를 다시 띄우는 방식이라 ConPTY 연결이 따라가지 못해
 *    검은 화면이 된다.
 * 2) MSI 설치 경로(Program Files), 3) ZIP 휴대용 설치 경로(관리자 권한 불필요). */
static const wchar_t *find_pwsh(wchar_t *buf, size_t buflen) {
    wchar_t found[MAX_PATH];
    if (SearchPathW(NULL, L"pwsh.exe", NULL, MAX_PATH, found, NULL) != 0 &&
        wcsstr(found, L"WindowsApps") == NULL) {
        wcscpy(buf, found);
        return buf;
    }
    PWSTR base = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_ProgramFilesX64, 0, NULL, &base))) {
        swprintf(buf, buflen, L"%ls\\PowerShell\\7\\pwsh.exe", base);
        CoTaskMemFree(base);
        if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return buf;
    }
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &base))) {
        swprintf(buf, buflen, L"%ls\\Programs\\PowerShell\\7\\pwsh.exe", base);
        CoTaskMemFree(base);
        if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return buf;
    }
    return NULL;
}

static BOOL conpty_spawn(Terminal *t, const char *cwd) {
    HANDLE in_read = NULL, out_write = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    if (!CreatePipe(&in_read, &t->in_write, &sa, 0) ||
        !CreatePipe(&t->out_read, &out_write, &sa, 0)) {
        return FALSE;
    }

    COORD size = { 120, 30 };
    HRESULT hr = CreatePseudoConsole(size, in_read, out_write, 0, &t->hpc);
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (FAILED(hr)) {
        term_log("CreatePseudoConsole failed: hr=0x%08lx", (unsigned long)hr);
        return FALSE;
    }

    STARTUPINFOEXW si = {0};
    si.StartupInfo.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    si.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attr_size);
    BOOL ok_init2 = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
    BOOL ok_upd = UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                            t->hpc, sizeof(t->hpc), NULL, NULL);
    term_log("attr: init2=%d(%lu) update=%d(%lu) attrval=0x%p macro=0x%lx sizeof_hpc=%zu",
             (int)ok_init2, ok_init2 ? 0ul : GetLastError(),
             (int)ok_upd, ok_upd ? 0ul : GetLastError(),
             (void *)t->hpc, (unsigned long)PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
             sizeof(t->hpc));

    /* PowerShell 7(pwsh)이 설치되어 있으면 그것을 쓰고,
     * 없으면 Windows 에 기본 내장된 powershell.exe 로 떨어진다.
     * -NoExit -Command: 시작하자마자 코드페이지를 UTF-8(65001) 로 바꾼다.
     * 이래야 한글 입출력이 왕복 모두 UTF-8 로 흐른다. (pwsh 7은 기본이 UTF-8이라
     * chcp 이 무해하다) 전체 경로에 공백이 있을 수 있어 겹따옴표로 감싼다. */
    wchar_t pwsh_path[MAX_PATH];
    const wchar_t *found = find_pwsh(pwsh_path, MAX_PATH);
    wchar_t cmdline[512];

    /* 진단용: MINIIDE_TERM_SHELL=cmd|powershell|pwshplain 로 셸을 강제 지정 */
    const char *env_shell = getenv("MINIIDE_TERM_SHELL");
    if (env_shell && g_strcmp0(env_shell, "cmd") == 0) {
        swprintf(cmdline, 512, L"cmd.exe");
    } else if (env_shell && g_strcmp0(env_shell, "powershell") == 0) {
        swprintf(cmdline, 512, L"powershell.exe -NoExit -Command \"chcp.com 65001 > $null\"");
    } else if (env_shell && g_strcmp0(env_shell, "pwshplain") == 0) {
        /* chcp 없이 순수 pwsh — chcp 충돌 여부 확인용 */
        if (found) swprintf(cmdline, 512, L"\"%ls\" -NoLogo -NoExit", found);
        else swprintf(cmdline, 512, L"powershell.exe -NoLogo -NoExit");
    } else if (found) {
        swprintf(cmdline, 512, L"\"%ls\" -NoExit -Command \"chcp.com 65001 > $null\"", found);
    } else {
        swprintf(cmdline, 512, L"powershell.exe -NoExit -Command \"chcp.com 65001 > $null\"");
    }
    wchar_t wcwd[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, cwd, -1, wcwd, MAX_PATH);

    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT,
                             NULL, wcwd, &si.StartupInfo, &pi);
    term_log("spawn: cmdline=%ls ok=%d last_error=%lu", cmdline,
             (int)ok, ok ? 0ul : GetLastError());

    /* 진단: 속성 리스트 즉시 해제가 ConPTY 연결을 끊는 의심 → 해제 보류 */
    /* DeleteProcThreadAttributeList(si.lpAttributeList);
       HeapFree(GetProcessHeap(), 0, si.lpAttributeList); */
    term_log("attrlist kept alive (diagnostic)");

    if (!ok) return FALSE;
    t->hproc = pi.hProcess;
    CloseHandle(pi.hThread);
    return TRUE;
}

/* 진단: 3초 뒤 자식 셸이 살아있는지, 죽었다면 종료 코드는 무엇인지 로그 */
static gboolean diag_child_alive(gpointer data) {
    Terminal *t = (Terminal *)data;
    if (!t->hproc) return G_SOURCE_REMOVE;
    DWORD code = 0;
    if (WaitForSingleObject(t->hproc, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(t->hproc, &code)) {
        term_log("child: EXITED code=%lu", (unsigned long)code);
    } else {
        term_log("child: still running");
    }
    return G_SOURCE_REMOVE;
}

/* ---------------- 공용 인터페이스 ---------------- */

Terminal *terminal_new(void) {
    Terminal *t = g_new0(Terminal, 1);
    t->alive = 1;
    char *cwd = g_get_current_dir();

    t->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    t->view = gtk_text_view_new();
    t->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(t->view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(t->view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(t->view), 4);

    /* 터미널 색 스타일 */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview { background-color: #0c0c0c; color: #d4d4d4; font-family: monospace; }"
        "textview text { background-color: #0c0c0c; color: #d4d4d4; caret-color: #d4d4d4; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(t->view),
                                    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    create_color_tags(t);

    /* 논리 커서 마크 — 생성 직후 버퍼 시작에 둔다 */
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(t->buf, &start);
    t->cursor = gtk_text_buffer_create_mark(t->buf, "vtcur", &start, TRUE);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), t->view);
    gtk_box_pack_start(GTK_BOX(t->box), scroll, TRUE, TRUE, 0);

    if (conpty_spawn(t, cwd)) {
        g_signal_connect(t->buf, "insert-text", G_CALLBACK(on_insert_text), t);
        g_signal_connect(t->view, "key-press-event", G_CALLBACK(on_key_press), t);
        g_timeout_add(3000, diag_child_alive, t);
        t->reader = g_thread_new("conpty-read", reader_thread, t);
    } else {
        /* ConPTY 실패(구형 Windows 등) — 안내문만 표시 */
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(t->buf, &end);
        gtk_text_buffer_insert(t->buf, &end,
            "ConPTY 를 시작할 수 없습니다 (Windows 10 1809+ 필요).\n"
            "Cannot start ConPTY (Windows 10 1809+ required).\n", -1);
        t->alive = 0;
    }
    g_free(cwd);
    return t;
}

GtkWidget *terminal_get_box(Terminal *t) {
    return t->box;
}

void terminal_set_cwd(Terminal *terminal, const char *folder) {
    if (!terminal->alive) return;
    /* GTK 경로는 '/' 구분자 → Windows '\' 로 바꿔 cd 명령을 보낸다 */
    char *path = g_strdup(folder);
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    char *cmd = g_strdup_printf("cd '%s'\r", path);
    conpty_write(terminal, cmd, strlen(cmd));
    g_free(cmd);
    g_free(path);
}

void terminal_focus_input(Terminal *terminal) {
    gtk_widget_grab_focus(terminal->view);
}
