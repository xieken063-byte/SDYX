/*
 * 《造物主：孤岛》模拟核心（一）：基础工具、寻路、居民的需求与状态机。
 * 世界侧（地图生成、生态、天气、灾害、神力）在 sim_world.c。
 * 只依赖 C 标准库。所有随机数来自 world_t.rng，因此同一存档可复现。
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sim_int.h"

sim_log_t g_log;
sim_fx_t g_fx[FX_N];
person_t g_tree[TREE_N];
bool g_tree_dirty;

static uint8_t s_sfx_q[8];
static uint8_t s_sfx_head, s_sfx_n;

void sim_sfx(int id)
{
    for (int k = 0; k < s_sfx_n; k++) /* 同一种音效不排两次 */
        if (s_sfx_q[(s_sfx_head + k) % 8] == id) return;
    if (s_sfx_n >= 8) return;
    s_sfx_q[(s_sfx_head + s_sfx_n) % 8] = (uint8_t)id;
    s_sfx_n++;
}
int sim_sfx_pop(void)
{
    if (!s_sfx_n) return SFX_NONE;
    int id = s_sfx_q[s_sfx_head];
    s_sfx_head = (uint8_t)((s_sfx_head + 1) % 8);
    s_sfx_n--;
    return id;
}

#define NAME_N 20
static const char *const NAMES_M[NAME_N] = {"阿山", "大石", "小虎", "青松", "铁牛", "阿海", "柱子",
                                            "木生", "石头", "长风", "阿福", "大川", "小满", "云生",
                                            "阿木", "春生", "二牛", "阿树", "水生", "冬青"};
static const char *const NAMES_F[NAME_N] = {"阿月", "小禾", "春芽", "青杏", "云朵", "阿兰", "小梅",
                                            "桃子", "秋叶", "星儿", "阿花", "小雨", "杏儿", "月牙",
                                            "阿朵", "小溪", "梨花", "青禾", "阿霞", "小鹿"};
static const char *const TRAITS[TR_COUNT] = {"勤劳", "懒散", "勇敢", "胆小", "善良"};
static const char *const WEATHERS[W_COUNT] = {"晴", "阴", "小雨", "暴雨", "雷电", "大风", "暴风雪"};
static const char *const CAUSES[D_COUNT] = {"饿死", "老死", "雷击", "火灾", "洪水", "岩浆", "地震", "瘟疫", "冻死", "狼袭"};
static const char *const POWERS[P_COUNT] = {
    "观察居民", "家谱", "岛屿纪事",
    "降下森林", "创造野猪", "创造兔子", "放出恶狼", "召来鱼群", "赐予食物", "创造居民", "降下先民", "创造矿石", "创造水源", "创造庄稼",
    "治愈众生", "万物生长", "平息灾祸", "消灭狼群", "清除乱石", "复原大地",
    "晴天", "阴天", "小雨", "暴雨", "雷电", "大风", "暴风雪", "流星雨", "彩虹", "日食",
    "天降雷火", "森林大火", "洪水", "地震", "火山喷发", "瘟疫",
    "时间流速", "音效", "声控神力", "镜头", "重造新岛"};
static const char *const WISHES[WISH_COUNT] = {"", "食物", "治病疗伤", "平息灾祸", "赶走狼群", "木柴"};
const char *sim_wish_name(int wish) { return WISHES[wish % WISH_COUNT]; }
static const char *const SEASONS[4] = {"春", "夏", "秋", "冬"};
static const char *const ERAS[ERA_COUNT] = {"石器", "青铜", "铁器", "航海"};
static const char *const PROFS[SK_COUNT] = {"猎人", "农夫", "工匠", "渔夫"};
static const char *const SKILLS[SK_COUNT] = {"狩猎", "种田", "手艺", "捕鱼"};
const char *sim_season_name(int season) { return SEASONS[season & 3]; }
const char *sim_era_name(int era) { return ERAS[era % ERA_COUNT]; }
static const char *const CATS[6] = {"观察", "创造", "祝福", "天气", "灾害", "设置"};

const int8_t SIM_DX[4] = {1, -1, 0, 0};
const int8_t SIM_DY[4] = {0, 0, 1, -1};
#define DX SIM_DX
#define DY SIM_DY

uint8_t sim_wdist[MAP_N];
uint8_t sim_danger[MAP_N];
uint8_t sim_par[MAP_N];
int sim_farms, sim_houses;
int sim_fierce;
int sim_rubble;
int sim_towers;
static bool s_hazard;

/* 营地周围的落脚点（篝火在 0,0，仓库在 1,0） */
static const int8_t SPOT[MAX_RES][2] = {{-1, 1}, {0, 1}, {1, 1}, {2, 1}, {-1, 0}, {2, 0}, {-1, -1}, {0, -1}};

/* ---------- 基础工具 ---------- */

uint32_t sim_rnd(world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return w->rng = x;
}
int sim_rn(world_t *w, int n) { return (int)(sim_rnd(w) % (uint32_t)n); }
#define rn sim_rn
static int iabs(int v) { return v < 0 ? -v : v; }
int sim_cheb(int ax, int ay, int bx, int by)
{
    int dx = iabs(ax - bx), dy = iabs(ay - by);
    return dx > dy ? dx : dy;
}
#define cheb sim_cheb
bool sim_inmap(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
#define inmap sim_inmap

void sim_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log.msg[g_log.seq % LOG_N], LOG_LEN, fmt, ap);
    va_end(ap);
    g_log.seq++;
}

void sim_fx_add(int type, int x, int y, int dur)
{
    int best = 0;
    for (int i = 0; i < FX_N; i++) {
        if (g_fx[i].t == 0) { best = i; break; }
        if (g_fx[i].t < g_fx[best].t) best = i;
    }
    g_fx[best] = (sim_fx_t){(uint8_t)type, (uint8_t)x, (uint8_t)y, (uint8_t)dur};
}
void sim_fx_step(void)
{
    for (int i = 0; i < FX_N; i++)
        if (g_fx[i].t) g_fx[i].t--;
}

bool sim_is_water(int terr) { return terr == T_SHOAL || terr == T_LAKE || terr == T_RIVER; }
bool sim_is_house(int obj) { return obj == O_HOUSE1 || obj == O_HOUSE2; }

bool sim_walkable(const world_t *w, int idx)
{
    int t = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    if (t != T_SAND && t != T_GRASS && t != T_RIVER && t != T_ASH) return false;
    return o != O_FIRE && o != O_STORE && o != O_FLAME && o != O_WELL && o != O_GRANARY && o != O_TOWER && o != O_PEN &&
           o != O_STATUE && o != O_SHIP;
}

bool sim_flooded(const world_t *w, int idx)
{
    if (!w->flood) return false;
    int t = TERR(w->tiles[idx]);
    return (t == T_SAND || t == T_GRASS || t == T_ASH) && sim_wdist[idx] <= w->flood;
}

bool sim_occupied(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && ((w->res[i].tx == x && w->res[i].ty == y) || (w->res[i].nx == x && w->res[i].ny == y)))
            return true;
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].alive && w->ani[k].tx == x && w->ani[k].ty == y) return true;
    return false;
}

int sim_pop(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < MAX_RES; i++) n += w->res[i].alive;
    return n;
}
int sim_animals(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < MAX_ANI; i++) n += w->ani[i].alive;
    return n;
}
int sim_kind_count(const world_t *w, int kind)
{
    int n = 0;
    for (int i = 0; i < MAX_ANI; i++) n += w->ani[i].alive && w->ani[i].kind == kind;
    return n;
}

/* ---------- 家谱 ---------- */

const person_t *sim_person(uint16_t uid)
{
    if (uid == 0 || uid == NO_ID) return NULL;
    const person_t *p = &g_tree[uid % TREE_N];
    return p->uid == uid ? p : NULL;
}
const resident_t *sim_resident_by_uid(const world_t *w, uint16_t uid)
{
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && w->res[i].uid == uid) return &w->res[i];
    return NULL;
}
void sim_tree_put(const world_t *w, const resident_t *r)
{
    person_t *p = &g_tree[r->uid % TREE_N];
    if (p->uid != r->uid) {
        memset(p, 0, sizeof *p);
        p->uid = r->uid;
        p->born = (uint16_t)(sim_day(w) > r->age_days ? sim_day(w) - r->age_days : 0);
        p->age = 0xFF;
    }
    p->father = r->father;
    p->mother = r->mother;
    p->name = r->name;
    p->sex = r->sex;
    p->gen = r->gen;
    g_tree_dirty = true;
}
int sim_tree_count(void)
{
    int n = 0;
    for (int i = 0; i < TREE_N; i++) n += g_tree[i].uid != 0;
    return n;
}
int sim_relatives(const world_t *w, uint16_t uid, uint16_t *out, uint8_t *role, int max)
{
    const person_t *p = sim_person(uid);
    int n = 0;
    if (!p) return 0;
    if (sim_person(p->father) && n < max) out[n] = p->father, role[n++] = 0;
    if (sim_person(p->mother) && n < max) out[n] = p->mother, role[n++] = 1;
    /* 配偶：在世的看现在的伴侣，否则看孩子的另一位父母 */
    uint16_t sp = NO_ID;
    const resident_t *r = sim_resident_by_uid(w, uid);
    if (r && r->partner != NO_SLOT && w->res[r->partner].alive) sp = w->res[r->partner].uid;
    for (int i = 0; i < TREE_N && sp == NO_ID; i++) {
        const person_t *c = &g_tree[i];
        if (!c->uid) continue;
        if (c->father == uid && sim_person(c->mother)) sp = c->mother;
        else if (c->mother == uid && sim_person(c->father)) sp = c->father;
    }
    if (sp != NO_ID && sim_person(sp) && n < max) out[n] = sp, role[n++] = 2;
    /* 子女按出生先后 */
    int first = n;
    for (int i = 0; i < TREE_N && n < max; i++) {
        const person_t *c = &g_tree[i];
        if (!c->uid || (c->father != uid && c->mother != uid)) continue;
        int k = n;
        while (k > first && sim_person(out[k - 1])->born > c->born) {
            out[k] = out[k - 1];
            role[k] = 3;
            k--;
        }
        out[k] = c->uid;
        role[k] = 3;
        n++;
    }
    return n;
}
int sim_descendants(uint16_t uid, int *living)
{
    static uint8_t mark[TREE_N];
    memset(mark, 0, sizeof mark);
    bool grew = true;
    int total = 0, alive = 0;
    while (grew) { /* 一代一代往下标记 */
        grew = false;
        for (int i = 0; i < TREE_N; i++) {
            const person_t *c = &g_tree[i];
            if (!c->uid || mark[i]) continue;
            const person_t *f = sim_person(c->father), *m = sim_person(c->mother);
            bool hit = c->father == uid || c->mother == uid || (f && mark[c->father % TREE_N]) ||
                       (m && mark[c->mother % TREE_N]);
            if (!hit) continue;
            mark[i] = 1;
            grew = true;
            total++;
            alive += c->age == 0xFF;
        }
    }
    if (living) *living = alive;
    return total;
}

void sim_devotion(world_t *w, int dfaith, int dfear)
{
    int f = w->faith + dfaith, e = w->fear + dfear;
    w->faith = (uint8_t)(f < 0 ? 0 : f > 100 ? 100 : f);
    w->fear = (uint8_t)(e < 0 ? 0 : e > 100 ? 100 : e);
}
int sim_day(const world_t *w) { return (int)(w->tick / TICKS_PER_DAY) + 1; }
int sim_year(const world_t *w) { return (sim_day(w) - 1) / DAYS_PER_YEAR + 1; }
int sim_season(const world_t *w) { return (sim_day(w) - 1) % DAYS_PER_YEAR; }
int sim_count_obj(const world_t *w, int obj)
{
    int n = 0;
    for (int i = 0; i < MAP_N; i++) n += OBJ(w->tiles[i]) == obj;
    return n;
}
const char *sim_building_name(int obj)
{
    switch (obj) {
    case O_WELL: return "水井";
    case O_GRANARY: return "粮仓";
    case O_DOCK: return "码头";
    case O_TOWER: return "瞭望塔";
    case O_FENCE: return "木栅栏";
    case O_PEN: return "牧场";
    case O_STATUE: return "神像";
    case O_SHIP: return "远航的大船";
    default: return "房屋";
    }
}

/* ---------- 职业与技能 ---------- */

int sim_prof(const resident_t *r)
{
    if (sim_age(r) < AGE_WORK) return -1;
    int best = 0;
    for (int k = 1; k < SK_COUNT; k++)
        if (r->skill[k] > r->skill[best]) best = k;
    return best;
}
const char *sim_prof_name(const resident_t *r)
{
    int age = sim_age(r), p = sim_prof(r);
    if (age < AGE_CHILD) return "婴儿";
    if (p < 0) return "孩子";
    if (age >= AGE_ELDER && r->skill[p] >= 45) return "长老";
    return PROFS[p];
}
const char *sim_skill_level(int v) { return v < 25 ? "学徒" : v < 45 ? "熟练" : v < 65 ? "精通" : "大师"; }

int sim_who(const resident_t *r) { return (r->sex << 7) | (r->name & 0x7F); }

void sim_note(world_t *w, resident_t *r, int ev, int arg)
{
    int k = r->ev_n % EV_MAX;
    r->ev[k] = (uint8_t)ev;
    r->ev_arg[k] = (uint8_t)arg;
    r->ev_day[k] = (uint16_t)sim_day(w);
    r->ev_n++;
}

/* 小步练习按概率涨（大约每练十来次涨一点，十几年才能成大师）；传承这种大块的直接加 */
void sim_skill_up(world_t *w, resident_t *r, int sk, int amount)
{
    if (amount < 10) amount = rn(w, 100) < amount * 15 ? 1 : 0;
    if (!amount) return;
    int old = r->skill[sk], v = old + amount;
    if (v > 100) v = 100;
    r->skill[sk] = (uint8_t)v;
    if (old < 65 && v >= 65) { /* 成了大师 */
        sim_note(w, r, EV_MASTER, sk);
        sim_logf("%s的%s手艺炉火纯青", sim_name(r), SKILLS[sk]);
    }
}

/* 越熟练、时代越先进，干活越快；最快是原来的一半 */
int sim_work_time(const world_t *w, const resident_t *r, int sk, int base)
{
    int pct = 100 - r->skill[sk] / 3 - w->era * 8 - ((w->trade & TD_TOOLS) ? 10 : 0);
    if (pct < 50) pct = 50;
    return base * pct / 100;
}

/* ---------- 心声与生平 ---------- */

static const char *who_name(int arg) { return sim_name_of(arg >> 7, arg & 0x7F); }

void sim_event_text(const resident_t *r, int k, char *buf, int n)
{
    buf[0] = 0;
    if (k >= r->ev_n || k >= EV_MAX) return;
    int i = (r->ev_n - 1 - k) % EV_MAX, a = r->ev_arg[i];
    int y = (r->ev_day[i] - 1) / DAYS_PER_YEAR + 1;
    const char *s = SEASONS[(r->ev_day[i] - 1) % DAYS_PER_YEAR];
    char what[48];
    switch (r->ev[i]) {
    case EV_BORN: snprintf(what, sizeof what, "出生"); break;
    case EV_ARRIVE: snprintf(what, sizeof what, "来到岛上"); break;
    case EV_MARRY: snprintf(what, sizeof what, "与%s结为伴侣", who_name(a)); break;
    case EV_CHILD: snprintf(what, sizeof what, "有了孩子%s", who_name(a)); break;
    case EV_HOUSE: snprintf(what, sizeof what, "亲手盖起了家"); break;
    case EV_MASTER: snprintf(what, sizeof what, "成为%s大师", SKILLS[a % SK_COUNT]); break;
    case EV_INHERIT: snprintf(what, sizeof what, "继承了%s的手艺", who_name(a)); break;
    case EV_BLESSED: snprintf(what, sizeof what, "受到神的赐福"); break;
    case EV_WOLF_KILL: snprintf(what, sizeof what, "打死了一头恶狼"); break;
    case EV_BUILD: snprintf(what, sizeof what, "建成了%s", sim_building_name(a)); break;
    case EV_ERA: snprintf(what, sizeof what, "带领岛民进入%s时代", ERAS[a % ERA_COUNT]); break;
    case EV_LOST_PARTNER: snprintf(what, sizeof what, "失去了伴侣%s", who_name(a)); break;
    case EV_LOST_PARENT: snprintf(what, sizeof what, "送别了%s%s", (a >> 7) ? "母亲" : "父亲", who_name(a)); break;
    case EV_LOST_CHILD: snprintf(what, sizeof what, "失去了孩子%s", who_name(a)); break;
    case EV_HURT: snprintf(what, sizeof what, "在%s中受了重伤", sim_cause_name(a)); break;
    case EV_ADULT: snprintf(what, sizeof what, "长大成人"); break;
    case EV_DREAM: snprintf(what, sizeof what, "实现梦想：%s", sim_dream_name(a)); break;
    case EV_FEUD: snprintf(what, sizeof what, "和%s结下了梁子", who_name(a)); break;
    case EV_MAKEUP: snprintf(what, sizeof what, "和%s和好了", who_name(a)); break;
    case EV_VOYAGE: snprintf(what, sizeof what, "出海远航归来"); break;
    case EV_FIRE: snprintf(what, sizeof what, "冲进火场救火"); break;
    default: snprintf(what, sizeof what, "……"); break;
    }
    snprintf(buf, (size_t)n, "%d年%s %s", y, s, what);
}

