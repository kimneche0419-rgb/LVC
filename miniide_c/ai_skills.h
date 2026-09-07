/* AI 스킬 표 — 코딩/기획/학습/자동화 분야별 프롬프트 스킬.
 * 각 스킬은 이름·입력 방식·프롬프트 형식만 정의하고, 실행(요청 전송)은 ai_panel 이 맡는다.
 * 프롬프트 기법은 검증된 오픈소스 프롬프트 라이브러리(awesome-chatgpt-prompts, CC0)의
 * 역할 부여·단계별 사고·출력 형식 고정 패턴을 로컬 DeepSeek 에 맞게 다듣다듬은 것이다.
 */
#ifndef AI_SKILLS_H
#define AI_SKILLS_H

#include "ui_lang.h"

/* 스킬 입력 방식 */
typedef enum {
    AI_SKILL_INPUT_CODE,   /* 에디터에 열린 코드를 대상으로 동작 */
    AI_SKILL_INPUT_TEXT    /* 입력창에 적은 요청을 대상으로 동작 */
} AiSkillInput;

/* 스킬 분야 — 메뉴의 하위 메뉴와 1:1 대응 */
typedef enum {
    AI_SKILL_CAT_QUALITY,  /* 코딩 품질 */
    AI_SKILL_CAT_PLAN,     /* 기획·계획 */
    AI_SKILL_CAT_LEARN,    /* 학습·설명 */
    AI_SKILL_CAT_TOOLS,    /* 자동화·도구 */
    AI_SKILL_CAT_COUNT
} AiSkillCat;

/* 스킬 하나 — 이름/프롬프트는 언어 전환 대응을 위해 UiStrId 로 참조한다.
 * prompt 는 printf 형식이며 %s 에 코드(CODE형) 또는 요청(TEXT형)이 들어간다. */
typedef struct {
    UiStrId name;
    AiSkillInput input;
    UiStrId prompt;
} AiSkill;

int            ai_skill_cat_count(void);
const char    *ai_skill_cat_name(AiSkillCat cat);  /* 현재 언어의 분야명 */
int            ai_skill_count(AiSkillCat cat);
const AiSkill *ai_skill_at(AiSkillCat cat, int idx);

#endif
