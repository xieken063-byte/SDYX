/*
 * 《造物主：孤岛》模拟核心（四）：剧情。
 * 主线章节按时间和时代推进；随机奇遇隔几天发生一次。故事发生后等神回应，
 * 一天之内没有回应，岛民就按最后一个选项（通常是"沉默"）自己拿主意。
 */
#include <stdio.h>
#include <string.h>

#include "sim_int.h"
#include "story.h"

#define rn sim_rn

#define WAIT_H 12 /* 故事发生后，神有半天的时间用神迹回应 */

char g_story_result[400]; /* 刚刚有了结局的故事：结果的文字（不进存档） */
char g_story_rtitle[64];
bool g_story_new;

static bool here(const resident_t *r) { return r->alive && !r->away; }
static bool flag(const world_t *w, int f) { return (w->story_flags >> f) & 1; }

static const story_t *current(const world_t *w)
{
    if (w->evt == 2 && w->evt_id < STORY_MAIN_N) return &STORY_MAIN[w->evt_id];
    if (w->evt == 1 && w->evt_id < STORY_TALE_N) return &STORY_TALE[w->evt_id];
    return NULL;
}

/* ---------- 条件与人选 ---------- */

static bool has_kids(const resident_t *r, const world_t *w)
{
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *c = &w->res[j];
        if (here(c) && (c->father == r->uid || c->mother == r->uid) && sim_age(c) < AGE_ADULT) return true;
    }
    return false;
}

static bool fits(const world_t *w, int role, int i, int who)
{
    const resident_t *r = &w->res[i];
    if (!here(r) || i == who) return false;
    int age = sim_age(r), p = sim_prof(r);
    switch (role) {
    case W_ANY: return age >= AGE_WORK;
    case W_ADULT: return age >= AGE_ADULT;
    case W_KID: return age >= AGE_CHILD && age < AGE_WORK;
    case W_TEEN: return age >= AGE_WORK && age < AGE_ADULT;
    case W_ELDER: return age >= AGE_ELDER;
    case W_MAN: return age >= AGE_ADULT && !r->sex;
    case W_WOMAN: return age >= AGE_ADULT && r->sex;
    case W_SINGLE: return age >= AGE_ADULT && age < 45 && r->partner == NO_SLOT;
    case W_PARENT: return age >= AGE_ADULT && has_kids(r, w);
    case W_SICK: return r->sick && age >= AGE_CHILD;
    case W_HUNT: return age >= AGE_WORK && p == SK_HUNT;
    case W_FARM: return age >= AGE_WORK && p == SK_FARM;
    case W_CRAFT: return age >= AGE_WORK && p == SK_CRAFT;
    case W_FISH: return age >= AGE_WORK && p == SK_FISH;
    case W_BRAVE: return age >= AGE_WORK && r->trait == TR_BRAVE;
    case W_TIMID: return age >= AGE_WORK && r->trait == TR_TIMID;
    case W_KIND: return age >= AGE_WORK && r->trait == TR_KIND;
    case W_LAZY: return age >= AGE_WORK && r->trait == TR_LAZY;
    case W_DILIGENT: return age >= AGE_WORK && r->trait == TR_DILIGENT;
    default: break;
    }
    if (who < 0) return false;
    const resident_t *o = &w->res[who];
    switch (role) {
    case W_PARTNER: return o->partner == i;
    case W_CHILD_OF: return (r->father == o->uid || r->mother == o->uid) && age >= AGE_CHILD;
    case W_FRIEND: return age >= AGE_CHILD && w->rel[who][i] >= 30 && w->feud[who][i] < 30;
    case W_FOE: return w->feud[who][i] >= 30;
    case W_OPPOSITE: return age >= AGE_ADULT && r->sex != o->sex && r->partner == NO_SLOT && age < 45;
    case W_LOVER: /* 有了好感、能成家的异性 */
        return age >= AGE_ADULT && r->sex != o->sex && r->partner == NO_SLOT && w->rel[who][i] >= 30 && r->father != o->uid &&
               r->mother != o->uid && o->father != r->uid && o->mother != r->uid &&
               !(r->father != NO_ID && (r->father == o->father || r->mother == o->mother));
    default: return false;
    }
}

