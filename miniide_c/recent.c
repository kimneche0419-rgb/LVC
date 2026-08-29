#include "recent.h"

#include <stdio.h>

/* 저장 위치: <사용자 설정 폴더>/miniide/recent.txt — 한 줄에 경로 하나.
 * 언어 설정(lang.txt)과 같은 폴더를 쓴다. */
#define RECENT_MAX 10

static char *recent_file_path(void) {
    return g_build_filename(g_get_user_config_dir(), "miniide", "recent.txt", NULL);
}

/* 파일에서 목록 읽기 — 없어진 파일은 여기서 걸러낸다 */
static GPtrArray *recent_load(void) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    char *path = recent_file_path();
    gchar *content = NULL;
    if (g_file_get_contents(path, &content, NULL, NULL)) {
        gchar **lines = g_strsplit(content, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            if (lines[i][0] != '\0' && g_file_test(lines[i], G_FILE_TEST_EXISTS)) {
                g_ptr_array_add(arr, g_strdup(lines[i]));
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(path);
    return arr;
}

static void recent_save(GPtrArray *arr) {
    char *path = recent_file_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    GString *body = g_string_new("");
    for (guint i = 0; i < arr->len; i++) {
        g_string_append_printf(body, "%s\n", (char *)g_ptr_array_index(arr, i));
    }
    g_file_set_contents(path, body->str, -1, NULL);
    g_string_free(body, TRUE);
    g_free(dir);
    g_free(path);
}

void recent_add(const char *file_path) {
    GPtrArray *arr = recent_load();
    for (guint i = 0; i < arr->len; i++) {
        if (g_strcmp0((char *)g_ptr_array_index(arr, i), file_path) == 0) {
            g_ptr_array_remove_index(arr, i);  /* 중복이면 앞으로 옮기기 위해 제거 */
            break;
        }
    }
    g_ptr_array_insert(arr, 0, g_strdup(file_path));
    while (arr->len > RECENT_MAX) g_ptr_array_remove_index(arr, arr->len - 1);
    recent_save(arr);
    g_ptr_array_free(arr, TRUE);
}

char **recent_get(void) {
    GPtrArray *arr = recent_load();
    char **out = g_new0(char *, arr->len + 1);
    for (guint i = 0; i < arr->len; i++) {
        out[i] = g_strdup((char *)g_ptr_array_index(arr, i));
    }
    g_ptr_array_free(arr, TRUE);
    return out;
}
