"""코드 에디터 — 줄번호 캔버스 + 간단한 파이썬 문법 강조.

default.py 의 LineNumberCanvas / highlight_syntax 로직을 그대로 이식.
"""
import re
import tkinter as tk

# 간단한 파이썬 문법 강조용 키워드
PY_KEYWORDS = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield", "self",
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
            self.create_text(38, y, anchor="ne", text=line_num, fill="#5a5a5a",
                             font=("Consolas", 10))
            i = self.text_widget.index(f"{i}+1line")


class CodeEditor(tk.Frame):
    """줄번호 + 문법 강조가 붙은 텍스트 에디터 프레임."""

    def __init__(self, master, on_change=None, **kwargs):
        super().__init__(master, bg="#1e1e1e", **kwargs)
        self.on_change = on_change  # 키가 눌릴 때마다 호출되는 콜백 (앱이 dirty 처리 등에 사용)

        self.text = tk.Text(
            self, wrap=tk.NONE, bg="#1e1e1e", fg="#d4d4d4",
            insertbackground="#ffffff", font=("Consolas", 12), relief=tk.FLAT,
            undo=True, padx=8, pady=6,
        )
        self.linenos = LineNumberCanvas(self, self.text)
        self.linenos.pack(side=tk.LEFT, fill=tk.Y)
        self.text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.text.bind("<KeyRelease>", self._on_key_release)
        self.text.bind("<MouseWheel>", lambda e: self.after(1, self.linenos.redraw))
        self.text.bind("<ButtonRelease>", lambda e: self.linenos.redraw())
        self.text.bind("<Configure>", lambda e: self.linenos.redraw())

        # 문법 강조 태그
        self.text.tag_config("keyword", foreground="#569cd6")
        self.text.tag_config("string", foreground="#ce9178")
        self.text.tag_config("comment", foreground="#6a9955")

    # ---------------- 내부 이벤트 ----------------
    def _on_key_release(self, _event=None):
        if self.on_change:
            self.on_change()
        self.linenos.redraw()
        self.highlight_syntax()

    # ---------------- 내용 접근 ----------------
    def get_content(self):
        return self.text.get("1.0", tk.END)

    def set_content(self, content):
        """파일을 여는 등 프로그램적으로 내용을 교체 (dirty 로 취급하지 않음)."""
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", content)
        self.linenos.redraw()
        self.highlight_syntax()

    # ---------------- 문법 강조 ----------------
    def highlight_syntax(self):
        """아주 단순한 파이썬 문법 강조 (키워드 / 문자열 / 주석)"""
        code = self.text.get("1.0", tk.END)
        for tag in ("keyword", "string", "comment"):
            self.text.tag_remove(tag, "1.0", tk.END)

        for match in re.finditer(r"\b[a-zA-Z_][a-zA-Z0-9_]*\b", code):
            if match.group() in PY_KEYWORDS:
                self._tag_range("keyword", match.start(), match.end())

        for match in re.finditer(r"(\".*?\"|'.*?')", code):
            self._tag_range("string", match.start(), match.end())

        for match in re.finditer(r"#.*", code):
            self._tag_range("comment", match.start(), match.end())

    def _tag_range(self, tag, start_char, end_char):
        self.text.tag_add(tag, f"1.0+{start_char}c", f"1.0+{end_char}c")