/* 心声：先看最近几天发生的事，再看眼下的处境 */
static bool i_am(const world_t *w, const resident_t *r, int idx) { return r == &w->res[idx % MAX_RES]; }
static bool is_follow(int g) { return g == G_GIFT || g == G_DATE || g == G_SNOW || g == G_PLAY || g == G_MAKEUP; }
void sim_thought(const world_t *w, const resident_t *r, char *buf, int n)
{
    int day = sim_day(w), age = sim_age(r);
    if (r->ev_n) {
        int i = (r->ev_n - 1) % EV_MAX, a = r->ev_arg[i];
        if (day - r->ev_day[i] <= 3) switch (r->ev[i]) {
            case EV_MARRY: snprintf(buf, (size_t)n, "能和%s在一起，真好", who_name(a)); return;
            case EV_CHILD: snprintf(buf, (size_t)n, "%s长大了会像谁呢", who_name(a)); return;
            case EV_LOST_PARTNER: snprintf(buf, (size_t)n, "%s，我好想你", who_name(a)); return;
            case EV_LOST_PARENT: snprintf(buf, (size_t)n, "再也见不到%s了", (a >> 7) ? "妈妈" : "爸爸"); return;
            case EV_LOST_CHILD: snprintf(buf, (size_t)n, "我的%s……", who_name(a)); return;
            case EV_HOUSE: snprintf(buf, (size_t)n, "终于有自己的家了"); return;
            case EV_BLESSED: snprintf(buf, (size_t)n, "神在看着我！"); return;
            case EV_WOLF_KILL: snprintf(buf, (size_t)n, "那头狼再也伤不了人了"); return;
            case EV_MASTER: snprintf(buf, (size_t)n, "这门手艺，我算是练成了"); return;
            case EV_INHERIT: snprintf(buf, (size_t)n, "%s的手艺不能断在我手里", who_name(a)); return;
            case EV_HURT: snprintf(buf, (size_t)n, "伤口还在疼"); return;
            default: break;
            }
    }
    if (r->sick) { snprintf(buf, (size_t)n, "浑身发烫，好难受"); return; }
    if (r->goal == G_FLEE || r->goal == G_COWER) { snprintf(buf, (size_t)n, w->fear >= 40 ? "神为什么发怒？" : "快躲起来！"); return; }
    if (r->goal == G_FIGHT) { snprintf(buf, (size_t)n, "绝不让狼伤害大家"); return; }
    if (r->food < 250) { snprintf(buf, (size_t)n, w->food ? "饿死了，先去吃点东西" : "仓库空了，可怎么办"); return; }
    if (r->hp < 400) { snprintf(buf, (size_t)n, "得好好歇一歇"); return; }
    if (w->wish) { snprintf(buf, (size_t)n, "神啊，请赐给我们%s", sim_wish_name(w->wish)); return; }
    if (r->preg) { snprintf(buf, (size_t)n, "肚子里的小家伙在动呢"); return; }
    if (w->fest_h) { snprintf(buf, (size_t)n, "今年是个丰收年！"); return; }
    if (r->state == S_SLEEP) { snprintf(buf, (size_t)n, "Zzz……"); return; }
    if (r->away) { snprintf(buf, (size_t)n, "海的那边，原来是这个样子"); return; }
    {
        const char *s = NULL;
        switch (r->goal) {
        case G_FETCH: case G_DOUSE: s = "快！别让火烧到房子"; break;
        case G_QUARREL: s = "气死我了！"; break;
        case G_MOURN: s = "一路走好"; break;
        case G_TRADE: s = "这些能换点什么好东西呢"; break;
        case G_FEEDPEN: s = "猪儿猪儿快长大"; break;
        case G_CLEAR: s = "收拾干净了，日子还得过"; break;
        case G_MAKEUP: s = "上次是我不对"; break;
        case G_WEDDING:
            if (w->cer == CER_NEWYEAR) s = "又是新的一年！";
            else if (w->cer == CER_WEDDING && (i_am(w, r, w->cer_a) || i_am(w, r, w->cer_b))) s = "这辈子就是你了";
            else s = "真替他们高兴";
            break;
        default: break;
        }
        if (s) { snprintf(buf, (size_t)n, "%s", s); return; }
    }
    if (r->partner != NO_SLOT && w->res[r->partner].away) {
        snprintf(buf, (size_t)n, "%s出海了，快点回来吧", sim_name(&w->res[r->partner]));
        return;
    }
    {
        int f = sim_foe(w, (int)(r - w->res), 50);
        if (f >= 0 && (day + r->uid) % 3 == 0) { snprintf(buf, (size_t)n, "还在生%s的气", sim_name(&w->res[f])); return; }
    }
    if (r->dream && !(r->flags & RF_DREAM) && (day + r->uid) % 4 == 0 && r->state != S_ACT) {
        snprintf(buf, (size_t)n, "总有一天，我要%s", sim_dream_name(r->dream));
        return;
    }
    if (r->state == S_ACT || is_follow(r->goal)) {
        const char *s = NULL;
        switch (r->goal) {
        case G_SWIM: s = "水好凉快！"; break;
        case G_STROLL: s = "海风吹着真舒服"; break;
        case G_GAZE: s = "今晚的星星真多"; break;
        case G_DANCE: s = "再跳一曲！"; break;
        case G_NAP: s = "就眯一小会儿……"; break;
        case G_FLOWER: s = "这朵最好看"; break;
        case G_GIFT: s = "不知道TA会不会喜欢"; break;
        case G_DATE: s = "和你在一起真好"; break;
        case G_SNOW: s = "看招！"; break;
        case G_PLAY: s = "来追我呀！"; break;
        case G_STORY: s = "很久很久以前……"; break;
        case G_LISTEN: s = "后来呢？后来呢？"; break;
        default: break;
        }
        if (s) { snprintf(buf, (size_t)n, "%s", s); return; }
    }
    if (r->fun < 200) { snprintf(buf, (size_t)n, "好无聊啊，找点乐子"); return; }
    if (r->social < 200) { snprintf(buf, (size_t)n, "好久没跟人说说话了"); return; }
    if (age < AGE_WORK) { snprintf(buf, (size_t)n, age < 6 ? "我想快点长大" : "长大了我要当%s", PROFS[(r->uid + age) % SK_COUNT]); return; }
    int season = sim_season(w);
    if (season == SEASON_AUTUMN && w->food < sim_pop(w) * 8 && !sim_count_obj(w, O_GRANARY)) { snprintf(buf, (size_t)n, "冬天快到了，得多存点粮"); return; }
    if (season == SEASON_WINTER) { snprintf(buf, (size_t)n, r->home == NO_ID ? "冬天没有屋子，太冷了" : "冬天真冷，盼着开春"); return; }
    if (r->home == NO_ID && age >= AGE_ADULT) { snprintf(buf, (size_t)n, "我也想有个自己的家"); return; }
    if (r->partner == NO_SLOT && age >= AGE_ADULT && age < 40) { snprintf(buf, (size_t)n, "什么时候能遇到那个人呢"); return; }
    if (age >= AGE_OLD) { snprintf(buf, (size_t)n, "这把老骨头，还能看几个春天"); return; }
    if (w->faith >= 60) { snprintf(buf, (size_t)n, "感谢神的眷顾"); return; }
    int p = sim_prof(r);
    static const char *const WORK[SK_COUNT] = {"今天能打到猎物吗", "庄稼长得不错", "再盖一间好房子", "海上的风浪真大"};
    if (season == SEASON_SPRING) { snprintf(buf, (size_t)n, "春天来了，到处都是花"); return; }
    snprintf(buf, (size_t)n, "%s", WORK[p < 0 ? 0 : p]);
}
/* 心愿：此刻最想要的东西 */
void sim_want(const world_t *w, const resident_t *r, char *buf, int n)
{
    int age = sim_age(r), season = sim_season(w), p = sim_prof(r);
    const char *s;
    if (age < AGE_CHILD) s = "快快长大";
    else if (r->food < 400) s = "吃顿饱饭";
    else if (r->sick) s = "早日病好";
    else if (r->energy < 300) s = "好好睡一觉";
    else if (r->fun < 400)
        s = season == SEASON_SUMMER ? "去海里游泳" : season == SEASON_WINTER ? "打一场雪仗" : age < AGE_WORK ? "找人一起玩" : "找点乐子";
    else if (r->social < 400) s = "找人说说话";
    else if (age < AGE_WORK) s = "听长老讲故事";
    else if (r->partner == NO_SLOT && age >= AGE_ADULT && age < 45) s = "遇到心上人";
    else if (r->home == NO_ID && age >= AGE_ADULT) s = "有间自己的房子";
    else if (r->partner != NO_SLOT && !r->kids && age < 45) s = "有个孩子";
    else if (p >= 0 && r->skill[p] < 65 && age < AGE_ELDER) {
        snprintf(buf, (size_t)n, "成为%s大师", SKILLS[p]);
        return;
    } else if (age >= AGE_ELDER) s = "看着子孙长大";
    else s = "日子一直这样好";
    snprintf(buf, (size_t)n, "%s", s);
}
int sim_hour(const world_t *w) { return (int)(w->tick % TICKS_PER_DAY) * 24 / TICKS_PER_DAY; }
bool sim_is_night(const world_t *w)
{
    int h = sim_hour(w);
    return h >= 21 || h < 5;
}
int sim_speed(const world_t *w)
{
    static const uint8_t SPD[4] = {1, 2, 8, 32};
    return SPD[w->speed_idx % 4];
}
int sim_age(const resident_t *r) { return r->age_days / DAYS_PER_YEAR; }
bool sim_in_house(const world_t *w, const resident_t *r)
{
    return r->step == 0 && sim_is_house(OBJ(w->tiles[IDX(r->tx, r->ty)]));
}

const char *sim_name_of(int sex, int name) { return (sex ? NAMES_F : NAMES_M)[name % NAME_N]; }
const char *sim_name(const resident_t *r) { return sim_name_of(r->sex, r->name); }
const char *sim_trait(const resident_t *r) { return TRAITS[r->trait % TR_COUNT]; }
const char *sim_power_name(int p) { return POWERS[p % P_COUNT]; }
const char *sim_weather_name(int wt) { return WEATHERS[wt % W_COUNT]; }
const char *sim_cause_name(int c) { return CAUSES[c % D_COUNT]; }
int sim_power_cat_id(int p)
{
    if (p <= P_RECORDS) return 0;
    if (p <= P_CROP) return 1;
    if (p <= P_RESTORE) return 2;
    if (p <= P_ECLIPSE) return 3;
    if (p <= P_PLAGUE) return 4;
    return 5;
}
const char *sim_power_cat(int p) { return CATS[sim_power_cat_id(p)]; }

const char *sim_status(const world_t *w, const resident_t *r)
{
    if (r->away) return "出海远航中";
    if (sim_age(r) < AGE_CHILD) return r->state == S_SLEEP ? "睡觉" : "在家里玩";
    if (r->state == S_SLEEP) return sim_in_house(w, r) ? "在家睡觉" : "露天睡觉";
    bool moving = r->state == S_MOVE;
    switch (r->goal) {
    case G_EAT_CAMP: return moving ? "回营地吃饭" : (w->fire_lit ? "烤东西吃" : "吃饭");
    case G_EAT_BERRY: return moving ? "去找浆果吃" : "吃浆果";
    case G_BERRY: return moving ? "去采浆果" : "采集浆果";
    case G_CHOP: return moving ? "去砍树" : "砍伐树木";
    case G_HUNT:
        if (w->ani[r->tgt % MAX_ANI].kind == A_RABBIT) return moving ? "追兔子" : "抓兔子";
        return moving ? "追猎野猪" : "与野猪搏斗";
    case G_FISH:
        if (OBJ(w->tiles[IDX(r->gx, r->gy)]) == O_DOCK) return moving ? "去码头" : "出海捕鱼";
        return moving ? "去水边" : "捕鱼";
    case G_HAUL: return r->carry == C_WOOD ? "搬木柴回营地" : r->carry == C_STONE ? "搬石料回营地" : "带食物回营地";
    case G_SLEEP: return "回去睡觉";
    case G_REST: return "休息";
    case G_FLEE: return "逃离危险";
    case G_CHAT: return moving ? "去找人聊天" : "聊天";
    case G_HELP: return moving ? "赶去救助同伴" : "照料伤者";
    case G_TEND:
        if (sim_age(&w->res[r->tgt % MAX_RES]) >= AGE_CHILD) return moving ? "给病人送饭" : "喂病人吃饭";
        return moving ? "回家看孩子" : "照顾孩子";
    case G_BUILD: return moving ? "去选地盖房" : "盖房子";
    case G_UPGRADE: return moving ? "回家翻修" : "翻修房屋";
    case G_REPAIR: return moving ? "回家修房" : "修缮房屋";
    case G_TILL: return moving ? "去开垦" : "开垦农田";
    case G_HARVEST: return moving ? "去收庄稼" : "收割庄稼";
    case G_MINE: return moving ? "去搬石头" : "凿石头";
    case G_SHELTER: return moving ? "回家避风雨" : "在家躲避风雨";
    case G_SHRINE: return moving ? "去立祭石" : "垒起祭石";
    case G_PRAY: return moving ? "去祭石前" : "向神祈祷";
    case G_COWER: return "惶恐不安";
    case G_FEAST: return moving ? "去篝火边" : "围着篝火庆祝";
    case G_FIGHT: return moving ? "追打恶狼" : "与恶狼搏斗";
    case G_SWIM: return moving ? "去海边" : "游泳";
    case G_STROLL: return "在沙滩上散步";
    case G_GAZE:
        if (w->sky == SKY_METEOR) return "看流星雨";
        if (w->sky == SKY_RAINBOW) return "看彩虹";
        if (w->sky == SKY_ECLIPSE) return "望着日食发呆";
        return moving ? "出门看星星" : "看星星";
    case G_DANCE: return moving ? "去篝火边" : "跳舞";
    case G_NAP: return moving ? "找树荫" : "在树下打盹";
    case G_FLOWER: return moving ? "去摘花" : "摘花";
    case G_GIFT: return "去送花";
    case G_DATE: return moving ? "去找伴侣" : "和伴侣约会";
    case G_SNOW: return "打雪仗";
    case G_PLAY: return moving ? "追着玩" : "嬉戏打闹";
    case G_STORY: return moving ? "去篝火边" : "讲故事";
    case G_LISTEN: return moving ? "去听故事" : "听故事";
    case G_FETCH: return moving ? "跑去打水" : "打水";
    case G_DOUSE: return moving ? "提水去救火" : "泼水救火";
    case G_CLEAR: return moving ? "去清理废墟" : "清理废墟";
    case G_QUARREL: return "吵架";
    case G_MAKEUP: return moving ? "去找人和好" : "和好";
    case G_WEDDING:
        if (w->cer == CER_WEDDING && (i_am(w, r, w->cer_a) || i_am(w, r, w->cer_b))) return moving ? "去婚礼" : "举行婚礼";
        return w->cer == CER_NEWYEAR ? (moving ? "去篝火边过年" : "过新年") : w->cer == CER_ADULT ? "参加成人礼" : (moving ? "去喝喜酒" : "参加婚礼");
    case G_MOURN: return moving ? "去墓园" : "送葬";
    case G_TRADE: return moving ? "带货去商船" : "和商人交易";
    case G_FEEDPEN: return moving ? "去牧场" : "喂猪";
    case G_CONSTRUCT:
        switch (w->proj_type) {
        case O_WELL: return moving ? "去挖井" : "挖井";
        case O_GRANARY: return moving ? "去修粮仓" : "修粮仓";
        case O_DOCK: return moving ? "去修码头" : "修码头";
        case O_TOWER: return moving ? "去修瞭望塔" : "修瞭望塔";
        default: return moving ? "去扎栅栏" : "扎栅栏";
        }
    default: return sim_age(r) < AGE_WORK ? "玩耍" : "闲逛";
    }
}

