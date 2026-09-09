#include "ai_skills.h"

/* 분야별 스킬 목록 — 순서가 곧 메뉴 표시 순서다.
 * 프롬프트 공통 규칙: (1) 여기서는 작업 지시만 준다(별도의 시스템 프롬프트는 없음)
 * (2) 단계를 번호로 명시해 단계별 사고를 유도한다
 * (3) 결과물은 ``` 코드 블록 형식을 강제한다. */
static const AiSkill QUALITY_SKILLS[] = {
    {STR_SKILL_REVIEW,   AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_REVIEW},
    {STR_SKILL_FIX,      AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_FIX},
    {STR_SKILL_REFACTOR, AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_REFACTOR},
    {STR_SKILL_PERF,     AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_PERF},
    {STR_SKILL_TEST,     AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_TEST},
};

static const AiSkill PLAN_SKILLS[] = {
    {STR_SKILL_IMPL_PLAN,       AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_IMPL_PLAN},
    {STR_SKILL_REQ,             AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_REQ},
    {STR_SKILL_STRUCT,          AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_STRUCT},
};

static const AiSkill LEARN_SKILLS[] = {
    {STR_SKILL_EXPLAIN_CODE,    AI_SKILL_INPUT_CODE, STR_PROMPT_SKILL_EXPLAIN_CODE},
    {STR_SKILL_EXPLAIN_CONCEPT, AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_EXPLAIN_CONCEPT},
};

static const AiSkill TOOLS_SKILLS[] = {
    {STR_SKILL_REGEX, AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_REGEX},
    {STR_SKILL_SHELL, AI_SKILL_INPUT_TEXT, STR_PROMPT_SKILL_SHELL},
};

static const AiSkill *const CAT_TABLE[AI_SKILL_CAT_COUNT] = {
    QUALITY_SKILLS, PLAN_SKILLS, LEARN_SKILLS, TOOLS_SKILLS,
};
static const int CAT_SIZES[AI_SKILL_CAT_COUNT] = {5, 3, 2, 2};
static const UiStrId CAT_NAMES[AI_SKILL_CAT_COUNT] = {
    STR_SKILL_CAT_QUALITY, STR_SKILL_CAT_PLAN, STR_SKILL_CAT_LEARN, STR_SKILL_CAT_TOOLS,
};

int ai_skill_cat_count(void) { return AI_SKILL_CAT_COUNT; }

const char *ai_skill_cat_name(AiSkillCat cat) {
    if (cat < 0 || cat >= AI_SKILL_CAT_COUNT) return "";
    return tr(CAT_NAMES[cat]);
}

int ai_skill_count(AiSkillCat cat) {
    if (cat < 0 || cat >= AI_SKILL_CAT_COUNT) return 0;
    return CAT_SIZES[cat];
}

const AiSkill *ai_skill_at(AiSkillCat cat, int idx) {
    if (cat < 0 || cat >= AI_SKILL_CAT_COUNT) return NULL;
    if (idx < 0 || idx >= CAT_SIZES[cat]) return NULL;
    return &CAT_TABLE[cat][idx];
}
