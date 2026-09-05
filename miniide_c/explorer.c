#include "explorer.h"
#include "ui_lang.h"

#include <string.h>

enum { COL_NAME, COL_PATH, COL_IS_DIR, N_COLS };

typedef struct {
    Explorer *explorer;
    GtkWindow *parent_window;
} ExplorerCtx;

static void populate_dir(Explorer *explorer, GtkTreeIter *parent, const char *path);

/* 지연 로딩용 더미 자식이 있는지 확인 (이름이 빈 문자열인 자식 1개뿐이면 더미) */
static gboolean has_only_dummy_child(GtkTreeStore *store, GtkTreeIter *node) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &child, node)) return FALSE;
    if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), node) != 1) return FALSE;
    char *name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &child, COL_NAME, &name, -1);
    gboolean is_dummy = (name == NULL || name[0] == '\0');
    g_free(name);
    return is_dummy;
}

/* 폴더 내용을 트리에 채운다 — GDir/GLib 만 써서 Linux 와 Windows 에서 같이 동작.
 * (예전 dirent/stat 버전은 Linux 전용이었다) */
static void populate_dir(Explorer *explorer, GtkTreeIter *parent, const char *path) {
    GDir *dir = g_dir_open(path, 0, NULL);
    if (!dir) return;

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] == '.') continue;
        g_ptr_array_add(names, g_strdup(name));
    }
    g_dir_close(dir);

    /* 이름순 정렬 (디렉터리 먼저) — 한글 이름도 g_utf8_collate 로 자연스럽게 정렬 */
    GHashTable *is_dir_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < names->len; i++) {
        char *n = g_ptr_array_index(names, i);
        char *full = g_build_filename(path, n, NULL);
        g_hash_table_insert(is_dir_map, g_strdup(n),
                            GINT_TO_POINTER(g_file_test(full, G_FILE_TEST_IS_DIR) ? 1 : 0));
        g_free(full);
    }
    for (guint i = 0; i + 1 < names->len; i++) {
        for (guint j = i + 1; j < names->len; j++) {
            char *ni = g_ptr_array_index(names, i);
            char *nj = g_ptr_array_index(names, j);
            gboolean i_dir = GPOINTER_TO_INT(g_hash_table_lookup(is_dir_map, ni)) != 0;
            gboolean j_dir = GPOINTER_TO_INT(g_hash_table_lookup(is_dir_map, nj)) != 0;
            gboolean should_swap = FALSE;
            if (!i_dir && j_dir) should_swap = TRUE;
            else if (i_dir == j_dir && g_utf8_collate(ni, nj) > 0) should_swap = TRUE;
            if (should_swap) {
                g_ptr_array_index(names, i) = nj;
                g_ptr_array_index(names, j) = ni;
            }
        }
    }

    for (guint i = 0; i < names->len; i++) {
        char *n = g_ptr_array_index(names, i);
        char *full_path = g_build_filename(path, n, NULL);
        gboolean is_dir = GPOINTER_TO_INT(g_hash_table_lookup(is_dir_map, n)) != 0;

        GtkTreeIter node;
        gtk_tree_store_append(explorer->store, &node, parent);
        gtk_tree_store_set(explorer->store, &node,
                            COL_NAME, n, COL_PATH, full_path, COL_IS_DIR, is_dir, -1);
        if (is_dir) {
            /* 더미 자식 하나를 넣어 확장 화살표가 보이도록 하고, 실제 확장 시 로드 */
            GtkTreeIter dummy;
            gtk_tree_store_append(explorer->store, &dummy, &node);
            gtk_tree_store_set(explorer->store, &dummy, COL_NAME, "", COL_PATH, "", COL_IS_DIR, FALSE, -1);
        }
        g_free(full_path);
    }

    g_hash_table_destroy(is_dir_map);
    g_ptr_array_free(names, TRUE);
}

static void on_row_expanded(GtkTreeView *tree_view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    Explorer *explorer = (Explorer *)user_data;
    if (!has_only_dummy_child(explorer->store, iter)) return;

    GtkTreeIter dummy;
    gtk_tree_model_iter_children(GTK_TREE_MODEL(explorer->store), &dummy, iter);
    gtk_tree_store_remove(explorer->store, &dummy);

    char *node_path = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(explorer->store), iter, COL_PATH, &node_path, -1);
    populate_dir(explorer, iter, node_path);
    g_free(node_path);
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                              GtkTreeViewColumn *column, gpointer user_data) {
    (void)column;
    Explorer *explorer = (Explorer *)user_data;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(explorer->store), &iter, path)) return;

    char *node_path = NULL;
    gboolean is_dir = FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(explorer->store), &iter, COL_PATH, &node_path, COL_IS_DIR, &is_dir, -1);

    if (!is_dir && node_path && node_path[0] != '\0' && explorer->on_open_file) {
        explorer->on_open_file(node_path, explorer->on_open_file_data);
    }
    g_free(node_path);
    (void)tree_view;
}

void explorer_open_folder_dialog(Explorer *explorer) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        tr(STR_FOLDER_SELECT), NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        tr(STR_BTN_CANCEL), GTK_RESPONSE_CANCEL, tr(STR_BTN_OPEN), GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_free(explorer->project_dir);
        explorer->project_dir = g_strdup(folder);

        gtk_tree_store_clear(explorer->store);
        char *base = g_path_get_basename(folder);

        GtkTreeIter root;
        gtk_tree_store_append(explorer->store, &root, NULL);
        gtk_tree_store_set(explorer->store, &root, COL_NAME, base, COL_PATH, folder, COL_IS_DIR, TRUE, -1);
        g_free(base);
        populate_dir(explorer, &root, folder);

        GtkTreePath *tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(explorer->store), &root);
        gtk_tree_view_expand_row(explorer->tree_view, tree_path, FALSE);
        gtk_tree_path_free(tree_path);

        if (explorer->on_folder_opened) {
            explorer->on_folder_opened(folder, explorer->on_folder_opened_data);
        }
        g_free(folder);
    }
    gtk_widget_destroy(dialog);
}

Explorer *explorer_new(GtkWindow *parent_window,
                        ExplorerOpenFileCb on_open_file, void *on_open_file_data,
                        ExplorerFolderOpenedCb on_folder_opened, void *on_folder_opened_data) {
    (void)parent_window;
    Explorer *explorer = g_new0(Explorer, 1);
    explorer->on_open_file = on_open_file;
    explorer->on_open_file_data = on_open_file_data;
    explorer->on_folder_opened = on_folder_opened;
    explorer->on_folder_opened_data = on_folder_opened_data;

    /* 사이드바에는 트리만 — 폴더 열기는 헤더바 버튼/Ctrl+Shift+O 가 담당한다 */
    explorer->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    explorer->store = gtk_tree_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    explorer->tree_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(explorer->store)));
    gtk_tree_view_set_headers_visible(explorer->tree_view, FALSE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(tr(STR_COL_NAME), renderer, "text", COL_NAME, NULL);
    gtk_tree_view_append_column(explorer->tree_view, column);

    g_signal_connect(explorer->tree_view, "row-expanded", G_CALLBACK(on_row_expanded), explorer);
    g_signal_connect(explorer->tree_view, "row-activated", G_CALLBACK(on_row_activated), explorer);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(explorer->tree_view));
    gtk_box_pack_start(GTK_BOX(explorer->box), scroll, TRUE, TRUE, 0);

    return explorer;
}

void explorer_refresh_language(Explorer *explorer) {
    /* 사이드바에는 트리만 있고 정적 문구가 없다 — 갱신할 것이 없다 */
    (void)explorer;
}
