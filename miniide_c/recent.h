/* 최근 파일 기록 — 열었던 파일 목록을 설정 폴더에 저장/조회한다. */
#ifndef RECENT_H
#define RECENT_H

#include <glib.h>

/* 파일을 열었을 때 호출 — 목록 맨 앞에 추가(중복 제거, 최대 10개)하고 저장한다. */
void recent_add(const char *file_path);

/* 최근 파일 경로 목록(새 항목이 앞). NULL 종료 배열이며 g_strfreev 로 해제한다.
 * 더 이상 존재하지 않는 파일은 자동으로 걸러진다. */
char **recent_get(void);

#endif