/* 随机挑一个符合规则的人；W_NONE 返回 NO_SLOT，挑不到返回 -1 */
static int pick(world_t *w, int role, int who)
{
    if (role == W_NONE) return NO_SLOT;
    int n = 0, cand[MAX_RES];
    for (int i = 0; i < MAX_RES; i++)
        if (fits(w, role, i, who)) cand[n++] = i;
    return n ? cand[rn(w, n)] : -1;
}

static bool cond_ok(const world_t *w, const story_t *s)
{
    if (sim_day(w) < s->day || w->era < s->era) return false;
    int pop = sim_pop(w), a = s->carg;
    switch (s->cond) {
    case K_ANY: return true;
    case K_SEASON: return sim_season(w) == a;
    case K_KID:
        for (int i = 0; i < MAX_RES; i++)
            if (fits(w, W_KID, i, -1)) return true;
        return false;
    case K_ELDER:
        for (int i = 0; i < MAX_RES; i++)
            if (fits(w, W_ELDER, i, -1)) return true;
        return false;
    case K_COUPLE:
        for (int i = 0; i < MAX_RES; i++)
            if (here(&w->res[i]) && w->res[i].partner != NO_SLOT) return true;
        return false;
    case K_SICK:
        for (int i = 0; i < MAX_RES; i++)
            if (here(&w->res[i]) && w->res[i].sick) return true;
        return false;
    case K_SINGLE:
        for (int i = 0; i < MAX_RES; i++)
            if (fits(w, W_SINGLE, i, -1)) return true;
        return false;
    case K_FEUD:
        for (int i = 0; i < MAX_RES; i++)
            if (here(&w->res[i]) && sim_foe(w, i, 30) >= 0) return true;
        return false;
    case K_FAITH: return w->faith >= a;
    case K_LOWFAITH: return w->faith < a;
    case K_FEAR: return w->fear >= a;
    case K_DOCK: return sim_count_obj(w, O_DOCK) > 0;
    case K_PEN: return w->livestock > 0;
    case K_SHIP: return sim_count_obj(w, O_SHIP) > 0;
    case K_STATUE: return w->statue != NO_ID;
    case K_SHRINE: return w->shrine != NO_ID;
    case K_FLAG: return flag(w, a);
    case K_NOFLAG: return !flag(w, a);
    case K_HUNGRY: return w->food < pop * 3;
    case K_RICH: return w->food >= pop * 12;
    case K_WOLVES: return sim_kind_count(w, A_WOLF) > 0;
    case K_NOWOLF: return sim_kind_count(w, A_WOLF) == 0;
    case K_POP: return pop >= a;
    case K_HOUSE: return sim_houses > 0;
    case K_FARM: return sim_farms > 0;
    default: return false;
    }
}

/* 两个人都挑得到才算数 */
static bool cast_roles(world_t *w, const story_t *s)
{
    int a = pick(w, s->who, -1);
    if (a < 0) return false;
    int b = pick(w, s->who2, a == NO_SLOT ? -1 : a);
    if (b < 0) return false;
    w->evt_who = (uint8_t)a;
    w->evt_who2 = (uint8_t)b;
    return true;
}

/* ---------- 文字 ---------- */

static const char *nm(const world_t *w, int i) { return i < MAX_RES ? sim_name(&w->res[i]) : "岛民"; }

/* 把 @、# 换成名字 */
static void fill(const world_t *w, const char *src, char *buf, int n)
{
    int k = 0;
    for (const char *p = src; p && *p && k < n - 1; p++) {
        const char *ins = NULL;
        if (*p == '@') ins = nm(w, w->evt_who);
        else if (*p == '#') ins = nm(w, w->evt_who2);
        if (ins) {
            int len = (int)strlen(ins);
            if (k + len >= n - 1) break;
            memcpy(buf + k, ins, (size_t)len);
            k += len;
        } else {
            buf[k++] = *p;
        }
    }
    buf[k] = 0;
}

bool sim_story_pending(const world_t *w) { return current(w) != NULL; }
int sim_story_chapter(const world_t *w)
{
    const story_t *s = current(w);
    return s ? s->chap : 0;
}
void sim_story_title(const world_t *w, char *buf, int n)
{
    const story_t *s = current(w);
    if (!s) { buf[0] = 0; return; }
    if (s->chap) snprintf(buf, (size_t)n, "第%d章 %s", s->chap, s->title);
    else snprintf(buf, (size_t)n, "%s", s->title);
}
void sim_story_text(const world_t *w, char *buf, int n)
{
    const story_t *s = current(w);
    if (!s) { buf[0] = 0; return; }
    fill(w, s->text, buf, n);
}
int sim_story_nopts(const world_t *w)
{
    const story_t *s = current(w);
    int k = 0;
    while (s && k < 3 && s->opt[k].label) k++;
    return k;
}
void sim_story_opt(const world_t *w, int k, char *buf, int n)
{
    const story_t *s = current(w);
    if (!s || k < 0 || k >= 3 || !s->opt[k].label) { buf[0] = 0; return; }
    fill(w, s->opt[k].label, buf, n);
}