/* ---------- 派生数据：到水的距离、危险区 ---------- */

void sim_calc_wdist(const world_t *w)
{
    static uint16_t q[MAP_N];
    int head = 0, tail = 0;
    for (int i = 0; i < MAP_N; i++) {
        int t = TERR(w->tiles[i]);
        if (sim_is_water(t) || t == T_SEA) {
            sim_wdist[i] = 0;
            q[tail++] = (uint16_t)i;
        } else {
            sim_wdist[i] = 255;
        }
    }
    while (head < tail) {
        int c = q[head++], cx = c % MAP_W, cy = c / MAP_W;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!inmap(nx, ny)) continue;
            int n = IDX(nx, ny);
            if (sim_wdist[n] != 255) continue;
            sim_wdist[n] = (uint8_t)(sim_wdist[c] + 1);
            q[tail++] = (uint16_t)n;
        }
    }
    for (int i = 0; i < MAP_N; i++)
        if (sim_wdist[i] > 15) sim_wdist[i] = 15;
}

void sim_calc_danger(const world_t *w)
{
    memset(sim_danger, 0, sizeof sim_danger);
    s_hazard = false;
    if (!w->fires && !w->lava && !w->flood) return;
    for (int i = 0; i < MAP_N; i++) {
        bool hot = OBJ(w->tiles[i]) == O_FLAME || TERR(w->tiles[i]) == T_LAVA;
        if (!hot && !sim_flooded(w, i)) continue;
        s_hazard = true;
        sim_danger[i] = 1;
        if (!hot) continue;
        int cx = i % MAP_W, cy = i / MAP_W;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                if (!inmap(cx + dx, cy + dy)) continue;
                int n = IDX(cx + dx, cy + dy);
                int lvl = (iabs(dx) <= 1 && iabs(dy) <= 1) ? 2 : 3;
                if (sim_danger[n] == 0 || (sim_danger[n] == 3 && lvl == 2)) sim_danger[n] = (uint8_t)lvl;
            }
    }
}

/* ---------- 寻路：780 格上的 BFS，不保存路径，每走一格重算一步 ---------- */

static uint16_t s_q[MAP_N];
static int s_self;     /* 当前决策的居民下标，供谓词使用 */
static int s_goal_idx; /* pred_goal 的目标格 */

void sim_set_self(int i) { s_self = i; }

int sim_bfs(const world_t *w, int sx, int sy, sim_pred_t pred, int *dir_out)
{
    memset(sim_par, 0, sizeof sim_par);
    int s = IDX(sx, sy), head = 0, tail = 0;
    sim_par[s] = 5;
    s_q[tail++] = (uint16_t)s;
    while (head < tail) {
        int c = s_q[head++];
        if (pred(w, c)) {
            if (dir_out) {
                int d = -1, k = c;
                while (k != s) {
                    d = sim_par[k] - 1;
                    k -= DX[d] + DY[d] * MAP_W;
                }
                *dir_out = d;
            }
            return c;
        }
        int cx = c % MAP_W, cy = c / MAP_W;
        int rot = ((cx + cy) & 1) * 2; /* 交替先横后纵，走出斜向的折线 */
        for (int k = 0; k < 4; k++) {
            int d = (k + rot) & 3;
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!inmap(nx, ny)) continue;
            int n = IDX(nx, ny);
            if (sim_par[n] || !sim_walkable(w, n)) continue;
            sim_par[n] = (uint8_t)(d + 1);
            s_q[tail++] = (uint16_t)n;
        }
    }
    return -1;
}
#define bfs sim_bfs

bool sim_pred_none(const world_t *w, int idx)
{
    (void)w;
    (void)idx;
    return false;
}
static bool pred_goal(const world_t *w, int idx)
{
    (void)w;
    return idx == s_goal_idx;
}

/* 目标是否已被别的居民占了（避免五个人抢同一棵树） */
static bool taken(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int i = 0; i < MAX_RES; i++) {
        const resident_t *r = &w->res[i];
        if (i == s_self || !r->alive || r->gx != x || r->gy != y) continue;
        switch (r->goal) {
        case G_BERRY: case G_EAT_BERRY: case G_CHOP: case G_FISH:
        case G_BUILD: case G_TILL: case G_HARVEST: case G_MINE: case G_SHRINE: return true;
        default: break;
        }
    }
    return false;
}
static bool free_tile(const world_t *w, int idx) { return !sim_danger[idx] && !taken(w, idx); }
static bool near4(const world_t *w, int idx, int terr, int obj)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (!inmap(nx, ny)) continue;
        uint16_t t = w->tiles[IDX(nx, ny)];
        if ((terr >= 0 && TERR(t) == terr) || (obj >= 0 && OBJ(t) == obj)) return true;
    }
    return false;
}
static int camp_dist(const world_t *w, int idx) { return cheb(idx % MAP_W, idx / MAP_W, w->camp_x, w->camp_y); }

static bool pred_berry(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_BERRY && free_tile(w, idx); }
static bool pred_tree(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_TREE && free_tile(w, idx); }
static bool pred_ripe(const world_t *w, int idx)
{
    return OBJ(w->tiles[idx]) == O_FARM && VAL(w->tiles[idx]) >= 100 && free_tile(w, idx);
}
bool sim_pred_shore(const world_t *w, int idx)
{
    if (TERR(w->tiles[idx]) == T_RIVER || !free_tile(w, idx)) return false;
    return near4(w, idx, T_SHOAL, -1) || near4(w, idx, T_LAKE, -1) || near4(w, idx, T_RIVER, -1);
}
static bool pred_mine(const world_t *w, int idx) { return free_tile(w, idx) && near4(w, idx, T_ROCK, -1); }
/* 挨着碎石的空地：岛民会站在这里把碎石凿开搬走 */
static int rubble_next_to(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (inmap(nx, ny) && IS_RUBBLE(w->tiles[IDX(nx, ny)])) return IDX(nx, ny);
    }
    return -1;
}
static bool pred_rubble(const world_t *w, int idx) { return free_tile(w, idx) && rubble_next_to(w, idx) >= 0; }
static bool pred_safe(const world_t *w, int idx)
{
    (void)w;
    return sim_danger[idx] == 0;
}
static bool empty_grass(const world_t *w, int idx)
{
    int o = OBJ(w->tiles[idx]);
    return TERR(w->tiles[idx]) == T_GRASS && (o == O_NONE || o == O_FLOWER);
}
/* 宅基地：离营地 3~10 格、离水至少 3 格、不和别的房子挨着 */
static bool pred_site(const world_t *w, int idx)
{
    if (!free_tile(w, idx)) return false;
    if (OBJ(w->tiles[idx]) == O_SITE) return VAL(w->tiles[idx]) == 0; /* 接手没盖完的房子；公共工地另算 */
    if (!empty_grass(w, idx) || sim_wdist[idx] < 3) return false;
    int d = camp_dist(w, idx);
    if (d < 3 || d > 10) return false;
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!inmap(x + dx, y + dy)) continue;
            int o = OBJ(w->tiles[IDX(x + dx, y + dy)]);
            if (sim_is_house(o) || o == O_SITE) return false;
        }
    return true;
}
static bool pred_till_any(const world_t *w, int idx)
{
    if (!free_tile(w, idx) || !empty_grass(w, idx) || sim_wdist[idx] < 2) return false;
    int d = camp_dist(w, idx);
    return d >= 5 && d <= 8 && !near4(w, idx, -1, O_HOUSE1) && !near4(w, idx, -1, O_HOUSE2);
}
static bool pred_shrine(const world_t *w, int idx)
{
    return free_tile(w, idx) && empty_grass(w, idx) && camp_dist(w, idx) == 3 && !sim_occupied(w, idx);
}
static bool pred_till_adj(const world_t *w, int idx) { return pred_till_any(w, idx) && near4(w, idx, -1, O_FARM); }

/* ---------- 居民：出生、死亡、受伤 ---------- */

bool sim_camp_spot(const world_t *w, int i, int *x, int *y)
{
    for (int k = 0; k < MAX_RES; k++) {
        int j = (i + k) % MAX_RES;
        int sx = w->camp_x + SPOT[j][0], sy = w->camp_y + SPOT[j][1];
        if (inmap(sx, sy) && sim_walkable(w, IDX(sx, sy))) {
            *x = sx;
            *y = sy;
            return true;
        }
    }
    return false;
}

int sim_spawn_resident(world_t *w, int sex, int trait, int age_years, int at_idx)
{
    int slot = -1;
    for (int i = 0; i < MAX_RES; i++)
        if (!w->res[i].alive) { slot = i; break; }
    if (slot < 0) return -1;
    /* 挑一个在世居民没用过的名字 */
    int name = rn(w, NAME_N);
    for (int k = 0; k < NAME_N; k++) {
        bool used = false;
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && w->res[i].sex == sex && w->res[i].name == name) used = true;
        if (!used) break;
        name = (name + 1) % NAME_N;
    }
    resident_t *r = &w->res[slot];
    memset(r, 0, sizeof *r);
    r->alive = 1;
    r->name = (uint8_t)name;
    r->sex = (uint8_t)sex;
    r->trait = (uint8_t)trait;
    r->uid = w->next_uid++;
    r->age_days = (uint16_t)(age_years * DAYS_PER_YEAR + (age_years ? rn(w, DAYS_PER_YEAR) : 0));
    r->hp = 1000;
    r->food = (uint16_t)(600 + rn(w, 300));
    r->energy = (uint16_t)(700 + rn(w, 300));
    r->mood = 600;
    r->fun = (uint16_t)(500 + rn(w, 400));
    r->social = (uint16_t)(500 + rn(w, 400));
    r->partner = NO_SLOT;
    r->home = NO_ID;
    if (age_years >= AGE_ADULT) {
        r->flags = RF_ADULT;
        r->dream = (uint8_t)(1 + rn(w, DR_COUNT - 1));
    }
    r->father = r->mother = NO_ID;
    r->fname = r->mname = NO_SLOT;
    r->gen = 1;
    int x = w->camp_x, y = w->camp_y + 1;
    if (at_idx >= 0) {
        x = at_idx % MAP_W;
        y = at_idx / MAP_W;
    } else {
        sim_camp_spot(w, slot, &x, &y);
    }
    r->tx = r->nx = (uint8_t)x;
    r->ty = r->ny = (uint8_t)y;
    for (int j = 0; j < MAX_RES; j++) w->rel[slot][j] = w->rel[j][slot] = w->feud[slot][j] = w->feud[j][slot] = 0;
    if (age_years) { /* 成年人来的时候就带着手艺，按性格偏向不同 */
        for (int k = 0; k < SK_COUNT; k++) r->skill[k] = (uint8_t)(8 + rn(w, 18));
        static const int8_t BIAS[TR_COUNT][SK_COUNT] = {{0, 15, 20, 0}, {5, 5, 0, 10}, {30, 0, 5, 0}, {0, 10, 0, 25}, {0, 25, 5, 5}};
        for (int k = 0; k < SK_COUNT; k++) r->skill[k] = (uint8_t)(r->skill[k] + BIAS[trait % TR_COUNT][k]);
    }
    sim_note(w, r, age_years ? EV_ARRIVE : EV_BORN, 0);
    sim_tree_put(w, r);
    return slot;
}

void sim_die(world_t *w, int i, int cause)
{
    resident_t *r = &w->res[i];
    if (!r->alive) return;
    r->alive = 0;
    w->deaths++;
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].held == i + 1) w->ani[k].held = 0;
    if (r->partner != NO_SLOT && w->res[r->partner].partner == i) w->res[r->partner].partner = NO_SLOT;
    int idx = IDX(r->tx, r->ty);
    int o = OBJ(w->tiles[idx]), t = TERR(w->tiles[idx]);
    bool ok = !r->away && t != T_LAVA && (o == O_NONE || o == O_FLOWER || o == O_STUMP || o == O_BURNT);
    int g = sim_on_death(w, i, ok ? idx : -1); /* 葬进墓园 */
    if (g >= 0) w->tiles[g] = MKTILE(TERR(w->tiles[g]) == T_ASH ? T_GRASS : TERR(w->tiles[g]), O_GRAVE);
    r->away = 0;
    dead_t *d = &w->dead[w->dead_n % MAX_DEAD];
    w->dead_n++;
    *d = (dead_t){r->name, r->sex, (uint8_t)sim_age(r), r->gen, (uint8_t)cause, 1, (uint16_t)sim_day(w)};
    person_t *pr = &g_tree[r->uid % TREE_N];
    if (pr->uid == r->uid) {
        pr->age = (uint8_t)(sim_age(r) > 250 ? 250 : sim_age(r));
        pr->cause = (uint8_t)cause;
        g_tree_dirty = true;
    }
    sim_sfx(SFX_DEATH);
    /* 亲人记下这件事 */
    int best_sk = 0;
    for (int k = 1; k < SK_COUNT; k++)
        if (r->skill[k] > r->skill[best_sk]) best_sk = k;
    resident_t *heir = NULL;
    for (int j = 0; j < MAX_RES; j++) {
        resident_t *o = &w->res[j];
        if (!o->alive || j == i) continue;
        bool child = o->father == r->uid || o->mother == r->uid;
        if (r->partner == j || o->partner == i) sim_note(w, o, EV_LOST_PARTNER, sim_who(r));
        else if (child) sim_note(w, o, EV_LOST_PARENT, sim_who(r));
        else if (r->father == o->uid || r->mother == o->uid) sim_note(w, o, EV_LOST_CHILD, sim_who(r));
        /* 手艺传给最年轻的孩子；没有孩子就传给岛上最年轻的人 */
        if (child && (!heir || o->age_days < heir->age_days || !(heir->father == r->uid || heir->mother == r->uid))) heir = o;
        else if (!heir && sim_age(o) < AGE_ADULT) heir = o;
    }
    if (heir && r->skill[best_sk] >= 40 && heir->skill[best_sk] < r->skill[best_sk]) {
        int gain = (r->skill[best_sk] - heir->skill[best_sk]) / 2;
        sim_skill_up(w, heir, best_sk, gain);
        sim_note(w, heir, EV_INHERIT, sim_who(r));
    }
    /* 天灾夺命，岛民会觉得是神在发怒；饿死人，则是神不眷顾 */
    if (cause == D_STARVE) sim_devotion(w, -3, 0);
    else if (cause != D_AGE && cause != D_WOLF) sim_devotion(w, 0, 4);
    const char *n = sim_name(r);
    switch (cause) {
    case D_STARVE: sim_logf("%s饿死了", n); break;
    case D_AGE: sim_logf("%s因年老去世，享年%d岁", n, sim_age(r)); break;
    case D_BOLT: sim_logf("%s被雷火夺去了生命", n); break;
    case D_FIRE: sim_logf("%s葬身火海", n); break;
    case D_FLOOD: sim_logf("%s被洪水卷走了", n); break;
    case D_LAVA: sim_logf("%s葬身岩浆", n); break;
    case D_QUAKE: sim_logf("%s在地震中遇难", n); break;
    case D_PLAGUE: sim_logf("%s病死了", n); break;
    case D_WOLF: sim_logf("%s被狼咬死了", n); break;
    default: sim_logf("%s在风雪中冻死了", n); break;
    }
    if (heir && r->skill[best_sk] >= 40 && heir->alive && heir->ev[(heir->ev_n - 1) % EV_MAX] == EV_INHERIT)
        sim_logf("%s的%s手艺传给了%s", n, SKILLS[best_sk], sim_name(heir));
    if (sim_pop(w) == 0) sim_logf("岛上已无人烟");
}

