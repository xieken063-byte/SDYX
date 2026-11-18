/*
 * 《造物主：孤岛》模拟核心 —— 纯 C、无硬件依赖，可在主机上编译测试。
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>

#define MAP_W 30
#define MAP_H 26
#define MAP_N (MAP_W * MAP_H)
#define TILE 8

#define MAX_RES 8
#define MAX_ANI 20
#define TREE_N 200 /* 家谱登记的人数上限（环形，满了覆盖最早的人） */
#define MAX_DEAD 8

#define TICKS_PER_DAY 600 /* 1x 速度下 1 tick = 100 ms，1 天 = 现实 1 分钟 */
#define TICKS_PER_HOUR (TICKS_PER_DAY / 24)
#define DAYS_PER_YEAR 4 /* 4 个游戏日记为 1 岁 */

/* 年龄段（岁） */
#define AGE_CHILD 3   /* 3 岁前是婴儿，待在家里由大人照顾 */
#define AGE_WORK 12   /* 12 岁开始干活 */
#define AGE_ADULT 16  /* 16 岁可以结为伴侣 */
#define AGE_ELDER 55  /* 55 岁后不再干重活 */
#define AGE_OLD 60    /* 60 岁起每天有概率老死，年纪越大概率越高 */

/* 每格 16 位：低 4 位地形，中 5 位地物，高 7 位附加值（作物长势 / 房屋耐久 / 燃烧剩余时间） */
enum { T_SEA, T_SHOAL, T_SAND, T_GRASS, T_ROCK, T_RIVER, T_LAKE, T_ASH, T_LAVA };
enum {
    O_NONE, O_SAPLING, O_YOUNG, O_TREE, O_STUMP, O_BUSH, O_BERRY, O_FLOWER,
    O_FIRE, O_STORE, O_GRAVE, O_BURNT,
    O_FARM, O_SITE, O_HOUSE1, O_HOUSE2, O_RUIN, O_FLAME, O_SHRINE,
    O_WELL, O_GRANARY, O_DOCK, O_TOWER, O_FENCE,
    O_PEN, O_STATUE, O_SHIP /* 牧场、神像、码头边的大船 */
};
/* 季节：一年四天，每天一季 */
enum { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER };
/* 时代 */
enum { ERA_STONE, ERA_BRONZE, ERA_IRON, ERA_SAIL, ERA_COUNT };
/* 技能：决定职业 */
enum { SK_HUNT, SK_FARM, SK_CRAFT, SK_FISH, SK_COUNT };
/* 一生中的大事 */
enum {
    EV_NONE, EV_BORN, EV_ARRIVE, EV_MARRY, EV_CHILD, EV_HOUSE, EV_MASTER, EV_INHERIT,
    EV_BLESSED, EV_WOLF_KILL, EV_BUILD, EV_ERA, EV_LOST_PARTNER, EV_LOST_PARENT, EV_LOST_CHILD, EV_HURT,
    EV_ADULT, EV_DREAM, EV_FEUD, EV_MAKEUP, EV_VOYAGE, EV_FIRE, EV_COUNT
};
#define EV_MAX 5 /* 每人记最近 5 件大事 */
enum { A_BOAR, A_RABBIT, A_WOLF, A_DOG, A_KINDS };
#define TERR(t) ((t) & 15)
#define OBJ(t) (((t) >> 4) & 31)
#define VAL(t) ((t) >> 9)
#define MKT(te, o, v) ((uint16_t)((te) | ((o) << 4) | ((v) << 9)))
#define MKTILE(te, o) MKT(te, o, 0)
/* 碎石：地震、火山、神力造出来的石头（附加值为 1），会风化、能被清走；岛上原有的山附加值为 0，永久存在 */
#define IS_RUBBLE(t) (TERR(t) == T_ROCK && VAL(t) == 1)
/* 神迹留下的地形：火山口的山（岩石附加值 2）、神造的湖（湖附加值 1），"复原大地"会把它们变回草地 */
#define GOD_MADE(t) ((TERR(t) == T_ROCK && VAL(t) == 2) || (TERR(t) == T_LAKE && VAL(t) == 1))

