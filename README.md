# Mini VS — 로컬 AI 코드 에디터

C와 GTK3로 직접 만든 가벼운 IDE. Ollama 로컬 모델(qwen2.5-coder)로
코드 생성 · 수정을 도와주는 AI 패널이 내장되어 있습니다.
인터넷 연결 없이 내 컴퓨터에서만 동작합니다.

## 기능

- 파일 탐색기 · 코드 에디터 · 내장 터미널 (VTE)
- AI 패널: Ollama 로컬 모델 연동 (코드 생성, 질의응답)

## 권장 사양

| 항목 | Linux (WSL2 포함) | macOS | Windows |
|------|------------------|-------|---------|
| OS | Ubuntu 20.04 이상 / WSL2 Ubuntu 22.04+ | macOS 12 Monterey 이상 | Windows 10 21H2 이상 (WSL2 권장) |
| CPU | 4코어 이상 | Apple Silicon (M1+) 또는 Intel 4코어 | 4코어 이상 |
| 메모리 | 8GB (AI 모델 사용 시 16GB 권장) | 16GB (통합 메모리) | 8GB (AI 모델 사용 시 16GB 권장) |
| 저장 공간 | 2GB 이상 여유 | 2GB 이상 여유 | 2GB 이상 여유 |
| GPU | 선택사항 | Apple Silicon 내장 GPU로 AI 가속 | 선택사항 |
| 비고 | 네이티브 실행 권장 | XQuartz 또는 Homebrew GTK 필요 | WSL2 + WSLg로 실행 |

> 💡 Ollama의 qwen2.5-coder:3b 모델은 약 2GB의 메모리를 사용합니다.
> 원활한 AI 기능 사용을 위해 전체 여유 메모리 6GB 이상을 권장합니다.

## 요구 사항 (Linux/WSL2 네이티브 빌드 기준)

- GTK3, vte-2.91, libcurl 개발 라이브러리
- Ollama (모델: qwen2.5-coder:3b)

```bash
# Ubuntu/WSL 기준
sudo apt install libgtk-3-dev libvte-2.91-dev libcurl4-openssl-dev \
  build-essential pkg-config
ollama pull qwen2.5-coder:3b
```

## 빌드 및 실행

```bash
cd miniide_c
make        # 빌드
make run    # 실행
```

## 사용 방법

1. Ollama 서버 실행: `ollama serve`
2. `make run` 으로 에디터 실행
3. 파일 탐색기에서 파일 열기 → 에디터에서 편집
4. AI 패널에 요청을 입력하면 로컬 모델이 응답

## 라이선스

MIT