bool sim_hurt(world_t *w, int i, int amount, int cause)
{
    resident_t *r = &w->res[i];
    if (r->away) return true; /* 出海了，岛上的灾祸碰不到 */
    if (r->hp <= amount) {
        sim_die(w, i, cause);
        return false;
    }
    r->hp = (uint16_t)(r->hp - amount);
    /* 重伤记一笔（同一天只记一次） */
    if (amount >= 200 && !(r->ev_n && r->ev[(r->ev_n - 1) % EV_MAX] == EV_HURT && r->ev_day[(r->ev_n - 1) % EV_MAX] == sim_day(w)))
        sim_note(w, r, EV_HURT, cause);
    return true;
}

void sim_infect(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!r->alive || r->sick || r->immune) return;
    r->sick = (uint8_t)(4 + rn(w, 3));
}

void sim_house_lost(world_t *w, int idx)
{
    const char *who = NULL;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && w->res[i].home == idx) {
            w->res[i].home = NO_ID;
            if (!who || sim_age(&w->res[i]) >= AGE_ADULT) who = sim_name(&w->res[i]);
        }
    if (who) sim_logf("%s家的房子毁了", who);
}

static bool home_ok(const world_t *w, const resident_t *r)
{
    return r->home != NO_ID && sim_is_house(OBJ(w->tiles[r->home]));
}

/* ---------- 居民：决策 ---------- */

static void drop_task(world_t *w, int i)
{
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].held == i + 1) w->ani[k].held = 0;
}

static void set_idle(resident_t *r, int wait)
{
    r->state = S_IDLE;
    r->goal = G_NONE;
    r->timer = (uint16_t)wait;
    r->step = 0;
    r->nx = r->tx;
    r->ny = r->ty;
}

static void go(resident_t *r, int goal, int idx)
{
    r->goal = (uint8_t)goal;
    r->gx = (uint8_t)(idx % MAP_W);
    r->gy = (uint8_t)(idx / MAP_W);
    r->state = S_MOVE;
    r->timer = 0;
}

static bool reachable(world_t *w, int i, int idx)
{
    s_self = i;
    s_goal_idx = idx;
    return bfs(w, w->res[i].tx, w->res[i].ty, pred_goal, NULL) >= 0;
}

static int camp_idx(const world_t *w, int i)
{
    int x, y;
    if (!sim_camp_spot(w, i, &x, &y)) return IDX(w->res[i].tx, w->res[i].ty);
    return IDX(x, y);
}
static bool camp_safe(const world_t *w, int i) { return sim_danger[camp_idx(w, i)] == 0; }
static void go_camp(world_t *w, int i, int goal) { go(&w->res[i], goal, camp_idx(w, i)); }

static void go_sleep(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (home_ok(w, r) && !sim_danger[r->home] && reachable(w, i, r->home)) go(r, G_SLEEP, r->home);
    else if (camp_safe(w, i)) go_camp(w, i, G_SLEEP);
    else go(r, G_SLEEP, IDX(r->tx, r->ty));
}

static bool try_find(world_t *w, int i, sim_pred_t pred, int goal)
{
    resident_t *r = &w->res[i];
    s_self = i;
    int idx = bfs(w, r->tx, r->ty, pred, NULL);
    if (idx < 0) return false;
    go(r, goal, idx);
    return true;
}

/* small_only：只抓兔子（胆小的人、少年和老人也敢） */
static bool try_hunt(world_t *w, int i, bool small_only)
{
    resident_t *r = &w->res[i];
    int best = -1, bestd = 999;
    int hunters[A_KINDS] = {0};
    for (int j = 0; j < MAX_RES; j++)
        if (w->res[j].alive && w->res[j].goal == G_HUNT) hunters[w->ani[w->res[j].tgt % MAX_ANI].kind % A_KINDS]++;
    for (int k = 0; k < MAX_ANI; k++) {
        const animal_t *a = &w->ani[k];
        if (!a->alive || a->held || a->kind == A_WOLF || sim_danger[IDX(a->tx, a->ty)]) continue;
        if (small_only && a->kind != A_RABBIT) continue;
        /* 只剩一两头时很难找到，除非快饿死了否则不去追，给种群留条活路 */
        if (sim_kind_count(w, a->kind) - hunters[a->kind] <= 2 && r->food >= 150) continue;
        bool chased = false;
        for (int j = 0; j < MAX_RES; j++)
            if (j != i && w->res[j].alive && w->res[j].goal == G_HUNT && w->res[j].tgt == k) chased = true;
        if (chased) continue;
        int d = iabs(a->tx - r->tx) + iabs(a->ty - r->ty);
        if (a->kind == A_RABBIT) d += 4; /* 同样远近先打野猪，肉多 */
        if (d < bestd) { bestd = d; best = k; }
    }
    if (best < 0 || !reachable(w, i, IDX(w->ani[best].tx, w->ani[best].ty))) return false;
    go(r, G_HUNT, s_goal_idx);
    r->tgt = (uint8_t)best;
    r->timer = 70; /* 最多追这么多格 */
    return true;
}

static bool pred_dock(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_DOCK && free_tile(w, idx); }
/* 有码头就出海（渔夫总是去码头），否则在岸边钓 */
static bool try_fish(world_t *w, int i)
{
    const resident_t *r = &w->res[i];
    bool fisher = sim_prof(r) == SK_FISH;
    if (sim_age(r) >= AGE_ADULT && (fisher || rn(w, 100) < 40) && try_find(w, i, pred_dock, G_FISH)) return true;
    return w->fish > 0 && try_find(w, i, sim_pred_shore, G_FISH);
}
static bool try_berry(world_t *w, int i) { return try_find(w, i, pred_berry, G_BERRY); }
static bool try_harvest(world_t *w, int i) { return try_find(w, i, pred_ripe, G_HARVEST); }

/* 按性格和年纪决定找食物的顺序；force 表示自己已经饿了 */
static bool food_job(world_t *w, int i, bool force)
{
    resident_t *r = &w->res[i];
    int age = sim_age(r);
    if (try_harvest(w, i)) return true;
    if (age < AGE_ADULT || age >= AGE_ELDER || r->sick)
        return try_berry(w, i) || try_fish(w, i) || (!r->sick && try_hunt(w, i, true));
    switch (sim_prof(r)) { /* 先干本行 */
    case SK_HUNT:
        if (r->trait != TR_TIMID && try_hunt(w, i, false)) return true;
        break;
    case SK_FISH:
        if (try_fish(w, i)) return true;
        break;
    case SK_FARM:
        if (try_berry(w, i)) return true;
        break;
    default: break;
    }
    if (r->trait == TR_BRAVE) return try_hunt(w, i, false) || try_fish(w, i) || try_berry(w, i);
    if (r->trait == TR_TIMID)
        return try_berry(w, i) || try_hunt(w, i, true) || try_fish(w, i) ||
               (force && r->food < 150 && try_hunt(w, i, false));
    int k = rn(w, 100);
    if (k < 40) return try_berry(w, i) || try_fish(w, i) || try_hunt(w, i, false);
    if (k < 70) return try_fish(w, i) || try_berry(w, i) || try_hunt(w, i, false);
    return try_hunt(w, i, false) || try_fish(w, i) || try_berry(w, i);
}

static void wander(world_t *w, int i, int goal)
{
    resident_t *r = &w->res[i];
    int bx = r->tx, by = r->ty;
    int k = rn(w, 100);
    if (k < 40) {
        bx = w->camp_x;
        by = w->camp_y;
    } else if (k < 70 && home_ok(w, r)) {
        bx = r->home % MAP_W;
        by = r->home / MAP_W;
    }
    for (int tries = 0; tries < 6; tries++) {
        int x = bx + rn(w, 9) - 4, y = by + rn(w, 9) - 4;
        if (!inmap(x, y) || !sim_walkable(w, IDX(x, y)) || sim_danger[IDX(x, y)]) continue;
        if (!reachable(w, i, IDX(x, y))) continue;
        go(r, goal, s_goal_idx);
        return;
    }
    set_idle(r, 20 + rn(w, 30));
}

static bool try_chat(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int j = rn(w, MAX_RES);
    for (int k = 0; k < MAX_RES; k++, j = (j + 1) % MAX_RES) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->away || o->state == S_SLEEP || o->goal == G_FLEE || o->goal == G_HUNT) continue;
        if (w->feud[i][j] >= 40) continue; /* 正在气头上，不想搭理 */
        if (sim_age(o) < AGE_CHILD || cheb(r->tx, r->ty, o->tx, o->ty) > 9) continue;
        if (sim_danger[IDX(o->tx, o->ty)] || !reachable(w, i, IDX(o->tx, o->ty))) continue;
        go(r, G_CHAT, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

static bool is_parent(const resident_t *p, const resident_t *c) { return c->father == p->uid || c->mother == p->uid; }
static bool kin(const resident_t *a, const resident_t *b)
{
    if (is_parent(a, b) || is_parent(b, a)) return true;
    return (a->father != NO_ID && a->father == b->father) || (a->mother != NO_ID && a->mother == b->mother);
}

static bool someone_doing(const world_t *w, int self, int goal, int tgt)
{
    for (int j = 0; j < MAX_RES; j++)
        if (j != self && w->res[j].alive && w->res[j].goal == goal && w->res[j].tgt == tgt) return true;
    return false;
}

/* 有没有饿着的婴儿需要我去喂 */
static bool try_tend(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (w->food == 0) return false;
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *b = &w->res[j];
        if (j == i || !b->alive || b->away) continue;
        if (sim_age(b) >= AGE_CHILD) { /* 病人、老人躺在屋里：家人和善良的人送饭过去 */
            if (b->food >= 350 || !(b->sick || sim_age(b) >= AGE_OLD) || !sim_in_house(w, b)) continue;
            if (r->trait != TR_KIND && !kin(r, b) && r->partner != j) continue;
        } else {
            if (b->food >= 450) continue;
            bool mine = is_parent(r, b);
            if (!mine) { /* 孤儿，或者父母顾不上：善良的人会搭把手，快饿坏了谁都会管 */
                if (r->trait != TR_KIND && b->food >= 200) continue;
            }
        }
        if (someone_doing(w, i, G_TEND, j) || sim_danger[IDX(b->tx, b->ty)]) continue;
        if (!reachable(w, i, IDX(b->tx, b->ty))) continue;
        go(r, G_TEND, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

static bool try_help(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->hp >= 400 || sim_age(o) < AGE_CHILD) continue;
        if (r->trait != TR_KIND && w->rel[i][j] < 40 && r->partner != j && !kin(r, o)) continue;
        if (someone_doing(w, i, G_HELP, j) || sim_danger[IDX(o->tx, o->ty)]) continue;
        if (!reachable(w, i, IDX(o->tx, o->ty))) continue;
        go(r, G_HELP, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

/* 没有家：先投靠伴侣或父母，再找空房，最后自己盖 */
static void adopt_home(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (home_ok(w, r)) return;
    r->home = NO_ID;
    if (r->partner != NO_SLOT && home_ok(w, &w->res[r->partner])) {
        r->home = w->res[r->partner].home;
        return;
    }
    if (sim_age(r) < AGE_ADULT)
        for (int j = 0; j < MAX_RES; j++)
            if (w->res[j].alive && is_parent(&w->res[j], r) && home_ok(w, &w->res[j])) {
                r->home = w->res[j].home;
                return;
            }
}

static bool try_claim(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!sim_houses) return false;
    for (int t = 0; t < MAP_N; t++) {
        if (!sim_is_house(OBJ(w->tiles[t])) || sim_danger[t]) continue;
        bool owned = false;
        for (int j = 0; j < MAX_RES; j++)
            if (w->res[j].alive && w->res[j].home == t) owned = true;
        if (owned) continue;
        r->home = (uint16_t)t;
        sim_logf("%s搬进了一间空屋", sim_name(r));
        return true;
    }
    return false;
}

static bool shrine_ok(const world_t *w) { return w->shrine != NO_ID && OBJ(w->tiles[w->shrine]) == O_SHRINE; }

/* 走到祭石旁边祈祷 */
static bool try_pray(world_t *w, int i)
{
    int at = w->statue < MAP_N && OBJ(w->tiles[w->statue]) == O_STATUE ? w->statue : shrine_ok(w) ? w->shrine : -1;
    if (at < 0 || sim_danger[at]) return false;
    int sx = at % MAP_W, sy = at / MAP_W, d = rn(w, 4);
    for (int k = 0; k < 4; k++, d = (d + 1) & 3) {
        int x = sx + DX[d], y = sy + DY[d];
        if (!inmap(x, y) || !sim_walkable(w, IDX(x, y)) || !reachable(w, i, IDX(x, y))) continue;
        go(&w->res[i], G_PRAY, IDX(x, y));
        return true;
    }
    return false;
}

/* 最近的恶狼离这个人几格（没有则 99） */
static int wolf_dist(const world_t *w, const resident_t *r, int *which)
{
    int best = 99;
    for (int k = 0; k < MAX_ANI; k++) {
        const animal_t *a = &w->ani[k];
        if (!a->alive || a->kind != A_WOLF || !a->fierce) continue;
        int d = cheb(a->tx, a->ty, r->tx, r->ty);
        if (d < best) {
            best = d;
            if (which) *which = k;
        }
    }
    return best;
}
static bool is_fighter(const resident_t *r)
{
    int age = sim_age(r);
    return r->trait == TR_BRAVE && age >= AGE_ADULT && age < AGE_ELDER && !r->sick && r->hp >= 500;
}
/* 篝火三格之内、自家屋里，是狼不敢来的地方 */
static bool is_sheltered(const world_t *w, const resident_t *r)
{
    return sim_in_house(w, r) || (w->fire_lit && cheb(r->tx, r->ty, w->camp_x, w->camp_y) <= 3);
}
static void go_refuge(world_t *w, int i, int goal)
{
    resident_t *r = &w->res[i];
    if (home_ok(w, r) && !sim_danger[r->home] && reachable(w, i, r->home)) go(r, goal, r->home);
    else if (camp_safe(w, i)) go_camp(w, i, goal);
    else set_idle(r, 20);
}

static bool bad_weather(const world_t *w) { return w->weather == W_STORM || w->weather == W_SNOW || w->weather == W_THUNDER; }

static void start_act(resident_t *r, int ticks);
static void arrive(world_t *w, int i);

/* ---------- 自由意愿：闲下来想做什么 ---------- */
/* 像模拟人生：每个人有"娱乐""社交"两项需求，会随时间下降；
 * 干完活、吃饱睡足之后，按需求、性格、季节、时辰给各种消遣打分，按分数随机挑一件去做。 */
enum { L_WORK, L_CHAT, L_SWIM, L_STROLL, L_GAZE, L_DANCE, L_NAP, L_FLOWER, L_DATE, L_SNOW, L_STORY, L_REST, L_MAKEUP, L_N };

static bool is_leisure(int g) { return g == G_NONE || g == G_REST || g == G_WANDER || g == G_CHAT || (g >= G_SWIM && g <= G_PLAY); }
static void add_need(uint16_t *v, int d)
{
    int t = *v + d;
    *v = (uint16_t)(t < 0 ? 0 : t > 1000 ? 1000 : t);
}
static bool clear_sky(const world_t *w) { return w->weather == W_SUNNY || w->weather == W_CLOUDY; }

/* 被邀请的一方：手头没事或者也在消遣，才会停下来一起玩 */
static bool engageable(const resident_t *o)
{
    return o->alive && !o->away && o->state != S_SLEEP && o->step == 0 && (o->state == S_IDLE || is_leisure(o->goal)) && !o->carry_n &&
           sim_age(o) >= AGE_CHILD;
}
static void engage(world_t *w, int i, int j, int goal, int ticks)
{
    resident_t *r = &w->res[i], *o = &w->res[j];
    o->goal = (uint8_t)goal;
    o->tgt = (uint8_t)i;
    o->nx = o->tx;
    o->ny = o->ty;
    o->face = r->tx < o->tx;
    start_act(o, ticks);
    (void)w;
}

static bool try_follow(world_t *w, int i, int j, int goal)
{
    resident_t *r = &w->res[i];
    const resident_t *o = &w->res[j];
    if (j == i || j >= MAX_RES || !o->alive || o->away || o->state == S_SLEEP) return false;
    if (sim_danger[IDX(o->tx, o->ty)] || cheb(r->tx, r->ty, o->tx, o->ty) > 14 || !reachable(w, i, IDX(o->tx, o->ty))) return false;
    go(r, goal, s_goal_idx);
    r->tgt = (uint8_t)j;
    r->timer = 80;
    return true;
}

/* 找一个玩伴：kids=true 只找孩子 */
static int pick_mate(world_t *w, int i, bool kids)
{
    const resident_t *r = &w->res[i];
    int j = rn(w, MAX_RES);
    for (int k = 0; k < MAX_RES; k++, j = (j + 1) % MAX_RES) {
        const resident_t *o = &w->res[j];
        if (j == i || !engageable(o) || cheb(r->tx, r->ty, o->tx, o->ty) > 12 || w->feud[i][j] >= 40) continue;
        int a = sim_age(o);
        if (kids ? a >= AGE_WORK : a >= AGE_ELDER) continue;
        return j;
    }
    return -1;
}

static bool pred_flower(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_FLOWER && free_tile(w, idx); }

/* 心上人：伴侣；没有伴侣就是关系最好的、能成家的异性 */
static int sweetheart(const world_t *w, int i)
{
    const resident_t *r = &w->res[i];
    if (r->partner != NO_SLOT) return w->res[r->partner].alive ? r->partner : -1;
    if (sim_age(r) < AGE_ADULT) return -1;
    int best = -1, bv = 25;
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->sex == r->sex || o->partner != NO_SLOT || sim_age(o) < AGE_ADULT || kin(r, o)) continue;
        if (w->rel[i][j] > bv) {
            bv = w->rel[i][j];
            best = j;
        }
    }
    return best;
}

static bool kids_awake(const world_t *w)
{
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *o = &w->res[j];
        if (o->alive && sim_age(o) >= AGE_CHILD && sim_age(o) < AGE_WORK && o->state != S_SLEEP) return true;
    }
    return false;
}