/* ---------- 效果 ---------- */

static void add16(uint16_t *v, int d, int max)
{
    int t = *v + d;
    *v = (uint16_t)(t < 0 ? 0 : t > max ? max : t);
}
static void add8(uint8_t *v, int d)
{
    int t = *v + d;
    *v = (uint8_t)(t < 0 ? 0 : t > 100 ? 100 : t);
}
static void hp(resident_t *r, int d)
{
    int t = r->hp + d;
    r->hp = (uint16_t)(t < 50 ? 50 : t > 1000 ? 1000 : t);
}
static int best_skill(const resident_t *r)
{
    int b = 0;
    for (int k = 1; k < SK_COUNT; k++)
        if (r->skill[k] > r->skill[b]) b = k;
    return b;
}

static void apply(world_t *w, const sop_t *op, int n)
{
    int a = w->evt_who, b = w->evt_who2;
    resident_t *ra = a < MAX_RES ? &w->res[a] : NULL, *rb = b < MAX_RES ? &w->res[b] : NULL;
    if (ra && !ra->alive) ra = NULL;
    if (rb && !rb->alive) rb = NULL;
    for (int k = 0; k < n; k++) {
        int v = op[k].arg;
        switch (op[k].op) {
        case X_FOOD: add16(&w->food, v, 999); break;
        case X_WOOD: add16(&w->wood, v, 999); break;
        case X_STONE: add16(&w->stone, v, 999); break;
        case X_FAITH: add8(&w->faith, v); break;
        case X_FEAR: add8(&w->fear, v); break;
        case X_MOOD:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i])) add16(&w->res[i].mood, v * 10, 1000);
            break;
        case X_MOOD1: if (ra) add16(&ra->mood, v * 10, 1000); break;
        case X_MOOD2: if (rb) add16(&rb->mood, v * 10, 1000); break;
        case X_HP1: if (ra) hp(ra, v * 10); break;
        case X_HP2: if (rb) hp(rb, v * 10); break;
        case X_SKILL1: if (ra) sim_skill_up(w, ra, best_skill(ra), v); break;
        case X_SKILL2: if (rb) sim_skill_up(w, rb, best_skill(rb), v); break;
        case X_SKILLK:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i]) && sim_age(&w->res[i]) < AGE_ADULT) sim_skill_up(w, &w->res[i], rn(w, SK_COUNT), v);
            break;
        case X_SICK1:
            if (ra && !ra->sick) {
                ra->sick = 3;
                ra->immune = 0;
            }
            break;
        case X_CURE:
            for (int i = 0; i < MAX_RES; i++)
                if (w->res[i].alive && w->res[i].sick) w->res[i].sick = 0, w->res[i].immune = 20;
            break;
        case X_GUEST: {
            int sex = v == 1 ? 0 : v == 2 ? 1 : rn(w, 2);
            int s = sim_spawn_resident(w, sex, rn(w, TR_COUNT), 18 + rn(w, 10), -1);
            if (s >= 0) sim_logf("%s在岛上住了下来", sim_name(&w->res[s]));
            break;
        }
        case X_BOAR: case X_RABBIT: case X_WOLF: {
            int kind = op[k].op == X_BOAR ? A_BOAR : op[k].op == X_RABBIT ? A_RABBIT : A_WOLF;
            for (int t = 0; t < v; t++) {
                int g = sim_rand_grass(w, 5, false);
                if (g >= 0) sim_spawn_animal(w, g % MAP_W, g / MAP_W, kind);
            }
            break;
        }
        case X_SLAY:
            for (int i = 0; i < MAX_ANI; i++)
                if (w->ani[i].alive && w->ani[i].kind == A_WOLF) w->ani[i].alive = 0;
            break;
        case X_FISH: add16(&w->fish, v, 60); break;
        case X_FUN:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i])) add16(&w->res[i].fun, v * 10, 1000);
            break;
        case X_DREAM1:
            if (ra) sim_dream_done(w, a);
            break;
        case X_FEUD:
            if (ra && rb) sim_feud_add(w, a, b, v);
            break;
        case X_BOND:
            if (ra && rb) {
                int t = w->rel[a][b] + v;
                t = t < 0 ? 0 : t > 100 ? 100 : t;
                w->rel[a][b] = w->rel[b][a] = (uint8_t)t;
            }
            break;
        case X_FLAG: w->story_flags |= 1u << v; break;
        case X_CROPS:
            for (int i = 0; i < MAP_N; i++)
                if (OBJ(w->tiles[i]) == O_FARM) {
                    int g = VAL(w->tiles[i]) + v;
                    w->tiles[i] = MKT(TERR(w->tiles[i]), O_FARM, g > 100 ? 100 : g < 0 ? 0 : g);
                }
            break;
        case X_FOREST:
            for (int t = 0; t < v; t++) {
                int g = sim_rand_grass(w, 4, true);
                if (g >= 0) w->tiles[g] = MKTILE(TERR(w->tiles[g]), O_SAPLING);
            }
            break;
        case X_FEAST: w->fest_h = 3; break;
        case X_RAIN: w->weather = W_RAIN, w->weather_h = 6, w->wild_wx = 0; break;
        case X_SUNNY: w->weather = W_SUNNY, w->weather_h = 12, w->wild_wx = 0; break;
        case X_SKY:
            w->sky = (uint8_t)v, w->sky_h = 3;
            sim_witness(w);
            break;
        case X_PIG:
            if (sim_count_obj(w, O_PEN)) w->livestock = (uint8_t)(w->livestock + v > 6 ? 6 : w->livestock + v < 0 ? 0 : w->livestock + v);
            else add16(&w->food, v * 5, 999);
            break;
        case X_DOG:
            if (ra) {
                sim_spawn_animal(w, ra->tx, ra->ty, A_DOG);
                for (int i = 0; i < MAX_ANI; i++)
                    if (w->ani[i].alive && w->ani[i].kind == A_DOG && w->ani[i].tx == ra->tx && w->ani[i].ty == ra->ty) w->ani[i].owner = (uint8_t)a;
            }
            break;
        case X_TRADE: w->trade |= (uint8_t)v; break;
        case X_HURTALL:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i]) && !sim_in_house(w, &w->res[i])) hp(&w->res[i], -v * 10);
            break;
        case X_ENERGY:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i])) w->res[i].energy = 1000;
            break;
        case X_SOCIAL:
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i])) w->res[i].social = 1000;
            break;
        case X_BLESS1:
            if (ra) sim_bless_one(w, a);
            break;
        case X_ADOPT:
            if (ra) {
                int s = sim_spawn_resident(w, rn(w, 2), rn(w, TR_COUNT), 0, IDX(ra->tx, ra->ty));
                if (s >= 0) {
                    resident_t *c = &w->res[s];
                    if (ra->sex) c->mother = ra->uid, c->mname = ra->name;
                    else c->father = ra->uid, c->fname = ra->name;
                    c->home = ra->home;
                    c->gen = (uint8_t)(ra->gen + 1);
                    ra->kids++;
                    sim_tree_put(w, c);
                    sim_logf("%s收养了一个孩子，取名%s", sim_name(ra), sim_name(c));
                }
            }
            break;
        case X_MARRY:
            if (ra && rb) sim_marry(w, a, b);
            break;
        case X_NOLOVE: /* 吓破了胆：好感跌到底，三十天里不会再走到一起 */
            if (ra && rb) {
                w->rel[a][b] = w->rel[b][a] = 0;
                sim_feud_add(w, a, b, 60);
            }
            break;
        case X_ENDING:
            w->ending = (uint8_t)v;
            w->story_flags |= 1u << F_ENDED;
            sim_ach(w, ACH_ENDING);
            break;
        default: break;
        }
    }
}

