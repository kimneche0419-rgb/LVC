/* 파일 탐색기 위젯 — GtkTreeView 기반.
 * 파이썬 버전(miniide/explorer.py)의 Explorer 를 C로 이식.
 */
#ifndef EXPLORER_H
#define EXPLORER_H

#include <gtk/gtk.h>

typedef void (*ExplorerOpenFileCb)(const char *path, void *user_data);
typedef void (*ExplorerFolderOpenedCb)(const char *folder, void *user_data);

typedef struct {
    GtkWidget *box;           /* paned 에 붙일 최상위 위젯 */
    GtkTreeView *tree_view;
    GtkTreeStore *store;
    char *project_dir;

    ExplorerOpenFileCb on_open_file;
    void *on_open_file_data;
    ExplorerFolderOpenedCb on_folder_opened;
    void *on_folder_opened_data;
} Explorer;

Explorer *explorer_new(GtkWindow *parent_window,
                        ExplorerOpenFileCb on_open_file, void *on_open_file_data,
                        ExplorerFolderOpenedCb on_folder_opened, void *on_folder_opened_data);

/* "폴더 열기" 버튼과 동일한 동작을 코드에서 직접 호출할 때 사용 */
void explorer_open_folder_dialog(Explorer *explorer);

#endif
