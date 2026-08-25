import os
import re
import sys
import threading
import subprocess
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, filedialog
from groq import Groq

# ─────────────────────────────────────────────────────────
# API 키 설정
# 보안을 위해 코드에 직접 키를 적지 않습니다.
# 환경변수 GROQ_API_KEY 를 사용하세요. (예: set GROQ_API_KEY=발급받은키)
# ─────────────────────────────────────────────────────────
GROQ_API_KEY = os.environ.get("GROQ_API_KEY", "")

CHAT_MODEL = "openai/gpt-oss-120b"      # 일반 대화용 모델
SEARCH_MODEL = "groq/compound"          # 실시간 웹 검색 에이전트 모델

# 간단한 파이썬 문법 강조용 키워드
PY_KEYWORDS = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield", "self",
}

TEXT_EXTENSIONS = {
    ".py", ".txt", ".md", ".json", ".csv", ".log", ".html", ".css",
    ".js", ".xml", ".yaml", ".yml", ".ini", ".c", ".cpp", ".java",
}


class LineNumberCanvas(tk.Canvas):
    """코드 에디터 좌측 줄번호를 그려주는 캔버스"""

    def __init__(self, master, text_widget, **kwargs):
        super().__init__(master, width=44, bg="#1e1e1e", highlightthickness=0, **kwargs)
        self.text_widget = text_widget

    def redraw(self, *_args):
        self.delete("all")
        i = self.text_widget.index("@0,0")
        while True:
            dline = self.text_widget.dlineinfo(i)
            if dline is None:
                break
            y = dline[1]
            line_num = str(i).split(".")[0]
            self.create_text(38, y, anchor="ne", text=line_num, fill="#5a5a5a", font=("Consolas", 10))
            i = self.text_widget.index(f"{i}+1line")