enum { TR_DILIGENT, TR_LAZY, TR_BRAVE, TR_TIMID, TR_KIND, TR_COUNT };
enum { S_IDLE, S_MOVE, S_ACT, S_SLEEP };
enum {
    G_NONE, G_WANDER, G_REST, G_EAT_CAMP, G_EAT_BERRY, G_BERRY,
    G_CHOP, G_HUNT, G_FISH, G_HAUL, G_SLEEP,
    G_FLEE, G_CHAT, G_HELP, G_TEND, G_BUILD, G_UPGRADE, G_REPAIR,
    G_TILL, G_HARVEST, G_MINE, G_SHELTER, G_SHRINE, G_PRAY, G_COWER, G_FEAST, G_FIGHT, G_CONSTRUCT,
    G_SWIM, G_STROLL, G_GAZE, G_DANCE, G_NAP, G_FLOWER, G_GIFT, G_DATE, G_SNOW, G_PLAY, G_STORY, G_LISTEN,
    G_FETCH, G_DOUSE, G_CLEAR, G_QUARREL, G_MAKEUP, G_WEDDING, G_MOURN, G_TRADE, G_FEEDPEN, G_BOARD
};
enum { WISH_NONE, WISH_FOOD, WISH_HEAL, WISH_CALM, WISH_WOLF, WISH_WOOD, WISH_COUNT }; /* 岛民的祈求 */
enum { C_NONE, C_FOOD, C_WOOD, C_STONE, C_FLOWER, C_WATER, C_GOODS };
enum { W_SUNNY, W_CLOUDY, W_RAIN, W_STORM, W_THUNDER, W_WIND, W_SNOW, W_COUNT };
enum { D_STARVE, D_AGE, D_BOLT, D_FIRE, D_FLOOD, D_LAVA, D_QUAKE, D_PLAGUE, D_COLD, D_WOLF, D_COUNT };
enum { SFX_NONE, SFX_BIRTH, SFX_DEATH, SFX_CHIME, SFX_BLESS, SFX_BLIP, SFX_THUNDER, SFX_RUMBLE, SFX_ALARM, SFX_WOLF, SFX_COUNT };

/* 神力，按类别排列（长按上/下可以整类跳过） */
enum {
    P_INSPECT, P_TREE, P_RECORDS,
    P_FOREST, P_BEAST, P_RABBIT, P_WOLF, P_FISH, P_FOOD, P_HUMAN, P_GENESIS, P_ORE, P_WATER, P_CROP,
    P_HEAL, P_GROW, P_CALM, P_SLAY, P_LEVEL, P_RESTORE,
    P_W_SUNNY, P_W_CLOUDY, P_W_RAIN, P_W_STORM, P_W_THUNDER, P_W_WIND, P_W_SNOW, P_METEOR, P_RAINBOW, P_ECLIPSE,
    P_BOLT, P_WILDFIRE, P_FLOOD, P_QUAKE, P_VOLCANO, P_PLAGUE,
    P_SPEED, P_SOUND, P_VOICE, P_ZOOM, P_NEWWORLD, P_COUNT
};

#define NO_ID 0xFFFF
#define NO_SLOT 0xFF

typedef struct {
    uint8_t alive;
    uint8_t name; /* 名字池下标 */
    uint8_t sex;  /* 0 男 1 女 */
    uint8_t trait;
    uint16_t uid;
    uint16_t age_days;
    uint16_t hp, food, energy, mood; /* 0..1000 */
    uint8_t tx, ty, nx, ny;          /* 当前格 / 下一格 */
    uint8_t step;                    /* 0..TILE 的格间进度 */
    uint8_t face;                    /* 1 = 朝左 */
    uint8_t state, goal;
    uint8_t gx, gy;
    uint8_t tgt; /* 狩猎目标 / 聊天、照顾、救助对象的下标 */
    uint8_t carry, carry_n;
    uint8_t partner;       /* 伴侣的下标，NO_SLOT = 无 */
    uint16_t timer;
    uint16_t home;         /* 房屋所在格，NO_ID = 无家可归 */
    uint16_t father, mother; /* 父母的 uid，NO_ID = 初代 */
    uint8_t fname, mname;  /* 父母的名字下标（父母去世后仍可查看） */
    uint8_t gen;           /* 第几代，初代为 1 */
    uint8_t kids;
    uint8_t preg;          /* 距离分娩的天数，0 = 未怀孕 */
    uint8_t sick;          /* 染病剩余天数 */
    uint8_t immune;        /* 病愈后的免疫天数 */
    uint8_t hurt_cd;       /* 受伤消息冷却 */
    uint16_t last_birth;   /* 上次生育是第几天 */
    uint8_t skill[SK_COUNT]; /* 狩猎 务农 手艺 捕鱼，0..100 */
    uint8_t ev[EV_MAX];      /* 一生大事（环形，ev_n 为写入总数） */
    uint8_t ev_arg[EV_MAX];  /* 事件参数：相关的人（性别<<7 | 名字）、技能或时代 */
    uint16_t ev_day[EV_MAX];
    uint8_t ev_n;
    uint8_t dream;          /* 一生的梦想（DR_*） */
    uint8_t flags;          /* RF_* */
    uint8_t away;           /* 出海远航，还有几天回来；0 = 在岛上 */
    uint16_t fun, social; /* 娱乐、社交两项需求，0..1000，越低越想要 */
    uint8_t pad4[2];
} resident_t;