void sim_story_choose(world_t *w, int k, char *result, int n)
{
    const story_t *s = current(w);
    if (result && n) result[0] = 0;
    if (!s) return;
    int nopt = sim_story_nopts(w);
    if (k < 0 || k >= nopt) k = nopt - 1;
    const sopt_t *o = &s->opt[k];
    char t[48];
    sim_story_title(w, t, sizeof t);
    w->evt = 0; /* 先了结，效果里的赐福不会再触发一次 */
    if (s->chap) {
        w->chapter = (uint8_t)(s->chap + 1);
        w->last_ch = (uint16_t)sim_day(w);
    }
    bool failed = o->risk && rn(w, 100) < o->risk;
    if (failed) apply(w, o->fops, 3);
    else apply(w, o->ops, 4);
    if (result) fill(w, failed ? o->fail : o->result, result, n);
    fill(w, failed ? o->fail : o->result, g_story_result, sizeof g_story_result);
    snprintf(g_story_rtitle, sizeof g_story_rtitle, "%s", t);
    g_story_new = true;
    sim_logf("【%s】有了结局", t);
    sim_sfx(SFX_CHIME);
}

/* ---------- 触发 ---------- */

static bool start(world_t *w, int kind, int id)
{
    const story_t *s = kind == 2 ? &STORY_MAIN[id] : &STORY_TALE[id];
    if (!cast_roles(w, s)) return false;
    w->evt = (uint8_t)kind;
    w->evt_id = (uint8_t)id;
    w->evt_h = WAIT_H;
    w->evt_seen = 0;
    if (kind == 1) {
        w->seen[id / 8] |= (uint8_t)(1 << (id % 8));
        w->last_evt = (uint16_t)sim_day(w);
        if (w->tales < 255) w->tales++;
        if (w->tales >= 30) sim_ach(w, ACH_TALES);
    }
    char t[48];
    sim_story_title(w, t, sizeof t);
    sim_logf("★故事：%s", t);
    sim_sfx(SFX_CHIME);
    return true;
}

