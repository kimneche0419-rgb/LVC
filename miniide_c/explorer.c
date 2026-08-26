#include "explorer.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

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

static void populate_dir(Explorer *explorer, GtkTreeIter *parent, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entries[4096];
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < 4096) {
        if (entry->d_name[0] == '.') continue;
        entries[count++] = entry;
    }
    /* readdir 버퍼는 closedir 전까지만 유효하므로 이름을 복사해둔다 */
    char names[4096][256];
    for (int i = 0; i < count; i++) {
        g_strlcpy(names[i], entries[i]->d_name, sizeof(names[i]));
    }
    closedir(dir);

    /* 이름순 정렬 (디렉터리 먼저) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            char pi[1024], pj[1024];
            snprintf(pi, sizeof(pi), "%s/%s", path, names[i]);
            snprintf(pj, sizeof(pj), "%s/%s", path, names[j]);
            struct stat si, sj;
            stat(pi, &si);
            stat(pj, &sj);
            gboolean i_dir = S_ISDIR(si.st_mode);
            gboolean j_dir = S_ISDIR(sj.st_mode);
            gboolean should_swap = FALSE;
            if (!i_dir && j_dir) should_swap = TRUE;
            else if (i_dir == j_dir && strcasecmp(names[i], names[j]) > 0) should_swap = TRUE;
            if (should_swap) {
                char tmp[256];
                strcpy(tmp, names[i]);
                strcpy(names[i], names[j]);
                strcpy(names[j], tmp);
            }
        }
    }

    for (int i = 0; i < count; i++) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, names[i]);
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        gboolean is_dir = S_ISDIR(st.st_mode);

        char display[300];
        snprintf(display, sizeof(display), "%s %s", is_dir ? "\xf0\x9f\x93\x81" : "\xf0\x9f\x93\x84", names[i]);

        GtkTreeIter node;
        gtk_tree_store_append(explorer->store, &node, parent);
        gtk_tree_store_set(explorer->store, &node,
                            COL_NAME, display, COL_PATH, full_path, COL_IS_DIR, is_dir, -1);
        if (is_dir) {
            /* 더미 자식 하나를 넣어 확장 화살표가 보이도록 하고, 실제 확장 시 로드 */
            GtkTreeIter dummy;
            gtk_tree_store_append(explorer->store, &dummy, &node);
            gtk_tree_store_set(explorer->store, &dummy, COL_NAME, "", COL_PATH, "", COL_IS_DIR, FALSE, -1);
        }
    }
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
        "폴더 선택", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "취소", GTK_RESPONSE_CANCEL, "열기", GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_free(explorer->project_dir);
        explorer->project_dir = g_strdup(folder);

        gtk_tree_store_clear(explorer->store);
        char display[300];
        char *base = g_path_get_basename(folder);
        snprintf(display, sizeof(display), "\xf0\x9f\x93\x81 %s", base);
        g_free(base);

        GtkTreeIter root;
        gtk_tree_store_append(explorer->store, &root, NULL);
        gtk_tree_store_set(explorer->store, &root, COL_NAME, display, COL_PATH, folder, COL_IS_DIR, TRUE, -1);
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

static void on_open_folder_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    explorer_open_folder_dialog((Explorer *)user_data);
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

    explorer->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *open_btn = gtk_button_new_with_label("\xf0\x9f\x93\x82 폴더 열기");
    gtk_box_pack_start(GTK_BOX(explorer->box), open_btn, FALSE, FALSE, 4);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_folder_clicked), explorer);

    explorer->store = gtk_tree_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    explorer->tree_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(explorer->store)));
    gtk_tree_view_set_headers_visible(explorer->tree_view, FALSE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("이름", renderer, "text", COL_NAME, NULL);
    gtk_tree_view_append_column(explorer->tree_view, column);

    g_signal_connect(explorer->tree_view, "row-expanded", G_CALLBACK(on_row_expanded), explorer);
    g_signal_connect(explorer->tree_view, "row-activated", G_CALLBACK(on_row_activated), explorer);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(explorer->tree_view));
    gtk_box_pack_start(GTK_BOX(explorer->box), scroll, TRUE, TRUE, 0);

    return explorer;
}