/* 梦想 */
enum { DR_NONE, DR_MASTER, DR_BIGHOUSE, DR_KIDS, DR_FISH, DR_VOYAGE, DR_OLD, DR_MIRACLE, DR_FRIENDS, DR_COUNT };
enum { RF_DREAM = 1, RF_ADULT = 2 };
/* 仪式 */
enum { CER_NONE, CER_WEDDING, CER_FUNERAL, CER_NEWYEAR, CER_ADULT };
/* 商船带来的东西 */
enum { TD_SEEDS = 1, TD_TOOLS = 2, TD_LIVESTOCK = 4, TD_HERBS = 8, TD_BOOKS = 16, TD_DOG = 32 };
/* 天象 */
enum { SKY_NONE, SKY_METEOR, SKY_RAINBOW, SKY_ECLIPSE };

typedef struct {
    uint8_t alive, kind;
    uint8_t tx, ty, nx, ny;
    uint8_t step, face;
    uint8_t held; /* 被哪个居民缠住（下标+1） */
    uint8_t flee;
    uint8_t hunger; /* 狼：多少小时没吃东西 */
    uint8_t fierce; /* 恶狼：神放出来的，专门猎杀人 */
    uint8_t owner;  /* 狗：主人的下标 */
    uint8_t pad;
} animal_t;

/* 家谱里的一条记录。在世者的年龄从居民数据里取，这里只记去世时的 */
typedef struct {
    uint16_t uid; /* 0 = 空 */
    uint16_t father, mother;
    uint16_t born; /* 出生在第几天 */
    uint8_t name, sex;
    uint8_t age;   /* 享年；0xFF = 在世 */
    uint8_t cause;
    uint8_t gen, pad;
} person_t;

typedef struct {
    uint8_t name, sex, age, gen, cause, used;
    uint16_t day;
} dead_t;

/* 整个世界就是一个无指针的 POD，直接整块存档 */
typedef struct {
    uint32_t rng;
    uint32_t tick;
    uint16_t tiles[MAP_N];
    resident_t res[MAX_RES];
    animal_t ani[MAX_ANI];
    uint8_t rel[MAX_RES][MAX_RES]; /* 好感度 0..100 */
    dead_t dead[MAX_DEAD];         /* 逝者名录（环形） */
    uint8_t camp_x, camp_y;
    uint8_t fire_lit;
    uint8_t speed_idx;
    uint8_t paused;
    uint8_t weather, weather_h; /* 当前天气与剩余小时数 */
    uint8_t flood;              /* 洪水等级 0..3：离水 N 格以内的陆地被淹 */
    uint8_t storm_h;            /* 暴雨已持续的小时数 */
    uint8_t volcano_h;          /* 火山还要喷发几小时 */
    uint8_t dead_n;             /* 写入逝者名录的总数（低 8 位） */
    uint8_t max_gen;
    uint16_t crater;            /* 火山口所在格 */
    uint16_t fires, lava;       /* 正在燃烧 / 岩浆格数 */
    uint16_t food, wood, stone, fish;
    uint16_t next_uid;
    uint16_t deaths, births;
    uint16_t marriages, built;
    uint16_t shrine;      /* 祭石所在格，NO_ID = 没有 */
    uint8_t faith, fear;  /* 岛民对神的信仰与恐惧，0..100 */
    uint8_t sound_on, voice_on;
    uint8_t wish, wish_h;  /* 岛民正在祈求什么、还等多少小时 */
    uint8_t fest_h, zoom;  /* 庆典还剩几小时；镜头是否拉近 */
    uint16_t last_fest;    /* 上次庆典是第几天 */
    uint8_t era;           /* 时代 */
    uint8_t proj_type;     /* 正在修建的公共建筑（O_WELL 等），0 = 没有 */
    uint16_t proj_idx;     /* 工地所在格 */
    uint8_t proj_prog, proj_need; /* 已完成 / 需要的工时 */
    uint8_t wild_wx, pad3;        /* 当前的坏天气是大自然的（伤害减半），不是神降的 */
    uint8_t feud[MAX_RES][MAX_RES]; /* 怨气 0..100 */
    uint32_t ach;                   /* 已达成的成就（位） */
    uint8_t cer, cer_h;             /* 正在举行的仪式与剩余小时 */
    uint8_t cer_a, cer_b;           /* 仪式的主角（下标） */
    uint16_t cer_idx;               /* 仪式地点 */
    uint8_t ship_h;                 /* 商船还停几小时；0 = 没有 */
    uint8_t ship_x, ship_y;         /* 商船停在哪 */
    uint8_t trade;                  /* 换到过的东西（TD_*） */
    uint8_t trades;                 /* 交易次数 */
    uint8_t livestock;              /* 牧场里的猪 */
    uint8_t voyage_d;               /* 远航还要几天回来 */
    uint8_t voyages;                /* 出海次数 */
    uint16_t statue;                /* 神像所在格 */
    uint8_t sky, sky_h;             /* 天象与剩余小时 */
    uint16_t last_ship;             /* 上次商船来是第几天 */
    uint16_t fires_put;             /* 岛民扑灭的火 */
    uint16_t quarrels;              /* 吵过的架 */
    uint8_t dreams_met;             /* 实现了梦想的人数 */
    uint8_t reborn;                 /* 灭绝过、又由神降下先民 */
    uint8_t ship_deals;             /* 这趟商船做成了几笔买卖 */
    uint16_t power_used[P_COUNT];
} world_t;