static bool try_chapter(world_t *w)
{
    if (flag(w, F_ENDED)) return false;
    int day = sim_day(w);
    if (w->chapter > 1 && day - w->last_ch < 5) return false;
    int want = w->chapter ? w->chapter : 1;
    for (int i = 0; i < STORY_MAIN_N; i++) {
        const story_t *s = &STORY_MAIN[i];
        if (s->chap != want || !cond_ok(w, s)) continue;
        if (start(w, 2, i)) return true; /* 挑不到合适的人：试下一个版本 */
    }
    return false;
}

static bool try_tale(world_t *w)
{
    int day = sim_day(w);
    if (day < 3 || day - w->last_evt < 3 || rn(w, 100) >= 40) return false;
    int total = 0, unseen = 0;
    for (int i = 0; i < STORY_TALE_N; i++)
        if (!((w->seen[i / 8] >> (i % 8)) & 1)) unseen++;
    if (!unseen) memset(w->seen, 0, sizeof w->seen); /* 都经历过了：从头再来 */
    for (int i = 0; i < STORY_TALE_N; i++)
        if (!((w->seen[i / 8] >> (i % 8)) & 1) && cond_ok(w, &STORY_TALE[i])) total += STORY_TALE[i].weight;
    if (!total) return false;
    int roll = rn(w, total);
    for (int i = 0; i < STORY_TALE_N; i++) {
        if (((w->seen[i / 8] >> (i % 8)) & 1) || !cond_ok(w, &STORY_TALE[i])) continue;
        roll -= STORY_TALE[i].weight;
        if (roll < 0) return start(w, 1, i);
    }
    return false;
}

/* 神迹落下：正在发生的故事跟着改变走向。favor = 降福，否则是降威 */
void sim_story_react(world_t *w, bool favor)
{
    if (!w->evt) return;
    int n = sim_story_nopts(w);
    sim_story_choose(w, n >= 2 ? (favor ? 0 : 1) : 0, NULL, 0);
}

/* 每小时：等回应的故事倒计时；没有故事时，早上 9 点看看有没有新故事 */
void sim_story_hour(world_t *w)
{
    if (w->evt) {
        const story_t *s = current(w);
        int a = w->evt_who, b = w->evt_who2;
        bool gone = (a < MAX_RES && !w->res[a].alive) || (b < MAX_RES && !w->res[b].alive);
        if (!s || gone) { /* 主角不在了，故事作罢 */
            w->evt = 0;
            return;
        }
        if (w->evt_h && --w->evt_h) return;
        sim_story_choose(w, sim_story_nopts(w) - 1, NULL, 0); /* 神没有出手：顺其自然 */
        return;
    }
    if (sim_hour(w) != 9 || sim_pop(w) == 0) return;
    if (try_chapter(w)) return;
    try_tale(w);
}