static int storyteller(const world_t *w)
{
    for (int j = 0; j < MAX_RES; j++)
        if (w->res[j].alive && w->res[j].goal == G_STORY) return j;
    return -1;
}

/* 去一个随机的地方：sand=true 偏向沙滩 */
static bool go_spot(world_t *w, int i, int goal, int radius, bool sand)
{
    resident_t *r = &w->res[i];
    s_self = i; /* taken() 要知道是谁在找 */
    for (int tries = 0; tries < 10; tries++) {
        int x = r->tx + rn(w, radius * 2 + 1) - radius, y = r->ty + rn(w, radius * 2 + 1) - radius;
        if (!inmap(x, y)) continue;
        int idx = IDX(x, y);
        if (!sim_walkable(w, idx) || sim_danger[idx] || taken(w, idx)) continue;
        if (sand && tries < 7 && TERR(w->tiles[idx]) != T_SAND) continue;
        if (!reachable(w, i, idx)) continue;
        go(r, goal, idx);
        return true;
    }
    return false;
}

static bool do_leisure(world_t *w, int i, int act)
{
    resident_t *r = &w->res[i];
    int j;
    switch (act) {
    case L_CHAT: return try_chat(w, i);
    case L_SWIM: return try_find(w, i, sim_pred_shore, G_SWIM);
    case L_STROLL: return go_spot(w, i, G_STROLL, 8, true);
    case L_GAZE: return go_spot(w, i, G_GAZE, 5, false);
    case L_DANCE:
        if (!camp_safe(w, i)) return false;
        go_camp(w, i, G_DANCE);
        return true;
    case L_NAP:
        if (home_ok(w, r) && reachable(w, i, r->home)) { go(r, G_NAP, r->home); return true; }
        return go_spot(w, i, G_NAP, 4, false);
    case L_FLOWER:
        j = sweetheart(w, i);
        if (j < 0 || !try_find(w, i, pred_flower, G_FLOWER)) return false;
        r->tgt = (uint8_t)j;
        return true;
    case L_DATE:
        j = r->partner;
        if (j == NO_SLOT || !engageable(&w->res[j])) return false;
        return try_follow(w, i, j, G_DATE);
    case L_SNOW:
        j = pick_mate(w, i, false);
        return j >= 0 && try_follow(w, i, j, G_SNOW);
    case L_STORY:
        if (storyteller(w) >= 0 || !camp_safe(w, i)) return false;
        go_camp(w, i, G_STORY);
        return true;
    case L_REST: wander(w, i, rn(w, 2) ? G_REST : G_WANDER); return true;
    case L_MAKEUP:
        j = sim_foe(w, i, 30);
        return j >= 0 && try_follow(w, i, j, G_MAKEUP);
    default: return false;
    }
}

/* 给每件事打分，按分数平方加权随机挑一件。返回 L_WORK 表示去干活 */
static int choose(world_t *w, int i, bool need_food, bool no_work)
{
    const resident_t *r = &w->res[i];
    int age = sim_age(r), hour = sim_hour(w), season = sim_season(w);
    int F = (1000 - r->fun) / 10, S = (1000 - r->social) / 10; /* 0..100，越大越想要 */
    bool day = hour >= 7 && hour < 18, sky = clear_sky(w);
    int sc[L_N] = {0};

    int work = 60 - F / 3;
    if (r->trait == TR_DILIGENT) work += 25;
    if (r->trait == TR_LAZY) work -= 20;
    if (need_food) work += 50;
    if (w->wood < 6 || (w->proj_type && hour < 17)) work += 15;
    if (w->faith >= 60) work += 10;
    if (r->energy < 350) work -= 25;
    if (age >= AGE_ELDER) work -= 30;
    if (r->sick) work -= 40;
    if (hour >= 18) work -= 35; /* 傍晚：收工了 */
    if (season == SEASON_AUTUMN && (w->food < sim_pop(w) * 12 || w->wood < 25)) work += 25; /* 秋天：为过冬囤粮备柴 */
    sc[L_WORK] = no_work ? 0 : work < 5 ? 5 : work;

    sc[L_CHAT] = S - 25 + (r->trait == TR_KIND ? 12 : 0);
    if (season == SEASON_SUMMER && hour >= 9 && hour < 18 && sky && age < AGE_OLD)
        sc[L_SWIM] = F * 4 / 5 - 5 + (r->trait == TR_BRAVE ? 10 : r->trait == TR_TIMID ? -15 : 0);
    if (day && !bad_weather(w)) sc[L_STROLL] = F / 2;
    if (hour >= 19 && sky && season != SEASON_WINTER) sc[L_GAZE] = F * 3 / 5 + (r->partner != NO_SLOT ? 10 : 0);
    if (hour >= 17 && w->fire_lit && age < AGE_OLD) sc[L_DANCE] = (F + S) / 2 - 10;
    if (hour >= 11 && hour < 16 && r->energy < 700) sc[L_NAP] = (700 - r->energy) / 5 + (r->trait == TR_LAZY ? 15 : 0);
    if ((season == SEASON_SPRING || season == SEASON_SUMMER) && day) sc[L_FLOWER] = S / 2 + 5;
    if (r->partner != NO_SLOT && !bad_weather(w)) sc[L_DATE] = S / 3 + (hour >= 17 ? 12 : 0);
    if (season == SEASON_WINTER && day && w->weather != W_SNOW && age < AGE_OLD) sc[L_SNOW] = F * 7 / 10;
    if (age >= AGE_ELDER && hour >= 17 && kids_awake(w)) sc[L_STORY] = 45;
    if (sim_foe(w, i, 30) >= 0) sc[L_MAKEUP] = 10 + (r->trait == TR_KIND ? 25 : 0) + (r->mood > 700 ? 10 : 0);
    sc[L_REST] = 4 + (r->trait == TR_LAZY ? 15 : 0) + (age >= AGE_ELDER ? 15 : 0) + (r->sick ? 30 : 0) + (r->energy < 350 ? 20 : 0);

    int total = 0;
    for (int k = 0; k < L_N; k++) {
        if (sc[k] < 0) sc[k] = 0;
        total += sc[k] * sc[k];
    }
    int roll = rn(w, total > 0 ? total : 1);
    for (int k = 0; k < L_N; k++) {
        roll -= sc[k] * sc[k];
        if (roll < 0) return k;
    }
    return L_WORK;
}

/* ---------- 更聪明一点：救火、分工、结伴打猎、灾后清理、仪式、交易 ---------- */

static int count_goal(const world_t *w, int self, int goal)
{
    int n = 0;
    for (int j = 0; j < MAX_RES; j++) n += j != self && w->res[j].alive && !w->res[j].away && w->res[j].goal == goal;
    return n;
}

/* 这处火会不会烧到村子：营地八格之内，或者两格之内有房屋、粮仓、牧场 */
static bool village_near(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    if (cheb(x, y, w->camp_x, w->camp_y) <= 8) return true;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            if (!inmap(x + dx, y + dy)) continue;
            int o = OBJ(w->tiles[IDX(x + dx, y + dy)]);
            if (sim_is_house(o) || o == O_GRANARY || o == O_PEN || o == O_STATUE || o == O_SHIP) return true;
        }
    return false;
}

static int flame_near(const world_t *w, int idx, bool village)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            if (!inmap(x + dx, y + dy)) continue;
            int n = IDX(x + dx, y + dy);
            if (OBJ(w->tiles[n]) == O_FLAME && (!village || village_near(w, n))) return n;
        }
    return -1;
}

/* 救火的人站的地方：离火两格（不贴着火），手里的水泼得到 */
static bool pred_douse(const world_t *w, int idx)
{
    if (!sim_walkable(w, idx) || (sim_danger[idx] != 0 && sim_danger[idx] != 3) || taken(w, idx)) return false;
    return flame_near(w, idx, true) >= 0;
}

/* 打水的地方：水边，或者井边 */
static bool pred_water(const world_t *w, int idx) { return sim_pred_shore(w, idx) || (free_tile(w, idx) && near4(w, idx, -1, O_WELL)); }

static bool fire_threat(const world_t *w)
{
    if (!w->fires || w->fires > 40) return false; /* 烧成一片火海就救不了了 */
    for (int i = 0; i < MAP_N; i++)
        if (OBJ(w->tiles[i]) == O_FLAME && village_near(w, i)) return true;
    return false;
}

static bool try_firefight(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->carry == C_WATER) {
        if (fire_threat(w) && try_find(w, i, pred_douse, G_DOUSE)) return true;
        r->carry = C_NONE;
        return false;
    }
    if (!fire_threat(w) || count_goal(w, i, G_FETCH) + count_goal(w, i, G_DOUSE) >= 6) return false;
    return try_find(w, i, pred_water, G_FETCH);
}

static bool pred_ruin(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_RUIN && free_tile(w, idx); }
static bool pred_pen(const world_t *w, int idx) { return free_tile(w, idx) && near4(w, idx, -1, O_PEN); }

static bool pred_shipside(const world_t *w, int idx)
{
    return free_tile(w, idx) && TERR(w->tiles[idx]) == T_SAND && cheb(idx % MAP_W, idx / MAP_W, w->ship_x, w->ship_y) <= 1;
}

static bool can_trade(const world_t *w)
{
    int pop = sim_pop(w);
    return w->ship_h >= 2 && w->ship_deals < 2 && (w->food >= pop * 10 + 12 || w->wood >= 24 || w->stone >= 24);
}

/* 结伴打猎：有人在追野猪，身边还没人帮忙，就过去搭把手 */
static bool try_join_hunt(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->away || o->goal != G_HUNT || cheb(r->tx, r->ty, o->tx, o->ty) > 10) continue;
        int k = o->tgt % MAX_ANI;
        const animal_t *a = &w->ani[k];
        if (!a->alive || a->kind != A_BOAR) continue;
        int n = 0;
        for (int m = 0; m < MAX_RES; m++) n += w->res[m].alive && w->res[m].goal == G_HUNT && w->res[m].tgt == k;
        if (n >= 2 || !reachable(w, i, IDX(a->tx, a->ty))) continue;
        go(r, G_HUNT, s_goal_idx);
        r->tgt = (uint8_t)k;
        r->timer = 60;
        return true;
    }
    return false;
}

/* 到仪式的地方去：离中心两格以内找一块空地 */
static bool go_near(world_t *w, int i, int center, int goal)
{
    resident_t *r = &w->res[i];
    s_self = i;
    int cx = center % MAP_W, cy = center / MAP_W, d = rn(w, 25);
    for (int t = 0; t < 25; t++, d = (d + 7) % 25) {
        int x = cx + d % 5 - 2, y = cy + d / 5 - 2;
        if (!inmap(x, y) || (x == cx && y == cy)) continue;
        int idx = IDX(x, y);
        if (!sim_walkable(w, idx) || sim_danger[idx] || taken(w, idx) || !reachable(w, i, idx)) continue;
        go(r, goal, idx);
        return true;
    }
    return false;
}

static bool try_ceremony(world_t *w, int i)
{
    int h = sim_hour(w);
    if (!w->cer || !w->cer_h || h < 6 || h >= 21 || w->cer_idx >= MAP_N) return false;
    return go_near(w, i, w->cer_idx, w->cer == CER_FUNERAL ? G_MOURN : G_WEDDING);
}

/* 天象当前：跑出来看 */
static bool try_sky(world_t *w, int i)
{
    if (!w->sky || rn(w, 100) >= 70) return false;
    return go_spot(w, i, G_GAZE, 3, false);
}

static void decide_child(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->food < 450) {
        if (w->food > 0 && camp_safe(w, i)) { go_camp(w, i, G_EAT_CAMP); return; }
        if (try_find(w, i, pred_berry, G_EAT_BERRY)) return;
    }
    if (sim_fierce && r->food >= 200) { go_refuge(w, i, G_COWER); return; }
    if (w->fest_h && camp_safe(w, i)) { go_camp(w, i, G_FEAST); return; }
    if (w->sky && w->sky_h && try_sky(w, i)) return;
    if (sim_is_night(w) || r->energy < 150) { go_sleep(w, i); return; }
    if (bad_weather(w) && home_ok(w, r) && reachable(w, i, r->home)) { go(r, G_SHELTER, r->home); return; }
    if (try_ceremony(w, i)) return;
    /* 长老在讲故事：孩子们都跑去听 */
    if (storyteller(w) >= 0 && camp_safe(w, i) && rn(w, 100) < 85) { go_camp(w, i, G_LISTEN); return; }
    int season = sim_season(w), hour = sim_hour(w);
    int k = rn(w, 100), j;
    if (season == SEASON_SUMMER && hour >= 9 && hour < 18 && clear_sky(w) && k < 25 && try_find(w, i, sim_pred_shore, G_SWIM)) return;
    if (season == SEASON_WINTER && w->weather != W_SNOW && k < 40 && (j = pick_mate(w, i, false)) >= 0 && try_follow(w, i, j, G_SNOW)) return;
    if (k < 70 && ((j = pick_mate(w, i, true)) >= 0 || (j = pick_mate(w, i, false)) >= 0) && try_follow(w, i, j, G_PLAY)) return;
    if (r->social < 600 && rn(w, 100) < 50 && try_chat(w, i)) return;
    if (rn(w, 100) < 70 && go_spot(w, i, G_STROLL, 6, false)) return;
    wander(w, i, G_WANDER);
}

