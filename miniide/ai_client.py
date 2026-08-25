"""로컬 Ollama 서버와 통신하는 클라이언트.

표준 라이브러리(urllib)만 사용 — pip install 이 필요 없고
Windows 파이썬에서도 그대로 실행된다.
WSL 안에서 띄운 Ollama도 Windows 의 localhost 포워딩으로 접근 가능.
"""
import json
import os
import urllib.error
import urllib.request

# 기본 연결 정보 (환경변수 OLLAMA_HOST 로 주소를 바꿀 수 있음)
DEFAULT_BASE_URL = "http://localhost:11434"
DEFAULT_MODEL = "qwen2.5-coder:3b"


class OllamaClient:
    """Ollama /api/chat 스트리밍 호출을 감싸는 얇은 클라이언트."""

    def __init__(self, base_url=None, model=None):
        base = base_url or os.environ.get("OLLAMA_HOST") or DEFAULT_BASE_URL
        self.base_url = base.rstrip("/")
        self.model = model or DEFAULT_MODEL

    def is_available(self):
        """Ollama 서버가 살아 있는지 확인 (GET /api/tags)."""
        try:
            with urllib.request.urlopen(f"{self.base_url}/api/tags", timeout=3) as resp:
                return resp.status == 200
        except (urllib.error.URLError, OSError, ValueError):
            return False

    def chat_stream(self, messages):
        """대화 기록(리스트)을 보내고, 응답 조각을 순서대로 yield.

        messages 형식: [{"role": "user"|"assistant"|"system", "content": "..."}]
        Ollama 는 스트리밍 시 한 줄에 JSON 하나씩(NDJSON)을 내려준다.
        """
        payload = json.dumps({
            "model": self.model,
            "messages": messages,
            "stream": True,
        }).encode("utf-8")
        req = urllib.request.Request(
            f"{self.base_url}/api/chat",
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=300) as resp:
            for raw_line in resp:
                line = raw_line.strip()
                if not line:
                    continue
                data = json.loads(line)
                content = data.get("message", {}).get("content", "")
                if content:
                    yield content
                if data.get("done"):
                    return
