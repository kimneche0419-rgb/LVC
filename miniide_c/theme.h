/* 다크 테마 모듈 — VS Code 스타일 색상을 앱 전체에 적용한다. */
#ifndef THEME_H
#define THEME_H

/* gtk_init 직후 한 번 호출하면 화면(screen) 단위로 CSS 테마가 적용된다. */
void theme_apply(void);

#endif
