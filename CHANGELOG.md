# Changelog

이 프로젝트의 주요 변경 사항을 기록합니다.
형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를 따르며,
버전 번호는 [Semantic Versioning](https://semver.org/spec/v2.0.0.html)을 따릅니다.

## [미출시(Unreleased)]

### 변경 (Changed)

- **AI 역할별 모델 라인업 갱신** (SPEC-STACK-001) — CODE/PLAN 역할의 후보열이
  `deepseek-coder-v2:16b-lite-instruct`(~10GB, Q4)를 우선하고
  `qwen2.5-coder:3b` → `qwen2.5-coder:1.5b` 순으로 폴백한다. LIGHT는 기존과
  동일하게 1.5b 우선. 역할별 `num_predict` 예산(600/1400/900)은 무변경.
  기본 설치 세트는 deepseek-coder + `qwen2.5-coder:1.5b` 2개(합계 약 11GB)로
  확정되었고 `qwen2.5-coder:3b`는 폴백 전용(기본 미설치)으로 전환.
  (`miniide_c/ai_panel.c`)
- **Windows 전용 전환** (SPEC-STACK-001) — Linux 빌드 경로 제거:
  Linux용 `Makefile`, `miniide.desktop`, VTE 기반 `terminal.c` 삭제.
  `terminal.h`는 공유 인터페이스로 유지하며 터미널은 ConPTY 구현만 사용.

### 추가 (Added)

- **out-of-tree 빌드 디렉터리** (SPEC-STACK-001) — `Makefile.win`이 오브젝트
  파일을 `miniide_c/build/`에만 생성하도록 변경. 소스 파일 옆에 중간 산출물이
  더 이상 생기지 않는다. `clean` 타겟도 `build/`를 정리하도록 갱신.

### 제거 (Removed)

- **`miniide_c/tests/` ConPTY 진단 스크래치 삭제** (SPEC-STACK-001) — 일회성
  진단용 스크래치(`conpty_mini.c`, `conpty_test*.c`와 산출물). 관련 버그픽스는
  이미 커밋되어 있어 재현 필요 시 재작성.

### 보안 (Security)

- **`.gitignore` 보강** (SPEC-STACK-001) — 일반 `*.exe` 룰, `miniide_c/tests/`
  산출물(`.exe`/`.out`/`.log`), `miniide_c/build/`, `.webui_secret_key` 패턴
  추가. 시크릿 파일이 어떤 브랜치에서도 커밋되지 않도록 방어.

## [1.0.0]

- 최초 안정 릴리스 (NSIS 인스톨러 `installer/miniide.nsi` VERSION 기준)
