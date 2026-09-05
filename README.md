# Mini VS — 로컬 AI 코드 에디터

C와 GTK3로 직접 만든 가벼운 IDE. Ollama 로컬 모델로 코드 생성 · 수정을
도와주는 AI 패널이 내장되어 있습니다.
인터넷 연결 없이 내 컴퓨터에서만 동작합니다.

## 기능

- 파일 탐색기 · 코드 에디터 · 내장 터미널 (ConPTY)
- AI 패널: Ollama 로컬 모델 연동 (코드 생성, 질의응답)

## 권장 사양 (Windows 전용)

| 항목 | 요구 사양 |
|------|-----------|
| OS | Windows 10 21H2 이상 (Windows 전용 — Linux/macOS 빌드 경로는 제거됨) |
| CPU | 4코어 이상 |
| 메모리 | 16GB 이상 권장 (deepseek-coder 모델이 약 10GB 사용) |
| 저장 공간 | 여유 15GB 이상 (앱 + 모델 포함) |
| GPU | 불필요 (CPU 추론) |

## 요구 사항

- **MSYS2 (mingw64)** — Windows 네이티브 빌드 툴체인 (`C:\msys64`에 GTK3/mingw64 패키지 설치 상태)
- **Ollama** — 아래 기본 설치 세트 모델

```powershell
# Windows PowerShell
ollama pull deepseek-coder-v2:16b-lite-instruct   # CODE/PLAN용, 약 10GB
ollama pull qwen2.5-coder:1.5b                    # LIGHT용, 약 1GB
ollama list                                        # 설치 확인 (기본 세트는 2개)
```

- 기본 설치 세트: `deepseek-coder-v2:16b-lite-instruct`(~10GB, CODE/PLAN) +
  `qwen2.5-coder:1.5b`(~1GB, LIGHT) — 합계 약 11GB
- `qwen2.5-coder:3b`는 폴백 전용 후보로 기본 설치 대상이 아닙니다
- 역할별 모델 지정을 바꾸고 싶으면 `<config>/miniide/models.txt`에
  `light=` / `code=` / `plan=` 으로 지정할 수 있습니다
  (지정한 모델이 실제로 설치되어 있을 때만 적용됩니다)

## 빌드 및 실행

Windows용 exe와 인스톨러는 WSL에서 `build_windows.sh`(MSYS2 mingw64 경유)로 만든다.
터미널은 Windows의 ConPTY, 한글 입출력은 UTF-8로 지원되고
메뉴(보기 → 언어)에서 한국어/English를 전환할 수 있다.

```bash
# WSL 에서 (C:\msys64 에 MSYS2 + mingw64 패키지 설치 상태)
cd miniide_c
./build_windows.sh          # dist/MiniIDE-Setup.exe 생성
./build_windows.sh compile  # exe 컴파일만
```

- 빌드 중간 산출물(`*.win.o`)은 `miniide_c/build/` 디렉터리에만 생성됩니다 (out-of-tree 빌드)
- `miniide_c/dist/`는 재현 가능한 산출물로 언제든 삭제해도 되며,
  필요 시 `make -f Makefile.win bundle`로 다시 만들 수 있습니다
- 포터블 실행: `dist/MiniIDE/bin/miniide.exe`
- 설치 배포: `dist/MiniIDE-Setup.exe` (시작 메뉴/바탕화면 바로가기 생성, 제어판 제거 지원)

## 사용 방법

1. Ollama 서버 실행: `ollama serve`
2. `dist/MiniIDE/bin/miniide.exe` 실행
3. 파일 탐색기에서 파일 열기 → 에디터에서 편집
4. AI 패널에 요청을 입력하면 로컬 모델이 응답

## 라이선스

MIT