static void decide(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int age = sim_age(r);
    bool night = sim_is_night(w);
    bool worker = age >= AGE_WORK && !r->sick;
    bool strong = age >= AGE_ADULT && age < AGE_ELDER && !r->sick;

    adopt_home(w, i);
    if (age < AGE_WORK) { decide_child(w, i); return; }

    if (r->carry == C_FLOWER || r->carry == C_GOODS) r->carry = C_NONE, r->carry_n = 0; /* 花没送出去，随手插在路边 */
    if (r->carry == C_WATER && !r->carry_n) { /* 提着水：赶去泼火 */
        if (try_firefight(w, i)) return;
        r->carry = C_NONE;
    }
    if (r->carry_n) {
        if (camp_safe(w, i)) go_camp(w, i, G_HAUL);
        else wander(w, i, G_REST);
        return;
    }
    /* 恶狼在岛上：勇敢的人提着家伙去打，其他人没饿到不行就躲着 */
    if (sim_fierce) {
        int k = -1, d = wolf_dist(w, r, &k);
        if (is_fighter(r) && d <= 12 && !w->ani[k].held && reachable(w, i, IDX(w->ani[k].tx, w->ani[k].ty))) {
            go(r, G_FIGHT, s_goal_idx);
            r->tgt = (uint8_t)k;
            r->timer = 60;
            return;
        }
        if (!is_fighter(r) && r->food >= 200 && !night) {
            go_refuge(w, i, G_COWER);
            return;
        }
    }
    /* 着火了：勇敢、身体好的人去打水救火，先保住村子 */
    if (strong && r->trait != TR_TIMID && r->food >= 200 && w->fires && try_firefight(w, i)) return;
    if (w->fest_h && camp_safe(w, i)) { go_camp(w, i, G_FEAST); return; } /* 庆典：大家聚到篝火边 */
    if (w->sky && w->sky_h && try_sky(w, i)) return;                    /* 天象：都跑出来看 */
    if (r->energy < 100 && r->food >= 200) { go_sleep(w, i); return; }

    /* 善良的人会多忍一会儿，把仓库里的食物留给别人 */
    int hungry = (r->trait == TR_KIND) ? 300 : 420;
    if (r->food < hungry) {
        if (w->food > 0 && camp_safe(w, i)) { go_camp(w, i, G_EAT_CAMP); return; }
        if (try_find(w, i, pred_berry, G_EAT_BERRY)) return;
        if (food_job(w, i, true)) return;
    }
    if (try_tend(w, i)) return;
    if (try_help(w, i)) return;
    if (night) { go_sleep(w, i); return; }
    if (try_ceremony(w, i)) return; /* 婚礼、葬礼、新年：全岛的人都去 */
    if (r->sick && r->hp < 600) { /* 病重：在家或营地歇着 */
        if (home_ok(w, r) && reachable(w, i, r->home)) go(r, G_SHELTER, r->home);
        else wander(w, i, G_REST);
        return;
    }

    int pop = sim_pop(w);
    bool need_food = w->food < pop * 2;
    if (bad_weather(w) && !need_food) { /* 恶劣天气：有家回家，没家就守着篝火 */
        if (home_ok(w, r) && reachable(w, i, r->home)) { go(r, G_SHELTER, r->home); return; }
        if (w->weather == W_SNOW && camp_safe(w, i)) { go_camp(w, i, G_REST); return; }
    }

    /* 恐惧：神降灾太多，岛民惶惶不可终日，胆小的人尤其如此 */
    if (w->fear >= 40 && !need_food && rn(w, 100) < (w->fear - 30) / 2 + (r->trait == TR_TIMID ? 20 : 0)) {
        if (home_ok(w, r) && reachable(w, i, r->home)) go(r, G_COWER, r->home);
        else if (camp_safe(w, i)) go_camp(w, i, G_COWER);
        else set_idle(r, 30);
        return;
    }
    /* 信仰：立一块祭石，闲下来去拜一拜 */
    if (strong && w->faith >= 35 && w->faith > w->fear && !shrine_ok(w) && rn(w, 100) < 25 &&
        try_find(w, i, pred_shrine, G_SHRINE))
        return;
    if (w->faith >= 20 && rn(w, 100) < w->faith / 6 && try_pray(w, i)) return;

    int pop0 = sim_pop(w);
    int act = choose(w, i, w->food < pop0 * 2, false);
    if (act != L_WORK) {
        if (do_leisure(w, i, act)) return;
        if (rn(w, 100) < 50 && try_chat(w, i)) return;
    }

    /* 自家房子坏了先修 */
    if (worker && home_ok(w, r) && VAL(w->tiles[r->home]) < 70 && w->wood >= 2 && reachable(w, i, r->home)) {
        go(r, G_REPAIR, r->home);
        return;
    }
    /* 商船来了：会做买卖的人带上多余的东西去换 */
    if (strong && can_trade(w) && count_goal(w, i, G_TRADE) == 0 && sim_hour(w) < 19 &&
        (r->trait == TR_DILIGENT || sim_prof(r) == SK_CRAFT || rn(w, 100) < 30) && try_find(w, i, pred_shipside, G_TRADE)) {
        r->carry = C_GOODS;
        return;
    }
    /* 结伴打猎：有人在追野猪就去帮忙 */
    if (strong && r->trait != TR_TIMID && rn(w, 100) < (sim_prof(r) == SK_HUNT || r->trait == TR_BRAVE ? 60 : 25) && try_join_hunt(w, i))
        return;
    /* 分工：同一样活已经有足够的人在干，就去干别的 */
    bool autumn = sim_season(w) == SEASON_AUTUMN;
    int on_food = count_goal(w, i, G_HUNT) + count_goal(w, i, G_FISH) + count_goal(w, i, G_BERRY) + count_goal(w, i, G_HARVEST);
    bool food_crew = on_food >= (pop + 1) / 2 && on_food >= 2 && w->food > 0;
    int choppers = count_goal(w, i, G_CHOP);
    bool need_wood = w->wood < 6;
    if (strong && need_wood && (choppers < 2 || w->wood < 2) && (!need_food || r->trait == TR_KIND || rn(w, 100) < 30))
        if (try_find(w, i, pred_tree, G_CHOP)) return;
    if (worker && !food_crew && (need_food || (w->food < pop * (autumn ? 12 : 5) && rn(w, 100) < 50)))
        if (food_job(w, i, false)) return;
    if (worker && w->food < pop * (autumn ? 15 : 8) && rn(w, 100) < 60 && try_harvest(w, i)) return;
    /* 灾后：先把废墟清出来 */
    if (strong && rn(w, 100) < 25 && try_find(w, i, pred_ruin, G_CLEAR)) return;
    /* 牧场：早上喂猪 */
    if (worker && w->livestock && sim_hour(w) < 11 && count_goal(w, i, G_FEEDPEN) == 0 &&
        rn(w, 100) < (sim_prof(r) == SK_FARM ? 50 : 15) && try_find(w, i, pred_pen, G_FEEDPEN))
        return;

    /* 野猪多的时候，勇敢的人会主动去打猎；多到泛滥时谁都会去 */
    {
        int boars = sim_kind_count(w, A_BOAR), rabbits = sim_kind_count(w, A_RABBIT);
        if (strong && w->food < pop * 12) {
            if (((r->trait == TR_BRAVE && boars >= 6 && rn(w, 100) < 35) || (boars >= 9 && rn(w, 100) < 20)) &&
                try_hunt(w, i, false))
                return;
            if (rabbits >= 8 && rn(w, 100) < (r->trait == TR_BRAVE ? 25 : 10) && try_hunt(w, i, true)) return;
        }
    }
    /* 安家 */
    if (strong && !home_ok(w, r)) {
        if (try_claim(w, i)) { set_idle(r, 5); return; }
        if (w->wood >= 12 && try_find(w, i, pred_site, G_BUILD)) return;
        if (w->wood < 12 && rn(w, 100) < 60 && try_find(w, i, pred_tree, G_CHOP)) return;
    }
    /* 公共建筑：工匠最积极 */
    if (strong && w->proj_type && w->proj_idx < MAP_N && OBJ(w->tiles[w->proj_idx]) == O_SITE &&
        rn(w, 100) < (sim_prof(r) == SK_CRAFT ? 60 : 25) && !sim_danger[w->proj_idx] && reachable(w, i, w->proj_idx)) {
        go(r, G_CONSTRUCT, w->proj_idx);
        return;
    }
    if (strong && w->era >= ERA_BRONZE && home_ok(w, r) && OBJ(w->tiles[r->home]) == O_HOUSE1 && w->wood >= 12 && w->stone >= 4 &&
        rn(w, 100) < (r->trait == TR_LAZY ? 10 : 40) && reachable(w, i, r->home)) {
        go(r, G_UPGRADE, r->home);
        return;
    }
    /* 开垦：每人一块半地为限 */
    int want = pop * 3 / 2 > 20 ? 20 : pop * 3 / 2;
    if (worker && sim_season(w) != SEASON_WINTER && sim_farms < want &&
        rn(w, 100) < (r->trait == TR_DILIGENT || sim_prof(r) == SK_FARM ? 60 : 30)) {
        if (sim_farms > 0 && try_find(w, i, pred_till_adj, G_TILL)) return;
        if (try_find(w, i, pred_till_any, G_TILL)) return;
    }
    /* 清理碎石：碎石越多越积极；缺石料时也先从碎石下手 */
    if (strong && sim_rubble && rn(w, 100) < (sim_rubble > 20 ? 40 : 20) && try_find(w, i, pred_rubble, G_MINE)) return;
    if (strong && sim_houses > 0 && w->stone < 12 + w->era * 8 && count_goal(w, i, G_MINE) < 2 && rn(w, 100) < (sim_prof(r) == SK_CRAFT ? 35 : 20) &&
        (try_find(w, i, pred_rubble, G_MINE) || try_find(w, i, pred_mine, G_MINE)))
        return;
    if (strong && w->wood < (autumn ? 25 : 16) && choppers < 2 && rn(w, 100) < 40)
        if (try_find(w, i, pred_tree, G_CHOP)) return;
    /* 没什么急活：勤快的人多囤点粮和柴，其他人找点自己喜欢的事做 */
    if (strong && rn(w, 100) < (r->trait == TR_DILIGENT ? 60 : r->trait == TR_LAZY ? 10 : 30)) {
        if (w->food < pop * 15 && food_job(w, i, false)) return;
        if (w->wood < 30 && try_find(w, i, pred_tree, G_CHOP)) return;
    }
    for (int t = 0; t < 2; t++)
        if (do_leisure(w, i, choose(w, i, false, true))) return;
    if (try_chat(w, i)) return;
    wander(w, i, G_WANDER);
}

/* ---------- 居民：行动 ---------- */

static void start_act(resident_t *r, int ticks)
{
    r->state = S_ACT;
    r->timer = (uint16_t)ticks;
}

static bool near_tgt(const world_t *w, const resident_t *r)
{
    const resident_t *o = &w->res[r->tgt % MAX_RES];
    return o->alive && cheb(r->tx, r->ty, o->tx, o->ty) <= 2;
}

static void arrive(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    int o = OBJ(w->tiles[idx]), v = VAL(w->tiles[idx]);
    switch (r->goal) {
    case G_EAT_CAMP: start_act(r, 15); break;
    case G_EAT_BERRY:
    case G_BERRY:
        if (o == O_BERRY) start_act(r, sim_work_time(w, r, SK_FARM, 20));
        else set_idle(r, 0);
        break;
    case G_CHOP:
        if (o == O_TREE) start_act(r, sim_work_time(w, r, SK_CRAFT, 50));
        else set_idle(r, 0);
        break;
    case G_FISH: start_act(r, sim_work_time(w, r, SK_FISH, o == O_DOCK ? 110 : 60)); break;
    case G_CONSTRUCT:
        if (idx == w->proj_idx && o == O_SITE && w->proj_type) start_act(r, sim_work_time(w, r, SK_CRAFT, 100));
        else set_idle(r, 0);
        break;
    case G_HAUL: start_act(r, 6); break;
    case G_SLEEP: r->state = S_SLEEP; break;
    case G_REST: start_act(r, 60 + rn(w, 60)); break;
    case G_SHELTER: start_act(r, 80); break;
    case G_COWER: start_act(r, 100); break;
    case G_FEAST: start_act(r, 40); break;
    case G_PRAY: start_act(r, 40); break;
    case G_SHRINE:
        if (empty_grass(w, idx) && !shrine_ok(w)) start_act(r, 100);
        else set_idle(r, 0);
        break;
    case G_FLEE: start_act(r, 25); break;
    case G_CHAT:
        if (near_tgt(w, r) && w->res[r->tgt].state != S_SLEEP) {
            r->face = w->res[r->tgt].tx < r->tx;
            start_act(r, 30);
        } else {
            set_idle(r, 0);
        }
        break;
    case G_HELP:
    case G_TEND:
        if (near_tgt(w, r)) start_act(r, r->goal == G_HELP ? 40 : 20);
        else set_idle(r, 0);
        break;
    case G_BUILD:
        if (o == O_SITE && v == 0) {
            start_act(r, sim_work_time(w, r, SK_CRAFT, 120));
        } else if (empty_grass(w, idx) && w->wood >= 8) {
            w->wood -= 8;
            w->tiles[idx] = MKTILE(T_GRASS, O_SITE);
            start_act(r, sim_work_time(w, r, SK_CRAFT, 120));
        } else {
            set_idle(r, 0);
        }
        break;
    case G_UPGRADE:
        if (o == O_HOUSE1 && w->wood >= 6 && w->stone >= 4) {
            w->wood -= 6;
            w->stone -= 4;
            start_act(r, sim_work_time(w, r, SK_CRAFT, 150));
        } else {
            set_idle(r, 0);
        }
        break;
    case G_REPAIR:
        if (sim_is_house(o) && v < 100 && w->wood >= 2) {
            w->wood -= 2;
            start_act(r, sim_work_time(w, r, SK_CRAFT, 60));
        } else {
            set_idle(r, 0);
        }
        break;
    case G_TILL:
        if (empty_grass(w, idx)) start_act(r, sim_work_time(w, r, SK_FARM, 60));
        else set_idle(r, 0);
        break;
    case G_HARVEST:
        if (o == O_FARM && v >= 100) start_act(r, sim_work_time(w, r, SK_FARM, 30));
        else set_idle(r, 0);
        break;
    case G_MINE: start_act(r, sim_work_time(w, r, SK_CRAFT, 70)); break;
    case G_SWIM: /* 面朝水，下水 */
        r->tgt = 0;
        for (int d = 0; d < 4; d++) {
            int x = r->tx + DX[d], y = r->ty + DY[d];
            if (inmap(x, y) && sim_is_water(TERR(w->tiles[IDX(x, y)]))) { r->tgt = (uint8_t)d; break; }
        }
        if (DX[r->tgt]) r->face = DX[r->tgt] < 0;
        start_act(r, 60 + rn(w, 30));
        break;
    case G_STROLL: start_act(r, 15 + rn(w, 20)); break;
    case G_GAZE: start_act(r, 50 + rn(w, 30)); break;
    case G_DANCE: start_act(r, 40 + rn(w, 20)); break;
    case G_NAP: start_act(r, 80 + rn(w, 40)); break;
    case G_FLOWER:
        if (o == O_FLOWER) start_act(r, 10);
        else set_idle(r, 0);
        break;
    case G_STORY:
        sim_logf("%s在篝火边讲起了从前的故事", sim_name(r));
        start_act(r, 100);
        break;
    case G_LISTEN: {
        int t = storyteller(w);
        if (t >= 0) {
            r->face = w->res[t].tx < r->tx;
            start_act(r, w->res[t].state == S_ACT ? w->res[t].timer + 2 : 90);
        } else {
            set_idle(r, 0);
        }
        break;
    }
    case G_FETCH: start_act(r, 8); break;
    case G_DOUSE: {
        int f = flame_near(w, idx, false);
        if (f >= 0 && r->carry == C_WATER) {
            r->face = f % MAP_W < r->tx;
            start_act(r, 6);
        } else {
            set_idle(r, 0);
        }
        break;
    }
    case G_CLEAR:
        if (o == O_RUIN) start_act(r, sim_work_time(w, r, SK_CRAFT, 50));
        else set_idle(r, 0);
        break;
    case G_WEDDING:
    case G_MOURN:
        if (!w->cer) { set_idle(r, 0); break; }
        r->face = (int)(w->cer_idx % MAP_W) < r->tx;
        start_act(r, 40);
        break;
    case G_TRADE:
        if (w->ship_h && cheb(r->tx, r->ty, w->ship_x, w->ship_y) <= 1) {
            r->face = w->ship_x < r->tx;
            start_act(r, 30);
        } else {
            r->carry = C_NONE;
            set_idle(r, 0);
        }
        break;
    case G_FEEDPEN: start_act(r, 30); break;
    case G_GIFT:
    case G_DATE:
    case G_SNOW:
    case G_MAKEUP:
    case G_PLAY: {
        int j = r->tgt % MAX_RES, len = r->goal == G_GIFT ? 15 : r->goal == G_DATE ? 50 : r->goal == G_SNOW ? 30 : 25;
        resident_t *t = &w->res[j];
        if (cheb(r->tx, r->ty, t->tx, t->ty) > 1 || !t->alive) { set_idle(r, 0); break; }
        r->face = t->tx < r->tx;
        if (r->goal != G_GIFT && engageable(t)) engage(w, i, j, r->goal, len);
        else if (r->goal == G_GIFT && t->state == S_IDLE) t->face = r->tx < t->tx;
        start_act(r, len);
        if (r->goal == G_SNOW && rn(w, 100) < 30) sim_logf("%s和%s打起了雪仗", sim_name(r), sim_name(t));
        break;
    }
    default: start_act(r, 15 + rn(w, 40)); break;
    }
}

