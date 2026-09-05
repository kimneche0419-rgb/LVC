#!/bin/bash
# Windows 빌드 스크립트 — WSL 에서 MSYS2 MINGW64 환경을 호출해 exe/인스톨러를 만든다.
# 사용: ./build_windows.sh          → exe 빌드 + 배포 폴더 + 인스톨러
#       ./build_windows.sh compile  → exe 컴파일만
set -e
cd "$(dirname "$0")"

MSYS2_BASH=/mnt/c/msys64/usr/bin/bash.exe
if [ ! -f "$MSYS2_BASH" ]; then
    echo "오류: MSYS2 를 찾을 수 없습니다 (C:\\msys64)"
    exit 1
fi

TARGET="${1:-all}"

case "$TARGET" in
    compile) MAKE_TARGET="all" ;;
    all)     MAKE_TARGET="installer" ;;
    *)       MAKE_TARGET="$TARGET" ;;
esac

# MSYS2 는 WSL 경로(/mnt/c/...)를 모르므로 /c/... 형태로 바꿔준다.
MSYS_PWD=$(pwd | sed 's|^/mnt/\(.\)/|/\1/|')

# MSYSTEM=MINGW64 로 mingw64 툴체인을 쓴다. WSL 에서 호출하면 로그인
# 프로필이 mingw64 PATH 를 안 만들어 주는 경우가 있어 직접 추가한다.
MSYSTEM=MINGW64 "$MSYS2_BASH" -lc "export PATH=/mingw64/bin:\$PATH; cd '$MSYS_PWD' && make -f Makefile.win $MAKE_TARGET"
