"""파일 탐색기 패널 — 폴더를 트리로 보여주고 더블클릭으로 파일을 연다.

default.py 의 build_explorer / populate_tree 로직을 그대로 이식.
"""
import os
import tkinter as tk
from tkinter import ttk, filedialog

TEXT_EXTENSIONS = {
    ".py", ".txt", ".md", ".json", ".csv", ".log", ".html", ".css",
    ".js", ".xml", ".yaml", ".yml", ".ini", ".c", ".cpp", ".java",
}


class Explorer(tk.Frame):
    """좌측 파일 탐색기.

    콜백:
      on_open_file(path)  — 파일 더블클릭 시 (앱이 에디터로 로드)
      on_folder_opened(folder) — 폴더가 열릴 때 (앱이 터미널 작업 폴더 갱신)
    """

    def __init__(self, master, on_open_file, on_folder_opened=None, **kwargs):
        super().__init__(master, bg="#252526", **kwargs)
        self.on_open_file = on_open_file
        self.on_folder_opened = on_folder_opened
        self.project_dir = None

        header = tk.Label(self, text="EXPLORER", font=("Consolas", 9, "bold"),
                          bg="#252526", fg="#bbbbbb", anchor="w", padx=10, pady=6)
        header.pack(fill=tk.X)

        open_btn = tk.Button(self, text="📂 폴더 열기", command=self.open_folder,
                             bg="#333333", fg="#ffffff", relief=tk.FLAT,
                             font=("Consolas", 9), cursor="hand2")
        open_btn.pack(fill=tk.X, padx=8, pady=(0, 6))

        style = ttk.Style()
        style.theme_use("default")
        style.configure("Explorer.Treeview", background="#252526", fieldbackground="#252526",
                        foreground="#d4d4d4", borderwidth=0, font=("Consolas", 10))
        style.map("Explorer.Treeview", background=[("selected", "#094771")])

        self.tree = ttk.Treeview(self, style="Explorer.Treeview", show="tree")
        self.tree.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.tree.bind("<Double-1>", self._on_tree_double_click)
        self.tree.bind("<<TreeviewOpen>>", self._on_tree_expand)

    def open_folder(self):
        folder = filedialog.askdirectory(title="폴더 선택")
        if not folder:
            return
        self.project_dir = folder
        self.tree.delete(*self.tree.get_children())
        root_node = self.tree.insert("", "end", text=f"📁 {os.path.basename(folder)}",
                                     values=[folder], open=True)
        self._populate(root_node, folder)
        if self.on_folder_opened:
            self.on_folder_opened(folder)

    def _populate(self, parent_node, path):
        try:
            entries = sorted(os.listdir(path),
                             key=lambda n: (not os.path.isdir(os.path.join(path, n)), n.lower()))
        except Exception:
            return
        for name in entries:
            if name.startswith("."):
                continue
            full_path = os.path.join(path, name)
            if os.path.isdir(full_path):
                node = self.tree.insert(parent_node, "end", text=f"📁 {name}", values=[full_path])
                self.tree.insert(node, "end", text="")  # 더미 항목 (지연 로딩용)
            else:
                self.tree.insert(parent_node, "end", text=f"📄 {name}", values=[full_path])

    def _on_tree_expand(self, _event):
        node = self.tree.focus()
        children = self.tree.get_children(node)
        # 더미 항목만 있는 경우 실제 내용을 로드
        if len(children) == 1 and self.tree.item(children[0], "text") == "":
            self.tree.delete(children[0])
            path = self.tree.item(node, "values")[0]
            self._populate(node, path)

    def _on_tree_double_click(self, _event):
        node = self.tree.focus()
        values = self.tree.item(node, "values")
        if not values:
            return
        path = values[0]
        if os.path.isfile(path) and self.on_open_file:
            self.on_open_file(path)
