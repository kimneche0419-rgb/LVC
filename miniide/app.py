"""메인 윈도우 조립 — 메뉴, 3분할 레이아웃, 파일 입출력, 코드 실행.

default.py 의 MiniIDE 클래스에서 UI 조립/파일 관리 부분을 이식하고,
에디터·탐색기·터미널·AI 패널은 각 모듈의 위젯으로 교체.
"""
import os
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext, filedialog

from .ai_client import OllamaClient
from .ai_panel import AIPanel
from .editor import CodeEditor
from .explorer import Explorer, TEXT_EXTENSIONS
from .terminal import Terminal


class MiniIDEApp:

    def __init__(self, root):
        self.root = root
        self.root.title("Mini IDE — 로컬 AI (Ollama)")
        self.root.geometry("1180x760")
        self.root.minsize(820, 560)
        self.root.configure(bg="#1e1e1e")

        self.current_file_path = None  # 현재 에디터에 열려있는 파일 경로
        self.dirty = False             # 저장되지 않은 변경사항 여부

        self._build_menu()
        self._build_layout()

    # ─────────────────────────────────────────────
    # 상단 메뉴바
    # ─────────────────────────────────────────────
    def _build_menu(self):
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="새 파일", command=self.new_file, accelerator="Ctrl+N")
        file_menu.add_command(label="폴더 열기", command=self.explorer.open_folder, accelerator="Ctrl+O")
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
        ai_menu.add_command(label="현재 코드 리뷰 요청", command=self.ai_panel.ask_review_code)
        ai_menu.add_command(label="현재 코드 버그 수정 요청", command=self.ai_panel.ask_fix_code)
        menubar.add_cascade(label="AI", menu=ai_menu)

        self.root.config(menu=menubar)

        # 단축키 바인딩
        self.root.bind("<Control-n>", lambda e: self.new_file())
        self.root.bind("<Control-o>", lambda e: self.explorer.open_folder())
        self.root.bind("<Control-s>", lambda e: self.save_file())
        self.root.bind("<F5>", lambda e: self.run_python_file())
        self.root.bind("<Control-grave>", lambda e: self.focus_terminal())

    # ─────────────────────────────────────────────
    # 전체 레이아웃: [파일 탐색기 | 에디터+출력콘솔 | AI 어시스턴트]
    # ─────────────────────────────────────────────
    def _build_layout(self):
        paned = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg="#1e1e1e",
                               sashwidth=4, sashrelief=tk.FLAT)
        paned.pack(fill=tk.BOTH, expand=True)

        # [좌측] 파일 탐색기 (폴더를 열면 터미널 작업 폴더도 함께 갱신)
        self.explorer = Explorer(paned, on_open_file=self.load_file_into_editor,
                                 on_folder_opened=self._on_folder_opened)
        paned.add(self.explorer, minsize=160)

        # [중앙] 에디터 + 실행 콘솔
        center_frame = tk.Frame(paned, bg="#1e1e1e")
        self._build_center(center_frame)
        paned.add(center_frame, minsize=400)

        # [우측] AI 어시스턴트 패널
        self.ai_client = OllamaClient()
        self.ai_panel = AIPanel(paned, self.ai_client,
                                get_code=lambda: self.editor.get_content())
        paned.add(self.ai_panel, minsize=280)

    def _on_folder_opened(self, folder):
        # 터미널은 이후 _build_center 에서 생성되지만, 이 콜백은
        # 사용자가 폴더를 여는 시점(생성 이후)에만 호출되므로 안전하다.
        if hasattr(self, "terminal"):
            self.terminal.set_cwd(folder)

    def _build_center(self, parent):
        # 파일 경로 표시줄
        self.path_label = tk.Label(parent, text="  (열린 파일 없음)", font=("Consolas", 9),
                                   bg="#2d2d2d", fg="#cccccc", anchor="w", padx=8, pady=4)
        self.path_label.pack(fill=tk.X)

        # 하단 패널(OUTPUT/TERMINAL)을 먼저 밑에 고정
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
        self.terminal = Terminal(self.bottom_tabs)
        self.bottom_tabs.add(self.terminal, text="TERMINAL")

        # 에디터 (남은 공간 전체)
        self.editor = CodeEditor(parent, on_change=self.on_editor_change)
        self.editor.pack(fill=tk.BOTH, expand=True)

    # ---------------- 에디터 변경/제목 ----------------
    def on_editor_change(self):
        self.dirty = True
        self.update_title()

    def update_title(self):
        name = os.path.basename(self.current_file_path) if self.current_file_path else "제목 없음"
        mark = " ●" if self.dirty else ""
        self.path_label.config(text=f"  {self.current_file_path or name}{mark}")

    # ---------------- 파일 입출력 ----------------
    def new_file(self):
        if not self.confirm_discard_changes():
            return
        self.editor.set_content("")
        self.current_file_path = None
        self.dirty = False
        self.update_title()

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

        self.editor.set_content(content)
        self.current_file_path = path
        self.dirty = False
        self.update_title()

    def save_file(self):
        if self.current_file_path is None:
            return self.save_file_as()
        try:
            with open(self.current_file_path, "w", encoding="utf-8") as f:
                f.write(self.editor.get_content())
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
        self.terminal.focus_input()

    def log_console(self, text, tag="ok"):
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, text + "\n", tag)
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)


def run():
    root = tk.Tk()
    MiniIDEApp(root)
    root.mainloop()
