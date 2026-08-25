"""미니 터미널 — 입력한 명령어를 subprocess 로 실행하고 결과를 출력.

default.py 의 build_terminal / run_terminal_command 로직을 그대로 이식.
"""
import os
import subprocess
import threading
import tkinter as tk
from tkinter import scrolledtext


class Terminal(tk.Frame):
    """OUTPUT 탭 아래 들어가는 간이 터미널."""

    def __init__(self, master, initial_cwd=None, **kwargs):
        super().__init__(master, bg="#0c0c0c", **kwargs)
        self.cwd = initial_cwd or os.getcwd()

        # 입력창을 먼저 side=tk.BOTTOM 으로 고정 배치해야 함.
        # term_display(expand=True)를 먼저 pack하면 입력창이 밀려서
        # 안 보이거나 클릭/입력이 안 되는 문제가 있었음.
        input_row = tk.Frame(self, bg="#0c0c0c")
        input_row.pack(fill=tk.X, side=tk.BOTTOM, padx=6, pady=4)

        self.display = scrolledtext.ScrolledText(
            self, wrap=tk.WORD, state=tk.DISABLED, bg="#0c0c0c", fg="#d4d4d4",
            font=("Consolas", 10), relief=tk.FLAT, padx=8, pady=6,
        )
        self.display.pack(fill=tk.BOTH, expand=True)
        self.display.tag_config("prompt", foreground="#4ec9b0", font=("Consolas", 10, "bold"))
        self.display.tag_config("cmd_text", foreground="#ffffff")
        self.display.tag_config("out_text", foreground="#d4d4d4")
        self.display.tag_config("err_text", foreground="#f44747")

        self.prompt_label = tk.Label(input_row, text=self._prompt_text(),
                                     font=("Consolas", 10, "bold"), bg="#0c0c0c", fg="#4ec9b0")
        self.prompt_label.pack(side=tk.LEFT)

        self.input = tk.Entry(input_row, font=("Consolas", 10), bg="#0c0c0c", fg="#ffffff",
                              insertbackground="#ffffff", bd=0)
        self.input.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 0))
        self.input.bind("<Return>", lambda e: self.run_command())

        # 명령 히스토리 (위/아래 화살표로 탐색)
        self.history = []
        self.history_idx = -1
        self.input.bind("<Up>", self._history_up)
        self.input.bind("<Down>", self._history_down)

        self.println(f"미니 터미널이 시작되었습니다. 작업 폴더: {self.cwd}", "out_text")

    # ---------------- 외부에서 쓰는 인터페이스 ----------------
    def set_cwd(self, folder):
        """폴더 열기 등으로 작업 폴더가 바뀔 때 호출."""
        self.cwd = folder
        self.prompt_label.config(text=self._prompt_text())
        self.println(f"작업 폴더가 변경되었습니다: {folder}", "out_text")

    def focus_input(self):
        self.input.focus_set()

    # ---------------- 내부 동작 ----------------
    def _prompt_text(self):
        folder_name = os.path.basename(self.cwd) or self.cwd
        return f"{folder_name}>"

    def println(self, text, tag="out_text"):
        self.display.config(state=tk.NORMAL)
        self.display.insert(tk.END, text + "\n", tag)
        self.display.see(tk.END)
        self.display.config(state=tk.DISABLED)

    def _history_up(self, _event):
        if not self.history:
            return
        self.history_idx = max(0, self.history_idx - 1)
        self.input.delete(0, tk.END)
        self.input.insert(0, self.history[self.history_idx])

    def _history_down(self, _event):
        if not self.history:
            return
        self.history_idx = min(len(self.history), self.history_idx + 1)
        self.input.delete(0, tk.END)
        if self.history_idx < len(self.history):
            self.input.insert(0, self.history[self.history_idx])

    def run_command(self):
        # 신뢰 경계: 여기서 실행되는 명령은 '이 PC의 사용자가 직접 입력한 것'뿐이다.
        # AI 응답 등 다른 출처의 텍스트가 이 메서드로 흘러들어오지 않게 유지할 것.
        command = self.input.get().strip()
        if not command:
            return
        self.input.delete(0, tk.END)
        self.history.append(command)
        self.history_idx = len(self.history)

        self.println(f"{self._prompt_text()} {command}", "prompt")

        # cd 명령은 실제 프로세스가 아니라 파이썬에서 직접 작업 폴더를 변경
        # (매 실행이 독립 프로세스라 상태 유지가 안 되기 때문)
        if command.startswith("cd ") or command == "cd":
            self._handle_cd(command)
            return

        if command in ("cls", "clear"):
            self.display.config(state=tk.NORMAL)
            self.display.delete("1.0", tk.END)
            self.display.config(state=tk.DISABLED)
            return

        threading.Thread(target=self._run_subprocess, args=(command,), daemon=True).start()

    def _handle_cd(self, command):
        target = command[3:].strip() if command != "cd" else ""
        if not target:
            self.println(self.cwd, "out_text")
            return
        new_path = target if os.path.isabs(target) else os.path.join(self.cwd, target)
        new_path = os.path.normpath(new_path)
        if os.path.isdir(new_path):
            self.cwd = new_path
            self.prompt_label.config(text=self._prompt_text())
        else:
            self.println(f"경로를 찾을 수 없습니다: {new_path}", "err_text")

    def _run_subprocess(self, command):
        try:
            result = subprocess.run(
                command, shell=True, cwd=self.cwd,
                capture_output=True, text=True, timeout=60,
            )
            if result.stdout:
                self.after(0, self.println, result.stdout.rstrip("\n"), "out_text")
            if result.stderr:
                self.after(0, self.println, result.stderr.rstrip("\n"), "err_text")
        except subprocess.TimeoutExpired:
            self.after(0, self.println, "명령 실행이 60초를 초과하여 중단되었습니다.", "err_text")
        except Exception as e:
            self.after(0, self.println, f"실행 오류: {e}", "err_text")
