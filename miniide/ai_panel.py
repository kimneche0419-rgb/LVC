"""AI 어시스턴트 패널 — 로컬 Ollama(qwen2.5-coder)와 대화.

기존 Groq 클라우드 버전에서 바뀐 점:
  • API 키/클라우드 의존성 제거 → 로컬 Ollama 호출 (ai_client.OllamaClient)
  • 대화 기록(messages)을 유지 — 이전 말을 기억하고 이어서 대화함
  • 검색 모드 제거 — 로컬 모델은 웹 검색을 할 수 없음
"""
import re
import threading
import tkinter as tk
from tkinter import messagebox, scrolledtext

from .ai_client import OllamaClient

# 대화 기록이 이 길이를 넘으면 오래된 것부터 잘라냄 (3B 모델 컨텍스트 보호)
MAX_HISTORY_MESSAGES = 30

# AI 답변에서 코드블록(```...```)을 뽑아내는 패턴
CODE_BLOCK_RE = re.compile(r"```(?:\w+)?\n(.*?)```", re.DOTALL)


class AIPanel(tk.Frame):
    """우측 AI 채팅 패널.

    콜백:
      get_code() — 퀵 액션(코드 리뷰/버그 수정)에 넣을 현재 에디터 코드
      apply_code(code) — "코드 적용" 버튼을 눌렀을 때 에디터에 반영
    """

    def __init__(self, master, client, get_code, apply_code=None, **kwargs):
        super().__init__(master, bg="#1e1e1e", **kwargs)
        self.client = client  # OllamaClient 인스턴스
        self.get_code = get_code
        self.apply_code = apply_code
        self.messages = []       # Ollama 에 보낼 대화 기록
        self.is_requesting = False
        self.last_bot_response = ""  # 가장 최근 AI 답변 (코드 적용용)

        self._build()

        # 시작 시 Ollama 서버 상태를 백그라운드에서 확인
        threading.Thread(target=self._check_server, daemon=True).start()

    # ---------------- UI 구성 ----------------
    def _build(self):
        header = tk.Frame(self, bg="#2d2d2d")
        header.pack(fill=tk.X)

        tk.Label(header, text=" 🤖 AI Assistant (로컬)", font=("Consolas", 10, "bold"),
                 bg="#2d2d2d", fg="#ffffff", anchor="w", padx=8, pady=6).pack(side=tk.LEFT)

        # 새 대화 버튼 — 대화 기록을 비우고 처음부터
        tk.Button(header, text="🗑 새 대화", command=self.clear_conversation,
                  bg="#333333", fg="#fff", relief=tk.FLAT, cursor="hand2",
                  font=("Consolas", 8)).pack(side=tk.RIGHT, padx=6)

        self.display = scrolledtext.ScrolledText(
            self, wrap=tk.WORD, state=tk.DISABLED, bg="#1e1e1e", fg="#d4d4d4",
            font=("Consolas", 10), relief=tk.FLAT, padx=10, pady=10,
        )
        self.display.pack(fill=tk.BOTH, expand=True)
        self.display.tag_config("user_tag", foreground="#569cd6", font=("Consolas", 9, "bold"))
        self.display.tag_config("user_text", foreground="#ce9178")
        self.display.tag_config("bot_tag", foreground="#4ec9b0", font=("Consolas", 9, "bold"))
        self.display.tag_config("bot_text", foreground="#d4d4d4")
        self.display.tag_config("error_text", foreground="#f44747")
        self.display.tag_config("system_text", foreground="#dcdcaa")

        # 퀵 액션 버튼
        quick_row = tk.Frame(self, bg="#1e1e1e")
        quick_row.pack(fill=tk.X, padx=8, pady=(0, 4))
        tk.Button(quick_row, text="코드 리뷰", command=self.ask_review_code,
                  bg="#333333", fg="#fff", relief=tk.FLAT, font=("Consolas", 8),
                  cursor="hand2").pack(side=tk.LEFT, padx=2)
        tk.Button(quick_row, text="버그 수정", command=self.ask_fix_code,
                  bg="#333333", fg="#fff", relief=tk.FLAT, font=("Consolas", 8),
                  cursor="hand2").pack(side=tk.LEFT, padx=2)

        # 입력창 + 전송 버튼
        input_row = tk.Frame(self, bg="#1e1e1e", padx=8, pady=8)
        input_row.pack(fill=tk.X)

        self.input = tk.Entry(input_row, font=("Consolas", 11), bg="#252526", fg="#cccccc",
                              bd=0, insertbackground="#ffffff")
        self.input.pack(side=tk.LEFT, fill=tk.X, expand=True, ipady=6, padx=(0, 6))
        self.input.bind("<Return>", lambda e: self._send_from_input())

        self.send_btn = tk.Button(input_row, text="RUN", command=self._send_from_input,
                                  bg="#0e639c", fg="#fff", relief=tk.FLAT, cursor="hand2",
                                  font=("Consolas", 10, "bold"))
        self.send_btn.pack(side=tk.RIGHT)

        self.status = tk.Label(self, text="연결 확인 중...", font=("Segoe UI", 8),
                               bg="#007acc", fg="#ffffff", anchor="w", padx=8)
        self.status.pack(fill=tk.X, side=tk.BOTTOM)

    # ---------------- 서버 상태 ----------------
    def _check_server(self):
        ok = self.client.is_available()
        if ok:
            text = f"✅ Ollama 연결됨 ({self.client.model})"
        else:
            text = "⚠ Ollama 서버에 연결할 수 없음 — WSL 에서 'ollama serve' 실행 필요"
        self.after(0, lambda: self.status.config(text=text))

    # ---------------- 화면 출력 ----------------
    def _append_message(self, sender, text):
        self.display.config(state=tk.NORMAL)
        if sender == "User":
            self.display.insert(tk.END, "🙋 You\n", "user_tag")
            self.display.insert(tk.END, f"{text}\n\n", "user_text")
        elif sender == "Bot":
            self.display.insert(tk.END, f"🤖 {self.client.model}\n", "bot_tag")
            self.display.insert(tk.END, f"{text}", "bot_text")
        elif sender == "System":
            self.display.insert(tk.END, f"[INFO] {text}\n\n", "system_text")
        else:
            self.display.insert(tk.END, f"[ERROR] {text}\n\n", "error_text")
        self.display.see(tk.END)
        self.display.config(state=tk.DISABLED)

    def _append_chunk(self, chunk_text):
        self.display.config(state=tk.NORMAL)
        self.display.insert(tk.END, chunk_text, "bot_text")
        self.display.see(tk.END)
        self.display.config(state=tk.DISABLED)

    def _finish_message(self):
        self.display.config(state=tk.NORMAL)
        self.display.insert(tk.END, "\n\n")
        self.display.config(state=tk.DISABLED)

    # ---------------- 요청 ----------------
    def clear_conversation(self):
        self.messages = []
        self.display.config(state=tk.NORMAL)
        self.display.delete("1.0", tk.END)
        self.display.config(state=tk.DISABLED)
        self._append_message("System", "새 대화를 시작합니다.")

    def ask_review_code(self):
        code = (self.get_code() or "").strip()
        if not code:
            self._append_message("System", "에디터에 코드가 없습니다.")
            return
        self._ask(f"다음 코드를 리뷰하고 개선할 점을 알려줘:\n```\n{code[:6000]}\n```")

    def ask_fix_code(self):
        code = (self.get_code() or "").strip()
        if not code:
            self._append_message("System", "에디터에 코드가 없습니다.")
            return
        self._ask(f"다음 코드에서 버그를 찾아 수정해줘:\n```\n{code[:6000]}\n```")

    def _send_from_input(self):
        query = self.input.get().strip()
        if not query:
            return
        self.input.delete(0, tk.END)
        self._ask(query)

    def _ask(self, query):
        if self.is_requesting:
            return
        self.is_requesting = True
        self.send_btn.config(state=tk.DISABLED, bg="#333333")
        self.status.config(text="⚡ 생성 중... (로컬 모델은 클라우드보다 느려요)")
        self._append_message("User", query)

        # 대화 기록에 사용자 메시지 추가 → 모델이 이전 대화를 기억함
        self.messages.append({"role": "user", "content": query})
        if len(self.messages) > MAX_HISTORY_MESSAGES:
            self.messages = self.messages[-MAX_HISTORY_MESSAGES:]

        threading.Thread(target=self._fetch_response, daemon=True).start()

    def _fetch_response(self):
        try:
            self.after(0, self._append_message, "Bot", "")
            full_response = ""
            for chunk in self.client.chat_stream(self.messages):
                full_response += chunk
                self.after(0, self._append_chunk, chunk)
            # 어시스턴트 응답도 대화 기록에 저장
            self.messages.append({"role": "assistant", "content": full_response})
            self.after(0, self._finish_message)
        except Exception as e:
            # 실패 시 방금 보낸 사용자 메시지를 기록에서 제거해 재시도가 깨끗하게 되도록 함
            if self.messages and self.messages[-1].get("role") == "user":
                self.messages.pop()
            msg = (f"Ollama 호출 실패: {e}\n"
                   "WSL 에서 Ollama 서버가 실행 중인지 확인해주세요 "
                   "(~/ollama/bin/ollama serve).")
            self.after(0, self._append_message, "Error", msg)
        finally:
            def restore():
                self.is_requesting = False
                self.send_btn.config(state=tk.NORMAL, bg="#0e639c")
                self.status.config(text=f"✅ Ollama 연결됨 ({self.client.model})")
            self.after(0, restore)
