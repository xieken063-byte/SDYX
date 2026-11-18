/*
 * 剧情：主线章节与随机奇遇。数据在 story_data.c，逻辑在 sim_story.c。
 * 正文里 @ 换成主角的名字，# 换成第二个人的名字，| 分页。
 */
#ifndef STORY_H
#define STORY_H

#include "sim.h"

/* 选项的效果 */
enum {
    X_NONE,
    X_FOOD, X_WOOD, X_STONE,   /* 库存 ± arg */
    X_FAITH, X_FEAR,           /* 信仰 / 恐惧 ± arg */
    X_MOOD, X_MOOD1, X_MOOD2,  /* 全岛 / 主角 / 第二人的心情 ± arg×10 */
    X_HP1, X_HP2,              /* 主角 / 第二人生命 ± arg×10（不会因此丧命） */
    X_SKILL1, X_SKILL2,        /* 主角 / 第二人最拿手的本事 + arg */
    X_SKILLK,                  /* 岛上每个孩子随机一项本事 + arg */
    X_SICK1, X_CURE,           /* 主角染病 / 全岛痊愈 */
    X_GUEST,                   /* 来一位新居民（arg：0 随机 1 男 2 女） */
    X_BOAR, X_RABBIT, X_WOLF,  /* 来 arg 头野猪 / 兔子 / 普通的狼 */
    X_SLAY,                    /* 岛上的狼都没了 */
    X_FISH,                    /* 鱼群 ± arg */
    X_FUN,                     /* 全岛娱乐 + arg×10 */
    X_DREAM1,                  /* 主角实现梦想 */
    X_FEUD, X_BOND,            /* 主角与第二人：怨气 / 好感 ± arg */
    X_FLAG,                    /* 记下剧情标记 arg */
    X_CROPS,                   /* 所有庄稼长势 + arg */
    X_FOREST,                  /* 种下 arg 棵树苗 */
    X_FEAST,                   /* 当晚办一场庆典 */
    X_RAIN, X_SUNNY,           /* 下雨 / 放晴 */
    X_SKY,                     /* 天象（arg = SKY_*） */
    X_PIG, X_DOG,              /* 牧场添 arg 头猪 / 来一条狗 */
    X_TRADE,                   /* 得到商船货物（arg = TD_* 位） */
    X_HURTALL,                 /* 屋外的人生命 − arg×10（不会因此丧命） */
    X_ENERGY, X_SOCIAL,        /* 全岛体力 / 社交补满 */
    X_BLESS1,                  /* 赐福主角 */
    X_ENDING,                  /* 结局（arg = 1 神之岛 2 敬畏之岛 3 自由之岛） */
    X_ADOPT,                   /* 主角收养一个婴儿 */
    X_MARRY,                   /* 主角和第二人结为伴侣 */
    X_NOLOVE,                  /* 主角和第二人再也不敢相爱 */
    X_COUNT
};

/* 触发条件 */
enum {
    K_ANY, K_SEASON, K_KID, K_ELDER, K_COUPLE, K_SICK, K_FAITH, K_FEAR, K_LOWFAITH, K_DOCK, K_PEN, K_FLAG, K_NOFLAG,
    K_HUNGRY, K_RICH, K_WOLVES, K_POP, K_SINGLE, K_STATUE, K_SHIP, K_FEUD, K_HOUSE, K_FARM, K_SHRINE, K_NOWOLF,
    K_COUNT
};

/* 挑主角（以及第二个人）的规则 */
enum {
    W_NONE, W_ANY, W_ADULT, W_KID, W_TEEN, W_ELDER, W_MAN, W_WOMAN, W_SINGLE, W_PARENT, W_SICK,
    W_HUNT, W_FARM, W_CRAFT, W_FISH, W_BRAVE, W_TIMID, W_KIND, W_LAZY, W_DILIGENT,
    W_PARTNER, W_CHILD_OF, W_FRIEND, W_FOE, W_OPPOSITE, W_LOVER, WHO_COUNT
};

/* 剧情标记（主线用） */
enum {
    F_GENTLE, F_STERN, F_SILENT, F_SEEDS, F_STONE, F_CAVE, F_MURAL, F_STRANGER, F_DROVE, F_TEMPLE, F_FREE, F_UNITED,
    F_SAIL, F_TRUTH, F_PATH_FAITH, F_PATH_FEAR, F_PATH_FREE, F_ENDED
};

typedef struct {
    uint8_t op;
    int8_t arg;
} sop_t;

typedef struct {
    const char *label;  /* 选项文字 */
    const char *result; /* 选了之后发生了什么 */
    sop_t ops[4];
    uint8_t risk;       /* 失败的概率 % */
    const char *fail;   /* 失败时发生了什么 */
    sop_t fops[3];
} sopt_t;

typedef struct {
    const char *title;
    const char *text;
    uint8_t chap;       /* 主线第几章（随机奇遇为 0） */
    uint8_t who, who2;
    uint8_t cond, carg;
    uint8_t era;        /* 最早的时代 */
    uint16_t day;       /* 最早的一天 */
    uint8_t weight;     /* 随机奇遇的权重 */
    sopt_t opt[3];
} story_t;

#define O(op, arg) {op, arg}
#define L(...) {__VA_ARGS__}
#define OPT(l, r, ops) {l, r, ops, 0, 0, {{0, 0}}}
#define RISK(l, r, ops, p, f, fops) {l, r, ops, p, f, fops}

extern const story_t STORY_MAIN[];
extern const int STORY_MAIN_N;
extern const story_t STORY_TALE[];
extern const int STORY_TALE_N;

#endif