/* 事件日志与特效：不进存档 */
#define LOG_N 4
#define LOG_LEN 72
typedef struct {
    char msg[LOG_N][LOG_LEN];
    uint32_t seq; /* 写入总数；最新一条是 msg[(seq-1)%LOG_N] */
} sim_log_t;

enum { FX_NONE, FX_SPARK, FX_BOLT, FX_HEAL, FX_QUAKE, FX_GLOW, FX_DOOM, FX_NOTE, FX_HEART, FX_SPLASH, FX_FIREWORK };
#define FX_N 12
typedef struct { uint8_t type, x, y, t; } sim_fx_t;

extern sim_log_t g_log;
extern sim_fx_t g_fx[FX_N];
extern person_t g_tree[TREE_N]; /* 家谱：单独存档 */
extern bool g_tree_dirty;

const person_t *sim_person(uint16_t uid);
void sim_tree_put(const world_t *w, const resident_t *r); /* 登记或更新一个人 */
/* 列出某人的亲属：role 0 父 1 母 2 配偶 3 子女；返回个数 */
int sim_relatives(const world_t *w, uint16_t uid, uint16_t *out, uint8_t *role, int max);
int sim_descendants(uint16_t uid, int *living); /* 后代总数；*living = 其中在世的 */
int sim_tree_count(void);
const resident_t *sim_resident_by_uid(const world_t *w, uint16_t uid);
int sim_kind_count(const world_t *w, int kind);
int sim_sfx_pop(void); /* 取出一个待播放的音效，没有则返回 SFX_NONE */

void sim_new(world_t *w, uint32_t seed);
void sim_refresh(world_t *w); /* 读档后调用：重建不进存档的派生数据 */
void sim_tick(world_t *w);
void sim_cast(world_t *w, int power);
void sim_bless_one(world_t *w, int i); /* 对一个人赐福 */
void sim_smite_one(world_t *w, int i); /* 对一个人降下雷罚 */
const char *sim_wish_name(int wish);
void sim_fx_step(void); /* 每帧调用一次，推进特效 */

int sim_pop(const world_t *w);
int sim_animals(const world_t *w);
int sim_day(const world_t *w);  /* 从 1 开始 */
int sim_year(const world_t *w); /* 从 1 开始，四天一年 */
int sim_season(const world_t *w);
const char *sim_season_name(int season);
const char *sim_era_name(int era);
int sim_prof(const resident_t *r);                 /* 职业 = 最高的技能，孩子返回 -1 */
const char *sim_prof_name(const resident_t *r);    /* "猎人" "农夫"…… */
const char *sim_skill_level(int skill);            /* "学徒" "熟练" "精通" "大师" */
const char *sim_building_name(int obj);
void sim_thought(const world_t *w, const resident_t *r, char *buf, int n); /* 此刻的心声 */
void sim_want(const world_t *w, const resident_t *r, char *buf, int n);
#define ACH_N 24
const char *sim_ach_name(int k);  /* 成就名 */
const char *sim_ach_how(int k);   /* 成就的达成条件 */
int sim_ach_count(const world_t *w);
const char *sim_dream_name(int d);
const char *sim_trade_name(int bit);    /* 心愿 */
void sim_event_text(const resident_t *r, int k, char *buf, int n);         /* 第 k 件大事（0 = 最近） */
int sim_count_obj(const world_t *w, int obj);
int sim_hour(const world_t *w); /* 0..23 */
int sim_speed(const world_t *w);
bool sim_is_night(const world_t *w);
bool sim_flooded(const world_t *w, int idx);
int sim_age(const resident_t *r); /* 岁 */
bool sim_in_house(const world_t *w, const resident_t *r);

const char *sim_name(const resident_t *r);
const char *sim_name_of(int sex, int name);
const char *sim_trait(const resident_t *r);
const char *sim_status(const world_t *w, const resident_t *r);
const char *sim_power_name(int power);
const char *sim_power_cat(int power);
int sim_power_cat_id(int power);
const char *sim_weather_name(int weather);
const char *sim_cause_name(int cause);

#endif
