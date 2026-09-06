; Mini IDE Windows 인스톨러 (NSIS)
; 빌드: make -f Makefile.win installer  → dist/MiniIDE-Setup.exe
; MSYS2 mingw64 환경에서 makensis 로 컴파일한다.

!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif

Name "Mini IDE ${VERSION}"
; 경로는 이 스크립트(installer/) 위치 기준
OutFile "..\dist\MiniIDE-Setup.exe"
InstallDir "$PROGRAMFILES64\MiniIDE"
InstallDirRegKey HKLM "Software\MiniIDE" "InstallDir"
RequestExecutionLevel admin
Unicode true

!define MUI_ABORTWARNING
; 인스톨러/제거 프로그램 창 아이콘 (miniide.exe 에 심긴 것과 같은 디자인)
!define MUI_ICON "..\miniide.ico"
!define MUI_UNICON "..\miniide.ico"
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; 한국어 인터페이스 (NSIS 기본 제공 언어파일)
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

Section "Mini IDE"
  SetOutPath "$INSTDIR"
  File /r "..\dist\MiniIDE\*.*"

  CreateDirectory "$SMPROGRAMS\Mini IDE"
  CreateShortcut "$SMPROGRAMS\Mini IDE\Mini IDE.lnk" "$INSTDIR\bin\miniide.exe"
  CreateShortcut "$DESKTOP\Mini IDE.lnk" "$INSTDIR\bin\miniide.exe"

  WriteRegStr HKLM "Software\MiniIDE" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniIDE" \
             "DisplayName" "Mini IDE ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniIDE" \
             "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniIDE" \
             "DisplayVersion" "${VERSION}"
  ; 프로그램 추가/제거 목록에도 아이콘이 뜨도록
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniIDE" \
             "DisplayIcon" "$INSTDIR\bin\miniide.exe,0"

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Mini IDE\Mini IDE.lnk"
  RMDir "$SMPROGRAMS\Mini IDE"
  Delete "$DESKTOP\Mini IDE.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniIDE"
  DeleteRegKey HKLM "Software\MiniIDE"
  RMDir /r "$INSTDIR"
SectionEnd