static void add_food(resident_t *r, int v)
{
    int f = r->food + v;
    r->food = (uint16_t)(f > 1000 ? 1000 : f);
}
static void add_mood(resident_t *r, int v)
{
    int f = r->mood + v;
    r->mood = (uint16_t)(f > 1000 ? 1000 : f);
}
static void tire(resident_t *r, int v) { r->energy = (uint16_t)(r->energy > v ? r->energy - v : 0); }

static void marry(world_t *w, int a, int b)
{
    resident_t *ra = &w->res[a], *rb = &w->res[b];
    ra->partner = (uint8_t)b;
    rb->partner = (uint8_t)a;
    sim_logf("%s和%s结为伴侣", sim_name(ra), sim_name(rb));
    sim_note(w, ra, EV_MARRY, sim_who(rb));
    sim_note(w, rb, EV_MARRY, sim_who(ra));
    w->marriages++;
    sim_on_marry(w, a, b);
    sim_sfx(SFX_CHIME);
    /* 还跟父母挤在一起的，成家后要自立门户 */
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *p = &w->res[j];
        if (!p->alive) continue;
        if (is_parent(p, ra) && p->home == ra->home) ra->home = NO_ID;
        if (is_parent(p, rb) && p->home == rb->home) rb->home = NO_ID;
    }
    if (home_ok(w, ra)) rb->home = ra->home; /* 住到一起；空出来的房子别人可以搬进去 */
    else if (home_ok(w, rb)) ra->home = rb->home;
    add_mood(ra, 200);
    add_mood(rb, 200);
}

static void bond(world_t *w, int a, int b, int d)
{
    resident_t *ra = &w->res[a], *rb = &w->res[b];
    int old = w->rel[a][b], v = old + d;
    if (v > 100) v = 100;
    w->rel[a][b] = w->rel[b][a] = (uint8_t)v;
    add_mood(ra, 30);
    add_mood(rb, 30);
    add_need(&ra->social, 250);
    add_need(&rb->social, 250);
    add_need(&ra->fun, 40);
    add_need(&rb->fun, 40);
    for (int t = 0; t < 2; t++) { /* 孩子跟大人聊天，会学到大人最拿手的本事 */
        resident_t *kid = t ? rb : ra, *adult = t ? ra : rb;
        int p = sim_prof(adult);
        if (sim_age(kid) >= AGE_CHILD && sim_age(kid) < AGE_ADULT && sim_age(adult) >= AGE_ADULT && p >= 0 &&
            kid->skill[p] < adult->skill[p])
            sim_skill_up(w, kid, p, 3);
    }
    if (old < 40 && v >= 40 && sim_age(ra) >= AGE_WORK && sim_age(rb) >= AGE_WORK)
        sim_logf("%s和%s成了朋友", sim_name(ra), sim_name(rb));
    if (v >= 70 && ra->sex != rb->sex && ra->partner == NO_SLOT && rb->partner == NO_SLOT &&
        sim_age(ra) >= AGE_ADULT && sim_age(rb) >= AGE_ADULT && !kin(ra, rb))
        marry(w, a, b);
    /* 面对面说话会传染 */
    if ((ra->sick || rb->sick) && rn(w, 100) < 35) {
        resident_t *t = ra->sick ? rb : ra;
        if (!t->sick && !t->immune) {
            sim_infect(w, ra->sick ? b : a);
            sim_logf("%s也染上了瘟疫", sim_name(t));
        }
    }
}

static void finish(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    int te = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    switch (r->goal) {
    case G_EAT_CAMP:
        if (w->food > 0) {
            w->food--;
            add_food(r, w->fire_lit ? 420 : 350); /* 有火就能烤熟了吃，更顶饿 */
        }
        break;
    case G_EAT_BERRY:
        if (o == O_BERRY) {
            w->tiles[idx] = MKTILE(te, O_BUSH);
            add_food(r, 400);
        }
        break;
    case G_BERRY:
        if (o == O_BERRY) {
            w->tiles[idx] = MKTILE(te, O_BUSH);
            r->carry = C_FOOD;
            r->carry_n = (uint8_t)(2 + r->skill[SK_FARM] / 50);
            tire(r, 15);
            sim_skill_up(w, r, SK_FARM, 1);
        }
        break;
    case G_CHOP:
        if (o == O_TREE) {
            w->tiles[idx] = MKTILE(te, O_STUMP);
            r->carry = C_WOOD;
            r->carry_n = (uint8_t)(4 + r->skill[SK_CRAFT] / 40 + w->era);
            tire(r, 35);
            sim_skill_up(w, r, SK_CRAFT, 2);
        }
        break;
    case G_FISH: {
        tire(r, 20);
        sim_skill_up(w, r, SK_FISH, 2);
        int p = (w->weather == W_STORM || w->weather == W_WIND) ? 40 : w->weather == W_SNOW ? 50 : 70;
        p += r->skill[SK_FISH] / 4 + w->era * 5;
        if (o == O_DOCK) { /* 出海：不靠岸边的鱼，收获也多 */
            if (w->weather == W_STORM || w->weather == W_SNOW) p -= 30;
            if (rn(w, 100) < p) {
                r->carry = C_FOOD;
                r->carry_n = (uint8_t)(5 + r->skill[SK_FISH] / 25 + w->era);
                if (rn(w, 100) < 2 + r->skill[SK_FISH] / 20) { /* 传说中的大鱼 */
                    r->carry_n = (uint8_t)(r->carry_n + 12);
                    sim_logf("%s钓到了一条传说中的大鱼！", sim_name(r));
                    sim_fx_add(FX_SPARK, r->tx, r->ty, 14);
                    sim_ach(w, ACH_FISH);
                    if (r->dream == DR_FISH) sim_dream_done(w, i);
                } else if (rn(w, 100) < 20) {
                    sim_logf("%s的渔船满载而归", sim_name(r));
                }
            }
            break;
        }
        if (sim_season(w) == SEASON_WINTER) p -= 20; /* 冬天鱼少 */
        if (w->fish > 0 && rn(w, 100) < p) {
            w->fish--;
            r->carry = C_FOOD;
            r->carry_n = (uint8_t)(3 + r->skill[SK_FISH] / 40);
            if (rn(w, 1000) < 5 + r->skill[SK_FISH] / 5) { /* 传说中的大鱼，岸边也偶尔能钓到 */
                r->carry_n = (uint8_t)(r->carry_n + 10);
                sim_logf("%s钓到了一条传说中的大鱼！", sim_name(r));
                sim_fx_add(FX_SPARK, r->tx, r->ty, 14);
                sim_ach(w, ACH_FISH);
                if (r->dream == DR_FISH) sim_dream_done(w, i);
            } else if (rn(w, 100) < 20) {
                sim_logf("%s钓上来一条大鱼", sim_name(r));
            }
        }
        break;
    }
    case G_HUNT: {
        animal_t *a = &w->ani[r->tgt % MAX_ANI];
        tire(r, 40);
        if (!a->alive || a->held != i + 1) break;
        a->held = 0;
        if (a->kind == A_RABBIT) { /* 兔子好抓，肉少，不伤人 */
            if (rn(w, 100) < 75 + r->skill[SK_HUNT] / 5) {
                a->alive = 0;
                r->carry = C_FOOD;
                r->carry_n = 3;
                sim_skill_up(w, r, SK_HUNT, 2);
                if (rn(w, 100) < 30) sim_logf("%s抓到了一只兔子", sim_name(r));
            } else {
                a->flee = 12;
            }
            break;
        }
        int p = (r->trait == TR_BRAVE ? 70 : r->trait == TR_TIMID ? 35 : 50) + r->skill[SK_HUNT] / 4 + w->era * 8;
        int helpers = 0;
        for (int j = 0; j < MAX_RES; j++)
            if (j != i && w->res[j].alive && w->res[j].goal == G_HUNT && w->res[j].state == S_ACT && w->res[j].tgt == r->tgt &&
                cheb(r->tx, r->ty, w->res[j].tx, w->res[j].ty) <= 2)
                helpers++;
        p += helpers * 25;
        sim_skill_up(w, r, SK_HUNT, 2);
        if (rn(w, 100) < p) {
            a->alive = 0;
            r->carry = C_FOOD;
            r->carry_n = (uint8_t)(8 + w->era);
            if (helpers) {
                sim_logf("%s和同伴合力捕获了一头野猪", sim_name(r));
                for (int j = 0; j < MAX_RES; j++)
                    if (j != i && w->res[j].alive && w->res[j].goal == G_HUNT && w->res[j].tgt == r->tgt && !w->res[j].carry_n) {
                        w->res[j].carry = C_FOOD;
                        w->res[j].carry_n = 4;
                        sim_skill_up(w, &w->res[j], SK_HUNT, 2);
                        bond(w, i, j, 6);
                    }
            } else {
                sim_logf("%s捕获了一头野猪", sim_name(r));
            }
        } else {
            a->flee = 10;
            if (rn(w, 100) < 40 / (1 + helpers)) {
                r->hp = (uint16_t)(r->hp > 150 ? r->hp - 150 : 1);
                sim_logf("%s被野猪撞伤了", sim_name(r));
            }
        }
        break;
    }
    case G_HAUL:
        if (r->carry == C_FOOD) w->food = (uint16_t)(w->food + r->carry_n > 999 ? 999 : w->food + r->carry_n);
        if (r->carry == C_WOOD) w->wood = (uint16_t)(w->wood + r->carry_n > 999 ? 999 : w->wood + r->carry_n);
        if (r->carry == C_STONE) w->stone = (uint16_t)(w->stone + r->carry_n > 999 ? 999 : w->stone + r->carry_n);
        r->carry = C_NONE;
        r->carry_n = 0;
        break;
    case G_CHAT:
        if (near_tgt(w, r)) {
            int j = r->tgt % MAX_RES;
            resident_t *t = &w->res[j];
            /* 话不投机：心情差、性格不合、旧怨未消，都容易吵起来；善良的人能忍 */
            int q = 6 + (r->mood < 450) * 6 + (t->mood < 450) * 6 + w->feud[i][j] / 5;
            if ((r->trait == TR_LAZY && t->trait == TR_DILIGENT) || (r->trait == TR_DILIGENT && t->trait == TR_LAZY)) q += 5;
            if (r->trait == TR_KIND || t->trait == TR_KIND) q /= 2;
            if (sim_age(r) >= AGE_WORK && sim_age(t) >= AGE_WORK && rn(w, 100) < q) {
                sim_feud_add(w, i, j, 30);
                w->rel[i][j] = w->rel[j][i] = (uint8_t)(w->rel[i][j] > 8 ? w->rel[i][j] - 8 : 0);
                r->mood = (uint16_t)(r->mood > 80 ? r->mood - 80 : 0);
                t->mood = (uint16_t)(t->mood > 80 ? t->mood - 80 : 0);
                if (w->quarrels < 0xFFFF) w->quarrels++;
                if (rn(w, 100) < 50) sim_logf("%s和%s吵了一架", sim_name(r), sim_name(t));
                r->goal = G_QUARREL;
                start_act(r, 20);
                if (t->state == S_ACT && t->goal == G_CHAT) t->goal = G_QUARREL, t->timer = 20, t->face = r->tx < t->tx;
                return;
            }
            bond(w, i, j, 8 + (r->trait == TR_KIND ? 4 : 0) + rn(w, 5));
        }
        break;
    case G_MAKEUP: {
        int j = r->tgt % MAX_RES;
        if (near_tgt(w, r) && w->feud[i][j]) {
            resident_t *t = &w->res[j];
            w->feud[i][j] = w->feud[j][i] = 0;
            bond(w, i, j, 15);
            sim_note(w, r, EV_MAKEUP, sim_who(t));
            sim_note(w, t, EV_MAKEUP, sim_who(r));
            sim_logf("%s主动找%s和好了", sim_name(r), sim_name(t));
            sim_fx_add(FX_HEART, r->tx, r->ty, 12);
            sim_ach(w, ACH_MAKEUP);
        }
        break;
    }
    case G_FETCH:
        r->carry = C_WATER;
        r->carry_n = 0;
        if (try_firefight(w, i)) return;
        r->carry = C_NONE;
        break;
    case G_DOUSE: {
        r->carry = C_NONE;
        int n = 0;
        for (int t = 0; t < 4; t++) { /* 一桶水能浇灭几处火苗 */
            int f = flame_near(w, idx, false);
            if (f < 0) break;
            sim_douse(w, f);
            n++;
        }
        if (n && rn(w, 100) < 25) sim_logf("%s提水救火", sim_name(r));
        if (n && rn(w, 100) < 10) sim_note(w, r, EV_FIRE, 0);
        tire(r, 30);
        break;
    }
    case G_CLEAR:
        if (o == O_RUIN) {
            w->tiles[idx] = MKTILE(te, O_NONE);
            r->carry = C_WOOD;
            r->carry_n = 2;
            sim_skill_up(w, r, SK_CRAFT, 1);
            tire(r, 30);
        }
        break;
    case G_WEDDING: add_need(&r->social, 200); add_need(&r->fun, 150); break;
    case G_MOURN: add_need(&r->social, 150); break;
    case G_TRADE:
        if (w->ship_h && cheb(r->tx, r->ty, w->ship_x, w->ship_y) <= 1) sim_do_trade(w, i);
        r->carry = C_NONE;
        break;
    case G_FEEDPEN:
        sim_skill_up(w, r, SK_FARM, 2);
        if (sim_season(w) <= SEASON_SUMMER && w->livestock && w->livestock < 6 && rn(w, 100) < 8) {
            w->livestock++;
            sim_logf("牧场里添了一窝小猪");
        }
        tire(r, 20);
        break;
    case G_SWIM:
        add_need(&r->fun, 320);
        add_mood(r, 60);
        tire(r, 40);
        break;
    case G_STROLL: add_need(&r->fun, 140); break;
    case G_GAZE:
        add_need(&r->fun, 260);
        add_mood(r, 50);
        if (r->partner != NO_SLOT && w->res[r->partner].goal == G_GAZE) bond(w, i, r->partner, 3);
        break;
    case G_DANCE:
        add_need(&r->fun, 240);
        add_need(&r->social, 120);
        for (int j = 0; j < MAX_RES; j++)
            if (j != i && w->res[j].alive && w->res[j].goal == G_DANCE && cheb(r->tx, r->ty, w->res[j].tx, w->res[j].ty) <= 3)
                bond(w, i, j, 2);
        break;
    case G_NAP:
        add_need(&r->energy, 280);
        add_need(&r->fun, 40);
        break;
    case G_FLOWER:
        if (o == O_FLOWER) {
            w->tiles[idx] = MKTILE(te, O_NONE);
            r->carry = C_FLOWER;
            r->carry_n = 0; /* 花不占背篓 */
            if (try_follow(w, i, r->tgt, G_GIFT)) return;
            r->carry = C_NONE;
        }
        break;
    case G_GIFT:
        r->carry = C_NONE;
        if (cheb(r->tx, r->ty, w->res[r->tgt % MAX_RES].tx, w->res[r->tgt % MAX_RES].ty) <= 2) {
            resident_t *t = &w->res[r->tgt % MAX_RES];
            add_mood(t, 120);
            add_need(&t->fun, 100);
            add_need(&r->social, 150);
            if (rn(w, 100) < 50) sim_logf("%s送给%s一束野花", sim_name(r), sim_name(t));
            bond(w, i, r->tgt, 12);
            sim_fx_add(FX_HEART, t->tx, t->ty, 12);
        }
        break;
    case G_DATE:
        add_need(&r->fun, 220);
        add_mood(r, 80);
        if (near_tgt(w, r)) {
            bond(w, i, r->tgt, 4);
            if (rn(w, 2)) sim_fx_add(FX_HEART, r->tx, r->ty, 12);
        }
        break;
    case G_SNOW:
    case G_PLAY:
        add_need(&r->fun, 260);
        if (near_tgt(w, r)) bond(w, i, r->tgt, 3);
        break;
    case G_STORY:
        add_need(&r->social, 220);
        add_need(&r->fun, 120);
        add_mood(r, 60);
        break;
    case G_LISTEN: {
        add_need(&r->fun, 240);
        add_need(&r->social, 120);
        int t = storyteller(w);
        if (t < 0) /* 讲完了：从长老那里学到一点本事 */
            for (int j = 0; j < MAX_RES; j++)
                if (w->res[j].alive && sim_age(&w->res[j]) >= AGE_ELDER && cheb(r->tx, r->ty, w->res[j].tx, w->res[j].ty) <= 3) {
                    int p = sim_prof(&w->res[j]);
                    if (p >= 0 && r->skill[p] < w->res[j].skill[p]) sim_skill_up(w, r, p, 4);
                    break;
                }
        break;
    }
    case G_HELP:
        if (near_tgt(w, r)) {
            resident_t *t = &w->res[r->tgt];
            int heal = t->sick ? 120 : 300; /* 病人只能缓一缓，治不好 */
            t->hp = (uint16_t)(t->hp + heal > 1000 ? 1000 : t->hp + heal);
            sim_logf("%s照料了受伤的%s", sim_name(r), sim_name(t));
            bond(w, i, r->tgt, 10);
        }
        break;
    case G_TEND:
        if (near_tgt(w, r) && w->food > 0) {
            w->food--;
            add_food(&w->res[r->tgt], 420);
            add_mood(&w->res[r->tgt], 60);
        }
        break;
    case G_BUILD:
        if (o == O_SITE) {
            w->tiles[idx] = MKT(te, O_HOUSE1, 100);
            sim_houses++;
            w->built++;
            r->home = (uint16_t)idx;
            if (r->partner != NO_SLOT && !home_ok(w, &w->res[r->partner])) w->res[r->partner].home = (uint16_t)idx;
            sim_logf("%s盖起了一间小屋", sim_name(r));
            sim_note(w, r, EV_HOUSE, 0);
            sim_skill_up(w, r, SK_CRAFT, 3);
            tire(r, 60);
            add_mood(r, 150);
        }
        break;
    case G_UPGRADE:
        if (o == O_HOUSE1) {
            w->tiles[idx] = MKT(te, O_HOUSE2, 100);
            sim_logf(w->era >= ERA_IRON ? "%s把小屋翻修成了石屋" : "%s把小屋翻修成了木屋", sim_name(r));
            sim_skill_up(w, r, SK_CRAFT, 3);
            tire(r, 60);
        }
        break;
    case G_REPAIR:
        if (sim_is_house(o)) w->tiles[idx] = MKT(te, o, 100);
        sim_skill_up(w, r, SK_CRAFT, 1);
        break;
    case G_TILL:
        if (empty_grass(w, idx)) {
            w->tiles[idx] = MKT(T_GRASS, O_FARM, 1);
            sim_farms++;
            tire(r, 30);
            sim_skill_up(w, r, SK_FARM, 2);
            if (sim_farms == 1) sim_logf("%s开垦了岛上第一块农田", sim_name(r));
        }
        break;
    case G_HARVEST:
        if (o == O_FARM && VAL(w->tiles[idx]) >= 100) {
            w->tiles[idx] = MKT(te, O_FARM, 1); /* 收完接着种 */
            r->carry = C_FOOD;
            r->carry_n = (uint8_t)(5 + r->skill[SK_FARM] / 30 + w->era);
            if (sim_season(w) == SEASON_AUTUMN) r->carry_n += 2; /* 秋收 */
            tire(r, 15);
            sim_skill_up(w, r, SK_FARM, 2);
        }
        break;
    case G_CONSTRUCT:
        if (idx == w->proj_idx && o == O_SITE && w->proj_type) {
            int step = 1 + r->skill[SK_CRAFT] / 50 + w->era;
            w->proj_prog = (uint8_t)(w->proj_prog + step);
            sim_skill_up(w, r, SK_CRAFT, 3);
            tire(r, 40);
            if (w->proj_prog >= w->proj_need) {
                int b = w->proj_type;
                w->tiles[idx] = MKT(te, b, 100);
                w->proj_type = 0;
                w->proj_idx = NO_ID;
                if (b == O_STATUE) {
                    w->statue = (uint16_t)idx;
                    w->tiles[idx] = MKT(te, b, 0);
                    sim_devotion(w, 20, -10);
                    sim_note(w, r, EV_BUILD, b);
                    sim_logf("神像落成了！岛民跪拜在神像前");
                    sim_fx_add(FX_GLOW, 0, 0, 12);
                    sim_sfx(SFX_BLESS);
                } else if (b == O_SHIP) {
                    w->tiles[idx] = MKT(te, b, 0);
                    sim_note(w, r, EV_BUILD, b);
                    sim_logf("大船造好了，随时可以出海远航");
                    sim_sfx(SFX_CHIME);
                } else if (b != O_FENCE) {
                    sim_note(w, r, EV_BUILD, b);
                    sim_logf("%s带着大家建成了%s", sim_name(r), sim_building_name(b));
                    sim_sfx(SFX_CHIME);
                    sim_fx_add(FX_SPARK, idx % MAP_W, idx / MAP_W, 14);
                }
            }
        }
        break;
    case G_SHRINE:
        if (empty_grass(w, idx) && !shrine_ok(w)) {
            w->tiles[idx] = MKTILE(te, O_SHRINE);
            w->shrine = (uint16_t)idx;
            sim_logf("%s垒起一块祭石，感谢神的眷顾", sim_name(r));
            sim_sfx(SFX_CHIME);
        }
        break;
    case G_FEAST:
        add_mood(r, 60);
        if (rn(w, 100) < 40) sim_fx_add(FX_NOTE, r->tx, r->ty, 12);
        break;
    case G_FIGHT: {
        animal_t *a = &w->ani[r->tgt % MAX_ANI];
        tire(r, 40);
        if (!a->alive || a->held != i + 1) break;
        a->held = 0;
        if (rn(w, 100) < 40 + r->skill[SK_HUNT] / 4 + w->era * 10) {
            a->alive = 0;
            add_mood(r, 150);
            sim_note(w, r, EV_WOLF_KILL, 0);
            sim_skill_up(w, r, SK_HUNT, 3);
            sim_logf("%s打死了一头恶狼", sim_name(r));
        } else {
            a->flee = 6;
            sim_sfx(SFX_WOLF);
            if (!sim_hurt(w, i, 280, D_WOLF)) return; /* 死了，不能再动他 */
            sim_logf("%s被恶狼咬伤了", sim_name(r));
        }
        break;
    }
    case G_PRAY:
        add_mood(r, 40);
        if (rn(w, 100) < 12) sim_devotion(w, 1, 0); /* 信仰靠祈祷代代相传 */
        break;
    case G_MINE: {
        r->carry = C_STONE;
        r->carry_n = (uint8_t)(2 + w->era);
        tire(r, 40);
        sim_skill_up(w, r, SK_CRAFT, 2);
        int rb = rubble_next_to(w, idx); /* 碎石凿完就没了，地面重新露出来 */
        if (rb >= 0) {
            w->tiles[rb] = MKTILE(T_GRASS, O_NONE);
            if (sim_rubble) sim_rubble--;
            r->carry_n = 3;
        }
        break;
    }
    default: break;
    }
    set_idle(r, 0);
}