class MiniIDE:

    def __init__(self, root):
        self.root = root
        self.root.title("Mini IDE - Groq AI Assistant")
        self.root.geometry("1180x760")
        self.root.minsize(820, 560)
        self.root.configure(bg="#1e1e1e")

        self.client = None
        self.is_requesting = False
        self.current_mode = "chat"        # AI 패널 모드: chat / search
        self.current_file_path = None      # 현재 에디터에 열려있는 파일 경로
        self.project_dir = None            # 탐색기에 표시 중인 루트 폴더
        self.dirty = False                 # 저장되지 않은 변경사항 여부

        self.build_menu()
        self.build_layout()
        self.init_client()

    # ─────────────────────────────────────────────────────────
    # Groq 클라이언트 초기화
    # ─────────────────────────────────────────────────────────
    def init_client(self):
        if not GROQ_API_KEY:
            self.append_ai_message(
                "System",
                "API 키가 설정되어 있지 않습니다.\n"
                "환경변수 GROQ_API_KEY를 설정한 뒤 다시 실행해주세요.\n",
            )
            self.ai_status.config(text="⚠ API 키 없음")
            self.ai_send_btn.config(state=tk.DISABLED)
            return
        try:
            self.client = Groq(api_key=GROQ_API_KEY)
        except Exception as e:
            messagebox.showerror("오류", f"Groq 클라이언트 초기화 실패.\n{e}")
            self.ai_status.config(text="⚠ 초기화 실패")
            self.ai_send_btn.config(state=tk.DISABLED)

    # ─────────────────────────────────────────────────────────
    # 상단 메뉴바
    # ─────────────────────────────────────────────────────────
    def build_menu(self):
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="새 파일", command=self.new_file, accelerator="Ctrl+N")
        file_menu.add_command(label="폴더 열기", command=self.open_folder, accelerator="Ctrl+O")
        file_menu.add_command(label="파일 저장", command=self.save_file, accelerator="Ctrl+S")
        file_menu.add_command(label="다른 이름으로 저장", command=self.save_file_as)
        file_menu.add_separator()
        file_menu.add_command(label="종료", command=self.root.quit)
        menubar.add_cascade(label="파일", menu=file_menu)

        run_menu = tk.Menu(menubar, tearoff=0)
        run_menu.add_command(label="파이썬 파일 실행", command=self.run_python_file, accelerator="F5")
        run_menu.add_separator()
        run_menu.add_command(label="터미널 열기", command=self.focus_terminal, accelerator="Ctrl+`")
        menubar.add_cascade(label="실행", menu=run_menu)

        ai_menu = tk.Menu(menubar, tearoff=0)
        ai_menu.add_command(label="현재 코드 리뷰 요청", command=self.ask_ai_review_code)
        ai_menu.add_command(label="현재 코드 버그 수정 요청", command=self.ask_ai_fix_code)
        menubar.add_cascade(label="AI", menu=ai_menu)

        self.root.config(menu=menubar)

        # 단축키 바인딩
        self.root.bind("<Control-n>", lambda e: self.new_file())
        self.root.bind("<Control-o>", lambda e: self.open_folder())
        self.root.bind("<Control-s>", lambda e: self.save_file())
        self.root.bind("<F5>", lambda e: self.run_python_file())
        self.root.bind("<Control-grave>", lambda e: self.focus_terminal())

    # ─────────────────────────────────────────────────────────
    # 전체 레이아웃: [파일 탐색기 | 에디터+출력콘솔 | AI 어시스턴트]
    # ─────────────────────────────────────────────────────────
    def build_layout(self):
        paned = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg="#1e1e1e",
                                sashwidth=4, sashrelief=tk.FLAT)
        paned.pack(fill=tk.BOTH, expand=True)

        # [좌측] 파일 탐색기
        explorer_frame = tk.Frame(paned, bg="#252526", width=220)
        self.build_explorer(explorer_frame)
        paned.add(explorer_frame, minsize=160)

        # [중앙] 에디터 + 실행 콘솔
        center_frame = tk.Frame(paned, bg="#1e1e1e")
        self.build_editor(center_frame)
        paned.add(center_frame, minsize=400)

        # [우측] AI 어시스턴트 패널
        ai_frame = tk.Frame(paned, bg="#1e1e1e", width=360)
        self.build_ai_panel(ai_frame)
        paned.add(ai_frame, minsize=280)

    # ---------------- 파일 탐색기 ----------------
    def build_explorer(self, parent):
        header = tk.Label(parent, text="EXPLORER", font=("Consolas", 9, "bold"),
                           bg="#252526", fg="#bbbbbb", anchor="w", padx=10, pady=6)
        header.pack(fill=tk.X)

        open_btn = tk.Button(parent, text="📂 폴더 열기", command=self.open_folder,
                              bg="#333333", fg="#ffffff", relief=tk.FLAT, font=("Consolas", 9),
                              cursor="hand2")
        open_btn.pack(fill=tk.X, padx=8, pady=(0, 6))

        style = ttk.Style()
        style.theme_use("default")
        style.configure("Explorer.Treeview", background="#252526", fieldbackground="#252526",
                         foreground="#d4d4d4", borderwidth=0, font=("Consolas", 10))
        style.map("Explorer.Treeview", background=[("selected", "#094771")])

        self.tree = ttk.Treeview(parent, style="Explorer.Treeview", show="tree")
        self.tree.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.tree.bind("<Double-1>", self.on_tree_double_click)
        self.tree.bind("<<TreeviewOpen>>", self.on_tree_expand)

    def open_folder(self):
        folder = filedialog.askdirectory(title="폴더 선택")
        if not folder:
            return
        self.project_dir = folder
        self.tree.delete(*self.tree.get_children())
        root_node = self.tree.insert("", "end", text=f"📁 {os.path.basename(folder)}",
                                      values=[folder], open=True)
        self.populate_tree(root_node, folder)

        # 터미널이 이미 생성되어 있다면 작업 폴더를 새 프로젝트 폴더로 갱신
        if hasattr(self, "term_input"):
            self.term_cwd = folder
            self.term_prompt_label.config(text=self._term_prompt_text())
            self._term_println(f"작업 폴더가 변경되었습니다: {folder}", "out_text")

    def populate_tree(self, parent_node, path):
        try:
            entries = sorted(os.listdir(path), key=lambda n: (not os.path.isdir(os.path.join(path, n)), n.lower()))
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

    def on_tree_expand(self, _event):
        node = self.tree.focus()
        children = self.tree.get_children(node)
        # 더미 항목만 있는 경우 실제 내용을 로드
        if len(children) == 1 and self.tree.item(children[0], "text") == "":
            self.tree.delete(children[0])
            path = self.tree.item(node, "values")[0]
            self.populate_tree(node, path)

    def on_tree_double_click(self, _event):
        node = self.tree.focus()
        values = self.tree.item(node, "values")
        if not values:
            return
        path = values[0]
        if os.path.isfile(path):
            self.load_file_into_editor(path)

    # ---------------- 코드 에디터 ----------------
    def build_editor(self, parent):
        # 파일 경로 표시줄
        self.path_label = tk.Label(parent, text="  (열린 파일 없음)", font=("Consolas", 9),
                                    bg="#2d2d2d", fg="#cccccc", anchor="w", padx=8, pady=4)
        self.path_label.pack(fill=tk.X)

        editor_container = tk.Frame(parent, bg="#1e1e1e")
        editor_container.pack(fill=tk.BOTH, expand=True)

        self.editor = tk.Text(
            editor_container, wrap=tk.NONE, bg="#1e1e1e", fg="#d4d4d4",
            insertbackground="#ffffff", font=("Consolas", 12), relief=tk.FLAT,
            undo=True, padx=8, pady=6,
        )
        self.linenos = LineNumberCanvas(editor_container, self.editor)
        self.linenos.pack(side=tk.LEFT, fill=tk.Y)
        self.editor.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.editor.bind("<KeyRelease>", self.on_editor_change)
        self.editor.bind("<MouseWheel>", lambda e: self.root.after(1, self.linenos.redraw))
        self.editor.bind("<ButtonRelease>", lambda e: self.linenos.redraw())
        self.editor.bind("<Configure>", lambda e: self.linenos.redraw())

        # 문법 강조 태그
        self.editor.tag_config("keyword", foreground="#569cd6")
        self.editor.tag_config("string", foreground="#ce9178")
        self.editor.tag_config("comment", foreground="#6a9955")

        # 하단 패널: OUTPUT / TERMINAL 탭
        style = ttk.Style()
        style.configure("Bottom.TNotebook", background="#1e1e1e", borderwidth=0)
        style.configure("Bottom.TNotebook.Tab", background="#2d2d2d", foreground="#cccccc",
                         padding=[10, 4], font=("Consolas", 9))
        style.map("Bottom.TNotebook.Tab", background=[("selected", "#1e1e1e")],
                  foreground=[("selected", "#ffffff")])

        self.bottom_tabs = ttk.Notebook(parent, style="Bottom.TNotebook", height=200)
        self.bottom_tabs.pack(fill=tk.X, side=tk.BOTTOM)

        # OUTPUT 탭
        output_tab = tk.Frame(self.bottom_tabs, bg="#0c0c0c")
        self.console = scrolledtext.ScrolledText(
            output_tab, height=10, wrap=tk.WORD, state=tk.DISABLED, bg="#0c0c0c",
            fg="#d4d4d4", font=("Consolas", 10), relief=tk.FLAT, padx=8, pady=6,
        )
        self.console.pack(fill=tk.BOTH, expand=True)
        self.console.tag_config("err", foreground="#f44747")
        self.console.tag_config("ok", foreground="#4ec9b0")
        self.bottom_tabs.add(output_tab, text="OUTPUT")

        # TERMINAL 탭
        terminal_tab = tk.Frame(self.bottom_tabs, bg="#0c0c0c")
        self.build_terminal(terminal_tab)
        self.bottom_tabs.add(terminal_tab, text="TERMINAL")

    # ---------------- 통합 터미널 ----------------
    def build_terminal(self, parent):
        """명령어를 입력하면 subprocess로 실행하고 결과를 출력하는 간이 터미널"""
        self.term_cwd = self.project_dir or os.getcwd()

        # [수정] 입력창을 먼저 side=tk.BOTTOM 으로 고정 배치해야 함.
        # 기존 코드는 term_display(expand=True)를 먼저 pack해서 남은 공간을
        # 전부 차지해버렸고, 그 다음에 pack된 input_row(명령어 입력창)가
        # 밀려서 안 보이거나 클릭/입력이 안 되는 문제가 있었음.
        input_row = tk.Frame(parent, bg="#0c0c0c")
        input_row.pack(fill=tk.X, side=tk.BOTTOM, padx=6, pady=4)

        self.term_display = scrolledtext.ScrolledText(
            parent, wrap=tk.WORD, state=tk.DISABLED, bg="#0c0c0c", fg="#d4d4d4",
            font=("Consolas", 10), relief=tk.FLAT, padx=8, pady=6,
        )
        self.term_display.pack(fill=tk.BOTH, expand=True)
        self.term_display.tag_config("prompt", foreground="#4ec9b0", font=("Consolas", 10, "bold"))
        self.term_display.tag_config("cmd_text", foreground="#ffffff")
        self.term_display.tag_config("out_text", foreground="#d4d4d4")
        self.term_display.tag_config("err_text", foreground="#f44747")

        self.term_prompt_label = tk.Label(input_row, text=self._term_prompt_text(),
                                           font=("Consolas", 10, "bold"), bg="#0c0c0c", fg="#4ec9b0")
        self.term_prompt_label.pack(side=tk.LEFT)

        self.term_input = tk.Entry(input_row, font=("Consolas", 10), bg="#0c0c0c", fg="#ffffff",
                                    insertbackground="#ffffff", bd=0)
        self.term_input.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 0))
        self.term_input.bind("<Return>", lambda e: self.run_terminal_command())

        # 명령 히스토리 (위/아래 화살표로 탐색)
        self.term_history = []
        self.term_history_idx = -1
        self.term_input.bind("<Up>", self._term_history_up)
        self.term_input.bind("<Down>", self._term_history_down)

        self._term_println(f"미니 터미널이 시작되었습니다. 작업 폴더: {self.term_cwd}", "out_text")

    def _term_prompt_text(self):
        folder_name = os.path.basename(self.term_cwd) or self.term_cwd
        return f"{folder_name}>"

    def _term_println(self, text, tag="out_text"):
        self.term_display.config(state=tk.NORMAL)
        self.term_display.insert(tk.END, text + "\n", tag)
        self.term_display.see(tk.END)
        self.term_display.config(state=tk.DISABLED)

    def _term_history_up(self, _event):
        if not self.term_history:
            return
        self.term_history_idx = max(0, self.term_history_idx - 1)
        self.term_input.delete(0, tk.END)
        self.term_input.insert(0, self.term_history[self.term_history_idx])

    def _term_history_down(self, _event):
        if not self.term_history:
            return
        self.term_history_idx = min(len(self.term_history), self.term_history_idx + 1)
        self.term_input.delete(0, tk.END)
        if self.term_history_idx < len(self.term_history):
            self.term_input.insert(0, self.term_history[self.term_history_idx])

    def run_terminal_command(self):
        command = self.term_input.get().strip()
        if not command:
            return
        self.term_input.delete(0, tk.END)
        self.term_history.append(command)
        self.term_history_idx = len(self.term_history)

        self._term_println(f"{self._term_prompt_text()} {command}", "prompt")

        # cd 명령은 실제 프로세스가 아니라 파이썬에서 직접 작업 폴더를 변경 (매 실행이 독립 프로세스라 상태 유지 안 되기 때문)
        if command.startswith("cd ") or command == "cd":
            self._handle_cd(command)
            return

        if command in ("cls", "clear"):
            self.term_display.config(state=tk.NORMAL)
            self.term_display.delete("1.0", tk.END)
            self.term_display.config(state=tk.DISABLED)
            return

        threading.Thread(target=self._run_terminal_subprocess, args=(command,), daemon=True).start()

    def _handle_cd(self, command):
        target = command[3:].strip() if command != "cd" else ""
        if not target:
            self._term_println(self.term_cwd, "out_text")
            return
        new_path = target if os.path.isabs(target) else os.path.join(self.term_cwd, target)
        new_path = os.path.normpath(new_path)
        if os.path.isdir(new_path):
            self.term_cwd = new_path
            self.term_prompt_label.config(text=self._term_prompt_text())
        else:
            self._term_println(f"경로를 찾을 수 없습니다: {new_path}", "err_text")

    def _run_terminal_subprocess(self, command):
        try:
            result = subprocess.run(
                command, shell=True, cwd=self.term_cwd,
                capture_output=True, text=True, timeout=60,
            )
            if result.stdout:
                self.root.after(0, self._term_println, result.stdout.rstrip("\n"), "out_text")
            if result.stderr:
                self.root.after(0, self._term_println, result.stderr.rstrip("\n"), "err_text")
        except subprocess.TimeoutExpired:
            self.root.after(0, self._term_println, "명령 실행이 60초를 초과하여 중단되었습니다.", "err_text")
        except Exception as e:
            self.root.after(0, self._term_println, f"실행 오류: {e}", "err_text")

    def on_editor_change(self, _event=None):
        self.dirty = True
        self.update_title()
        self.linenos.redraw()
        self.highlight_syntax()

    def update_title(self):
        name = os.path.basename(self.current_file_path) if self.current_file_path else "제목 없음"
        mark = " ●" if self.dirty else ""
        self.path_label.config(text=f"  {self.current_file_path or name}{mark}")

    def highlight_syntax(self):
        """아주 단순한 파이썬 문법 강조 (키워드 / 문자열 / 주석)"""
        code = self.editor.get("1.0", tk.END)
        for tag in ("keyword", "string", "comment"):
            self.editor.tag_remove(tag, "1.0", tk.END)

        for match in re.finditer(r"\b[a-zA-Z_][a-zA-Z0-9_]*\b", code):
            word = match.group()
            if word in PY_KEYWORDS:
                start = f"1.0+{match.start()}c"
                end = f"1.0+{match.end()}c"
                self.editor.tag_add("keyword", start, end)

        for match in re.finditer(r"(\".*?\"|'.*?')", code):
            start = f"1.0+{match.start()}c"
            end = f"1.0+{match.end()}c"
            self.editor.tag_add("string", start, end)

        for match in re.finditer(r"#.*", code):
            start = f"1.0+{match.start()}c"
            end = f"1.0+{match.end()}c"
            self.editor.tag_add("comment", start, end)

    # ---------------- 파일 입출력 ----------------
    def new_file(self):
        if not self.confirm_discard_changes():
            return
        self.editor.delete("1.0", tk.END)
        self.current_file_path = None
        self.dirty = False
        self.update_title()
        self.highlight_syntax()

    def load_file_into_editor(self, path):
        ext = os.path.splitext(path)[1].lower()
        if ext not in TEXT_EXTENSIONS:
            messagebox.showwarning("미지원 형식", f"'{ext}' 형식은 편집을 지원하지 않습니다.")
            return
        if not self.confirm_discard_changes():
            return
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
        except Exception as e:
            messagebox.showerror("파일 열기 오류", f"파일을 여는 중 오류가 발생했습니다.\n{e}")
            return

        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", content)
        self.current_file_path = path
        self.dirty = False
        self.update_title()
        self.highlight_syntax()

    def save_file(self):
        if self.current_file_path is None:
            return self.save_file_as()
        try:
            with open(self.current_file_path, "w", encoding="utf-8") as f:
                f.write(self.editor.get("1.0", tk.END))
            self.dirty = False
            self.update_title()
            self.log_console(f"저장 완료: {self.current_file_path}", "ok")
        except Exception as e:
            messagebox.showerror("저장 오류", f"파일 저장 중 오류가 발생했습니다.\n{e}")

    def save_file_as(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".py",
            filetypes=[("Python 파일", "*.py"), ("텍스트 파일", "*.txt"), ("모든 파일", "*.*")],
        )
        if not path:
            return
        self.current_file_path = path
        self.save_file()

    def confirm_discard_changes(self):
        """저장되지 않은 변경사항이 있으면 확인 팝업을 띄웁니다."""
        if not self.dirty:
            return True
        answer = messagebox.askyesnocancel("저장되지 않은 변경사항", "변경사항을 저장하시겠습니까?")
        if answer is None:
            return False  # 취소
        if answer:
            self.save_file()
        return True

    # ---------------- 코드 실행 ----------------
    def run_python_file(self):
        if self.current_file_path is None or not self.current_file_path.endswith(".py"):
            messagebox.showwarning("실행 불가", "먼저 .py 파일을 저장한 뒤 실행해주세요.")
            return
        self.save_file()
        self.bottom_tabs.select(0)  # 실행 시 OUTPUT 탭으로 자동 전환
        self.log_console(f"▶ 실행: {self.current_file_path}\n", "ok")
        threading.Thread(target=self._run_subprocess, daemon=True).start()

    def _run_subprocess(self):
        try:
            result = subprocess.run(
                [sys.executable, self.current_file_path],
                capture_output=True, text=True, timeout=30,
            )
            if result.stdout:
                self.root.after(0, self.log_console, result.stdout, "ok")
            if result.stderr:
                self.root.after(0, self.log_console, result.stderr, "err")
            self.root.after(0, self.log_console, f"\n(종료 코드: {result.returncode})\n", "ok")
        except subprocess.TimeoutExpired:
            self.root.after(0, self.log_console, "실행 시간이 30초를 초과하여 중단되었습니다.\n", "err")
        except Exception as e:
            self.root.after(0, self.log_console, f"실행 오류: {e}\n", "err")

    def focus_terminal(self):
        self.bottom_tabs.select(1)
        self.term_input.focus()

    def log_console(self, text, tag="ok"):
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, text + "\n", tag)
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)

    # ─────────────────────────────────────────────────────────
    # 우측 AI 어시스턴트 패널
    # ─────────────────────────────────────────────────────────
    def build_ai_panel(self, parent):
        header = tk.Frame(parent, bg="#2d2d2d")
        header.pack(fill=tk.X)

        self.ai_tab_label = tk.Label(header, text=" 🤖 AI Assistant (Chat)", font=("Consolas", 10, "bold"),
                                      bg="#2d2d2d", fg="#ffffff", anchor="w", padx=8, pady=6)
        self.ai_tab_label.pack(side=tk.LEFT)

        mode_btns = tk.Frame(header, bg="#2d2d2d")
        mode_btns.pack(side=tk.RIGHT, padx=6)
        tk.Button(mode_btns, text="💬", command=lambda: self.switch_ai_mode("chat"),
                  bg="#333333", fg="#fff", relief=tk.FLAT, cursor="hand2").pack(side=tk.LEFT, padx=2)
        tk.Button(mode_btns, text="🔍", command=lambda: self.switch_ai_mode("search"),
                  bg="#333333", fg="#fff", relief=tk.FLAT, cursor="hand2").pack(side=tk.LEFT, padx=2)

        self.ai_display = scrolledtext.ScrolledText(
            parent, wrap=tk.WORD, state=tk.DISABLED, bg="#1e1e1e", fg="#d4d4d4",
            font=("Consolas", 10), relief=tk.FLAT, padx=10, pady=10,
        )
        self.ai_display.pack(fill=tk.BOTH, expand=True)
        self.ai_display.tag_config("user_tag", foreground="#569cd6", font=("Consolas", 9, "bold"))
        self.ai_display.tag_config("user_text", foreground="#ce9178")
        self.ai_display.tag_config("bot_tag", foreground="#4ec9b0", font=("Consolas", 9, "bold"))
        self.ai_display.tag_config("bot_text", foreground="#d4d4d4")
        self.ai_display.tag_config("error_text", foreground="#f44747")
        self.ai_display.tag_config("system_text", foreground="#dcdcaa")

        quick_row = tk.Frame(parent, bg="#1e1e1e")
        quick_row.pack(fill=tk.X, padx=8, pady=(0, 4))
        tk.Button(quick_row, text="코드 리뷰", command=self.ask_ai_review_code,
                  bg="#333333", fg="#fff", relief=tk.FLAT, font=("Consolas", 8), cursor="hand2").pack(side=tk.LEFT, padx=2)
        tk.Button(quick_row, text="버그 수정", command=self.ask_ai_fix_code,
                  bg="#333333", fg="#fff", relief=tk.FLAT, font=("Consolas", 8), cursor="hand2").pack(side=tk.LEFT, padx=2)

        input_row = tk.Frame(parent, bg="#1e1e1e", padx=8, pady=8)
        input_row.pack(fill=tk.X)

        self.ai_input = tk.Entry(input_row, font=("Consolas", 11), bg="#252526", fg="#cccccc",
                                  bd=0, insertbackground="#ffffff")
        self.ai_input.pack(side=tk.LEFT, fill=tk.X, expand=True, ipady=6, padx=(0, 6))
        self.ai_input.bind("<Return>", lambda e: self.start_ai_thread())

        self.ai_send_btn = tk.Button(input_row, text="RUN", command=self.start_ai_thread,
                                      bg="#0e639c", fg="#fff", relief=tk.FLAT, cursor="hand2",
                                      font=("Consolas", 10, "bold"))
        self.ai_send_btn.pack(side=tk.RIGHT)

        self.ai_status = tk.Label(parent, text="Ready", font=("Segoe UI", 8),
                                   bg="#007acc", fg="#ffffff", anchor="w", padx=8)
        self.ai_status.pack(fill=tk.X, side=tk.BOTTOM)

    def switch_ai_mode(self, mode):
        self.current_mode = mode
        label = "💬 AI Assistant (Chat)" if mode == "chat" else "🔍 AI Assistant (Search)"
        self.ai_tab_label.config(text=f" {label}")
        self.ai_send_btn.config(text="RUN" if mode == "chat" else "SEARCH",
                                 bg="#0e639c" if mode == "chat" else "#cd7f32")

    def append_ai_message(self, sender, text):
        self.ai_display.config(state=tk.NORMAL)
        if sender == "User":
            self.ai_display.insert(tk.END, "🙋 You\n", "user_tag")
            self.ai_display.insert(tk.END, f"{text}\n\n", "user_text")
        elif sender == "Bot":
            self.ai_display.insert(tk.END, "🤖 Groq\n", "bot_tag")
            self.ai_display.insert(tk.END, f"{text}", "bot_text")
        elif sender == "System":
            self.ai_display.insert(tk.END, f"[INFO] {text}\n\n", "system_text")
        else:
            self.ai_display.insert(tk.END, f"[ERROR] {text}\n\n", "error_text")
        self.ai_display.see(tk.END)
        self.ai_display.config(state=tk.DISABLED)

    def append_ai_chunk(self, chunk_text):
        self.ai_display.config(state=tk.NORMAL)
        self.ai_display.insert(tk.END, chunk_text, "bot_text")
        self.ai_display.see(tk.END)
        self.ai_display.config(state=tk.DISABLED)

    def finish_ai_message(self):
        self.ai_display.config(state=tk.NORMAL)
        self.ai_display.insert(tk.END, "\n\n")
        self.ai_display.config(state=tk.DISABLED)

    def ask_ai_review_code(self):
        code = self.editor.get("1.0", tk.END).strip()
        if not code:
            messagebox.showinfo("코드 없음", "에디터에 코드가 없습니다.")
            return
        self.send_ai_query(f"다음 코드를 리뷰하고 개선할 점을 알려줘:\n```\n{code[:6000]}\n```")

    def ask_ai_fix_code(self):
        code = self.editor.get("1.0", tk.END).strip()
        if not code:
            messagebox.showinfo("코드 없음", "에디터에 코드가 없습니다.")
            return
        self.send_ai_query(f"다음 코드에서 버그를 찾아 수정해줘:\n```\n{code[:6000]}\n```")

    def start_ai_thread(self):
        query = self.ai_input.get().strip()
        if not query:
            return
        self.ai_input.delete(0, tk.END)
        self.send_ai_query(query)

    def send_ai_query(self, query):
        if self.is_requesting:
            return
        if self.client is None:
            messagebox.showwarning("설정 필요", "API 키가 설정되지 않아 요청을 보낼 수 없습니다.")
            return

        self.is_requesting = True
        self.ai_send_btn.config(state=tk.DISABLED, bg="#333333")
        self.ai_status.config(text="⚡ 요청 처리 중...")
        self.append_ai_message("User", query)

        threading.Thread(target=self.fetch_ai_response, args=(query,), daemon=True).start()

    def fetch_ai_response(self, query):
        mode = self.current_mode
        try:
            selected_model = CHAT_MODEL if mode == "chat" else SEARCH_MODEL
            completion = self.client.chat.completions.create(
                model=selected_model,
                messages=[{"role": "user", "content": query}],
                temperature=0.7 if mode == "search" else 1,
                max_completion_tokens=2048,
                top_p=1,
                stream=True,
                stop=None,
            )
            self.root.after(0, self.append_ai_message, "Bot", "")
            for chunk in completion:
                chunk_text = chunk.choices[0].delta.content or ""
                if chunk_text:
                    self.root.after(0, self.append_ai_chunk, chunk_text)
            self.root.after(0, self.finish_ai_message)
        except Exception as e:
            self.root.after(0, self.append_ai_message, "Error", f"API 호출 중 오류 발생: {e}")
        finally:
            btn_color = "#0e639c" if mode == "chat" else "#cd7f32"

            def restore():
                self.is_requesting = False
                self.ai_send_btn.config(state=tk.NORMAL, bg=btn_color)
                self.ai_status.config(text="Ready")

            self.root.after(0, restore)


if __name__ == "__main__":
    root = tk.Tk()
    app = MiniIDE(root)
    root.mainloop()