static void move(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->step) {
        int here = IDX(r->tx, r->ty);
        bool slow = TERR(w->tiles[here]) == T_RIVER || sim_flooded(w, here);
        r->step += slow ? 1 : 2;
        if (r->step < TILE) return;
        r->tx = r->nx;
        r->ty = r->ny;
        r->step = 0;
    }
    if (r->goal == G_HUNT || r->goal == G_FIGHT) {
        animal_t *a = &w->ani[r->tgt % MAX_ANI];
        bool helper = a->held && a->held != i + 1; /* 别人已经缠住了：过去帮忙 */
        if (!a->alive || r->timer == 0 || (helper && (r->goal != G_HUNT || a->kind != A_BOAR))) { set_idle(r, 0); return; }
        r->timer--;
        if (cheb(r->tx, r->ty, a->tx, a->ty) <= 1) {
            if (helper) {
                r->face = a->tx < r->tx;
                start_act(r, 30);
                return;
            }
            a->held = (uint8_t)(i + 1);
            r->face = a->tx < r->tx;
            start_act(r, 30);
            return;
        }
        r->gx = a->tx;
        r->gy = a->ty;
    } else if (is_follow(r->goal)) { /* 跟着人走：送花、约会、打雪仗、追着玩 */
        const resident_t *o = &w->res[r->tgt % MAX_RES];
        if (!o->alive || o->state == S_SLEEP || r->timer == 0) {
            if (r->carry == C_FLOWER) r->carry = C_NONE, r->carry_n = 0;
            set_idle(r, 0);
            return;
        }
        r->timer--;
        if (cheb(r->tx, r->ty, o->tx, o->ty) <= 1) {
            arrive(w, i);
            return;
        }
        r->gx = o->tx;
        r->gy = o->ty;
    } else if (r->tx == r->gx && r->ty == r->gy) {
        arrive(w, i);
        return;
    }
    int dir = -1;
    s_self = i;
    s_goal_idx = IDX(r->gx, r->gy);
    if (bfs(w, r->tx, r->ty, pred_goal, &dir) < 0 || dir < 0) { set_idle(r, 10); return; }
    r->nx = (uint8_t)(r->tx + DX[dir]);
    r->ny = (uint8_t)(r->ty + DY[dir]);
    if (DX[dir]) r->face = DX[dir] < 0;
    r->step = 1;
}

/* 每 10 tick 结算一次需求和环境伤害 */
static void needs(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    bool asleep = r->state == S_SLEEP;
    bool night = sim_is_night(w);
    bool indoors = sim_in_house(w, r);
    bool baby = sim_age(r) < AGE_CHILD;

    int v = r->food - ((asleep || baby) ? 6 : 10) - ((w->weather == W_SNOW && !indoors) ? 2 : 0);
    r->food = (uint16_t)(v < 0 ? 0 : v);
    if (asleep || baby) {
        v = r->energy + (indoors ? 30 : w->fire_lit ? 25 : 16);
        r->energy = (uint16_t)(v > 1000 ? 1000 : v);
    } else {
        tire(r, r->sick ? 12 : 9);
    }
    if (r->hurt_cd) r->hurt_cd--;
    /* 娱乐、社交需求慢慢下降；孩子尤其坐不住 */
    if (!baby) {
        add_need(&r->fun, asleep ? -1 : sim_age(r) < AGE_WORK ? -14 : -10);
        add_need(&r->social, asleep ? -1 : -7);
    }

    /* 环境伤害 */
    if (TERR(w->tiles[idx]) == T_LAVA) { sim_die(w, i, D_LAVA); return; }
    if (OBJ(w->tiles[idx]) == O_FLAME || (w->fires && near4(w, idx, -1, O_FLAME))) {
        if (!sim_hurt(w, i, 110, D_FIRE)) return;
        if (!r->hurt_cd) {
            sim_logf("%s在火灾中受伤了", sim_name(r));
            r->hurt_cd = 40;
        }
    }
    if (w->flood >= 2 && sim_flooded(w, idx) && !sim_hurt(w, i, 40, D_FLOOD)) return;
    /* 暴风雪：屋外很快冻僵，小屋挡不住多少，木屋也只是勉强；只有紧挨着篝火才不冷 */
    if (w->weather == W_SNOW && !(w->fire_lit && cheb(r->tx, r->ty, w->camp_x, w->camp_y) <= 1)) {
        int cold = !indoors ? 20 : OBJ(w->tiles[idx]) == O_HOUSE1 ? 12 : 7;
        if (sim_age(r) < AGE_WORK || sim_age(r) >= AGE_ELDER) cold = cold * 3 / 2; /* 老人孩子更不禁冻 */
        if (w->wild_wx) cold /= 2;                                               /* 自然的风雪没那么狠 */
        if (cold && !sim_hurt(w, i, cold, D_COLD)) return;
    }
    /* 冬夜：屋外又不在篝火边，会冻伤 */
    if (sim_season(w) == SEASON_WINTER && night && !indoors && !(w->fire_lit && cheb(r->tx, r->ty, w->camp_x, w->camp_y) <= 3) &&
        !sim_hurt(w, i, 3, D_COLD))
        return;
    if (sim_season(w) == SEASON_WINTER && r->food) r->food--;
    if (r->sick && !sim_hurt(w, i, (sim_age(r) >= AGE_ELDER || sim_age(r) < AGE_WORK) ? 8 : 5, D_PLAGUE)) return;
    if (r->food == 0) {
        if (!sim_hurt(w, i, 9, D_STARVE)) return;
    } else if (r->food > 450 && r->hp < 1000 && !r->sick) {
        v = r->hp + (asleep ? 5 : 3);
        r->hp = (uint16_t)(v > 1000 ? 1000 : v);
    }

    int target = 500 + (r->food - 500) / 4 + (r->energy - 500) / 5;
    if (night) target += (indoors || w->fire_lit) ? 50 : -100;
    if (r->hp < 500) target -= 150;
    if (r->sick) target -= 150;
    target += w->faith / 5 - w->fear / 3;
    if (r->partner != NO_SLOT) target += 40;
    if (home_ok(w, r)) target += 40;
    if (!baby) target += (r->fun - 500) / 8 + (r->social - 500) / 8; /* 太无聊、太孤单都会不开心 */
    if (!indoors && (w->weather == W_RAIN || w->weather == W_STORM)) target -= 40;
    if (!indoors && w->weather == W_SNOW) target -= 100;
    if (target < 0) target = 0;
    if (target > 1000) target = 1000;
    if (r->mood < target) r->mood += 4;
    else if (r->mood > target) r->mood = (uint16_t)(r->mood > 4 ? r->mood - 4 : 0);
}

static bool in_danger(const resident_t *r)
{
    int d = sim_danger[IDX(r->tx, r->ty)];
    return d == 1 || d == 2 || (d == 3 && r->trait == TR_TIMID); /* 胆小的人离得老远就跑 */
}

void sim_res_tick(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->away) return; /* 出海远航 */
    if ((w->tick + (uint32_t)i * 3) % 10 == 0) needs(w, i);
    if (!r->alive) return;

    if (sim_age(r) < AGE_CHILD) { /* 婴儿：待在家里，白天醒着晚上睡 */
        if (home_ok(w, r)) {
            r->tx = r->nx = (uint8_t)(r->home % MAP_W);
            r->ty = r->ny = (uint8_t)(r->home / MAP_W);
        }
        r->state = sim_is_night(w) ? S_SLEEP : S_IDLE;
        r->goal = G_NONE;
        r->step = 0;
        return;
    }

    /* 恶狼逼近：不是去打狼的人，丢下手里的事往家里或篝火边跑 */
    if (sim_fierce && r->step == 0 && r->goal != G_FLEE && r->goal != G_FIGHT && r->state != S_SLEEP &&
        (w->tick + (uint32_t)i) % 5 == 0 && !is_sheltered(w, r) && wolf_dist(w, r, NULL) <= (sim_towers ? 7 : 4)) { /* 有瞭望塔，警报来得早 */
        drop_task(w, i);
        go_refuge(w, i, G_FLEE);
        return;
    }
    /* 灾害当前：放下手里的事先逃命（走到格子中间才转向） */
    if (s_hazard && r->step == 0 && r->goal != G_FLEE && (w->tick + (uint32_t)i) % 5 == 0 && in_danger(r)) {
        drop_task(w, i);
        s_self = i;
        int idx = bfs(w, r->tx, r->ty, pred_safe, NULL);
        if (idx >= 0) go(r, G_FLEE, idx);
        else set_idle(r, 10);
        return;
    }

    switch (r->state) {
    case S_IDLE:
        if (r->timer) r->timer--;
        else decide(w, i);
        break;
    case S_MOVE: move(w, i); break;
    case S_ACT:
        if (r->timer) r->timer--;
        else finish(w, i);
        break;
    case S_SLEEP:
        if ((w->tick + (uint32_t)i * 7) % 25) break;
        if (sim_is_night(w) ? r->food < 120 : r->energy >= 600 || r->food < 250) set_idle(r, 0);
        break;
    }
}

/* ---------- 神对单个人的眷顾与惩罚 ---------- */

void sim_bless_one(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!r->alive) return;
    r->hp = r->food = r->energy = 1000;
    r->sick = 0;
    add_mood(r, 400);
    sim_fx_add(FX_HEAL, r->tx, r->ty, 16);
    sim_fx_add(FX_SPARK, r->tx, r->ty, 16);
    sim_devotion(w, 4, -2);
    sim_sfx(SFX_BLESS);
    sim_logf("神赐福于%s", sim_name(r));
}

void sim_smite_one(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!r->alive) return;
    sim_devotion(w, -3, 8);
    sim_logf("神降罚于%s", sim_name(r));
    sim_strike(w, IDX(r->tx, r->ty), true);
}
