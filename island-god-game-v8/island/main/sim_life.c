/*
 * 《造物主：孤岛》模拟核心（三）：人情与岛外的世界。
 * 恩怨、婚礼葬礼新年成人礼、梦想、商船、远航、牧场、天象、神像、成就。
 */
#include <stdio.h>
#include <string.h>

#include "sim_int.h"

#define DX SIM_DX
#define DY SIM_DY
#define rn sim_rn
#define cheb sim_cheb
#define inmap sim_inmap

static bool here(const resident_t *r) { return r->alive && !r->away; }
static bool adult(const resident_t *r) { return here(r) && sim_age(r) >= AGE_ADULT; }

/* ---------- 梦想 ---------- */

static const char *const DREAMS[DR_COUNT] = {"",           "成为一代大师", "住上最好的房子", "儿孙满堂", "钓到传说中的大鱼",
                                             "出海看看远方", "活到七十岁",   "亲眼见到神迹",   "交三个知心朋友"};
const char *sim_dream_name(int d) { return DREAMS[d % DR_COUNT]; }

void sim_dream_done(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!r->alive || !r->dream || (r->flags & RF_DREAM)) return;
    r->flags |= RF_DREAM;
    w->dreams_met++;
    sim_note(w, r, EV_DREAM, r->dream);
    r->mood = 1000;
    sim_logf("%s实现了梦想：%s", sim_name(r), DREAMS[r->dream % DR_COUNT]);
    sim_fx_add(FX_SPARK, r->tx, r->ty, 14);
    sim_sfx(SFX_CHIME);
}

static int friends(const world_t *w, int i)
{
    int n = 0;
    for (int j = 0; j < MAX_RES; j++) n += j != i && w->res[j].alive && w->rel[i][j] >= 60;
    return n;
}

static void dreams_day(world_t *w)
{
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!here(r) || (r->flags & RF_DREAM)) continue;
        bool met = false;
        switch (r->dream) {
        case DR_MASTER:
            for (int k = 0; k < SK_COUNT; k++) met |= r->skill[k] >= 65;
            break;
        case DR_BIGHOUSE: met = r->home != NO_ID && OBJ(w->tiles[r->home]) == O_HOUSE2; break;
        case DR_KIDS: met = r->kids >= 3; break;
        case DR_OLD: met = sim_age(r) >= 70; break;
        case DR_FRIENDS: met = friends(w, i) >= 3; break;
        default: break; /* 大鱼、远航、神迹：由事件本身来实现 */
        }
        if (met) sim_dream_done(w, i);
    }
}

/* 神迹当前：醒着、在岛上的人都看见了 */
void sim_witness(world_t *w)
{
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!here(r) || r->state == S_SLEEP) continue;
        if (r->dream == DR_MIRACLE) sim_dream_done(w, i);
    }
}

/* 长大成人：16 岁，许下一生的梦想 */
static void coming_of_age(world_t *w)
{
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!here(r) || sim_age(r) < AGE_ADULT || (r->flags & RF_ADULT)) continue;
        r->flags |= RF_ADULT;
        if (!r->dream) r->dream = (uint8_t)(1 + rn(w, DR_COUNT - 1));
        sim_note(w, r, EV_ADULT, 0);
        sim_logf("%s长大成人了，梦想是%s", sim_name(r), DREAMS[r->dream % DR_COUNT]);
        sim_fx_add(FX_SPARK, r->tx, r->ty, 12);
        for (int j = 0; j < MAX_RES; j++) /* 父母脸上有光 */
            if (w->res[j].alive && (r->father == w->res[j].uid || r->mother == w->res[j].uid)) w->res[j].mood = 1000;
        if (!w->cer) {
            w->cer = CER_ADULT, w->cer_h = 2, w->cer_a = (uint8_t)i, w->cer_b = (uint8_t)i;
            w->cer_idx = (uint16_t)IDX(w->camp_x, w->camp_y);
        }
    }
}

/* ---------- 恩怨 ---------- */

void sim_feud_add(world_t *w, int a, int b, int v)
{
    int f = w->feud[a][b] + v;
    if (f > 100) f = 100;
    if (f < 0) f = 0;
    bool first = w->feud[a][b] < 50 && f >= 50;
    w->feud[a][b] = w->feud[b][a] = (uint8_t)f;
    if (first) {
        sim_note(w, &w->res[a], EV_FEUD, sim_who(&w->res[b]));
        sim_note(w, &w->res[b], EV_FEUD, sim_who(&w->res[a]));
    }
}

/* 一个人的最大仇人（怨气 ≥ min），没有返回 -1 */
int sim_foe(const world_t *w, int i, int min)
{
    int best = -1, bv = min - 1;
    for (int j = 0; j < MAX_RES; j++)
        if (j != i && w->res[j].alive && w->feud[i][j] > bv) bv = w->feud[i][j], best = j;
    return best;
}

static void feud_day(world_t *w)
{
    for (int a = 0; a < MAX_RES; a++)
        for (int b = a + 1; b < MAX_RES; b++) {
            if (!w->feud[a][b]) continue;
            int d = 2 + (w->res[a].trait == TR_KIND) * 2 + (w->res[b].trait == TR_KIND) * 2;
            if (!w->res[a].alive || !w->res[b].alive) d = 100;
            sim_feud_add(w, a, b, -d);
        }
}

/* 结为伴侣：婚礼；暗暗喜欢其中一人的单身者会吃醋 */
void sim_on_marry(world_t *w, int a, int b)
{
    for (int k = 0; k < MAX_RES; k++) {
        const resident_t *o = &w->res[k];
        if (k == a || k == b || !adult(o) || o->partner != NO_SLOT) continue;
        for (int t = 0; t < 2; t++) {
            int love = t ? b : a, rival = t ? a : b;
            if (w->res[love].sex == o->sex || w->rel[k][love] < 50 || w->res[rival].sex != o->sex) continue;
            sim_feud_add(w, k, rival, 45);
            sim_logf("%s有些嫉妒%s", sim_name(o), sim_name(&w->res[rival]));
        }
    }
    if (!w->cer || w->cer == CER_ADULT) {
        w->cer = CER_WEDDING, w->cer_h = 3, w->cer_a = (uint8_t)a, w->cer_b = (uint8_t)b;
        w->cer_idx = (uint16_t)IDX(w->camp_x, w->camp_y);
    }
}

/* ---------- 墓园与葬礼 ---------- */

static bool grave_ok(const world_t *w, int idx)
{
    int t = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    return (t == T_GRASS || t == T_ASH) && (o == O_NONE || o == O_FLOWER) && !sim_occupied(w, idx) && !sim_danger[idx];
}

/* 墓园：已有坟墓旁边；没有就在离营地 6～9 格的一片空地 */
static int grave_spot(const world_t *w)
{
    int best = -1, bd = 99;
    for (int i = 0; i < MAP_N; i++) {
        if (OBJ(w->tiles[i]) != O_GRAVE) continue;
        int cx = i % MAP_W, cy = i / MAP_W;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                if (!inmap(cx + dx, cy + dy) || !grave_ok(w, IDX(cx + dx, cy + dy))) continue;
                int d = cheb(cx + dx, cy + dy, cx, cy) * 10 + cheb(cx + dx, cy + dy, w->camp_x, w->camp_y);
                if (d < bd) bd = d, best = IDX(cx + dx, cy + dy);
            }
    }
    if (best >= 0) return best;
    for (int r = 6; r <= 9 && best < 0; r++)
        for (int i = 0; i < MAP_N && best < 0; i++)
            if (cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) == r && grave_ok(w, i)) best = i;
    return best;
}

int sim_on_death(world_t *w, int i, int at)
{
    int g = grave_spot(w);
    if (g < 0) g = at;
    if (g >= 0 && sim_pop(w) >= 1 && (!w->cer || w->cer == CER_ADULT || w->cer == CER_NEWYEAR)) {
        w->cer = CER_FUNERAL, w->cer_h = 2, w->cer_a = (uint8_t)i, w->cer_b = (uint8_t)i;
        w->cer_idx = (uint16_t)g;
    }
    return g;
}

static void ceremony_hour(world_t *w)
{
    if (!w->cer) return;
    int h = sim_hour(w);
    if (h >= 21 || h < 6) return; /* 夜里不办，第二天接着 */
    if (w->cer_h == 3 || (w->cer_h == 2 && (w->cer == CER_FUNERAL || w->cer == CER_ADULT))) {
        const resident_t *a = &w->res[w->cer_a % MAX_RES], *b = &w->res[w->cer_b % MAX_RES];
        switch (w->cer) {
        case CER_WEDDING: sim_logf("%s和%s举行了婚礼，全岛都来祝福", sim_name(a), sim_name(b)); break;
        case CER_FUNERAL: sim_logf("岛民聚在墓园，为%s送行", sim_name(a)); break;
        case CER_NEWYEAR: sim_logf("新年到了！大家围着篝火守岁"); break;
        default: break;
        }
    }
    if (w->cer == CER_WEDDING || w->cer == CER_NEWYEAR) sim_fx_add(FX_NOTE, w->camp_x, w->camp_y, 14);
    if (--w->cer_h == 0) {
        for (int i = 0; i < MAX_RES; i++) {
            resident_t *r = &w->res[i];
            if (!here(r)) continue;
            r->social = 1000;
            if (w->cer != CER_FUNERAL) r->mood = (uint16_t)(r->mood + 150 > 1000 ? 1000 : r->mood + 150);
        }
        if (w->cer == CER_WEDDING || w->cer == CER_NEWYEAR) /* 喜事冲淡了怨气 */
            for (int a = 0; a < MAX_RES; a++)
                for (int b = 0; b < MAX_RES; b++) w->feud[a][b] = (uint8_t)(w->feud[a][b] > 20 ? w->feud[a][b] - 20 : 0);
        w->cer = CER_NONE;
    }
}

/* ---------- 商船 ---------- */

static const char *const GOODS[6] = {"良种", "好工具", "牲畜", "草药", "书简", "一只小狗"};
const char *sim_trade_name(int bit)
{
    for (int k = 0; k < 6; k++)
        if (bit == 1 << k) return GOODS[k];
    return "稀罕玩意";
}

/* 船停在离营地最近的海面上，旁边要有沙滩可以走过去 */
static int anchorage(const world_t *w)
{
    int best = -1, bd = 999;
    for (int i = 0; i < MAP_N; i++) {
        int t = TERR(w->tiles[i]);
        if (t != T_SEA && t != T_SHOAL) continue;
        int x = i % MAP_W, y = i / MAP_W;
        bool beach = false;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (inmap(nx, ny) && TERR(w->tiles[IDX(nx, ny)]) == T_SAND && sim_walkable(w, IDX(nx, ny))) beach = true;
        }
        if (!beach || x < 1 || x >= MAP_W - 1 || y < 2) continue;
        int dock = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (inmap(nx, ny) && OBJ(w->tiles[IDX(nx, ny)]) == O_DOCK) dock = 1;
        }
        int dist = cheb(x, y, w->camp_x, w->camp_y) - dock * 6;
        if (dist < bd) bd = dist, best = i;
    }
    return best;
}

static void ship_hour(world_t *w)
{
    int day = sim_day(w), h = sim_hour(w);
    if (w->ship_h) {
        if (--w->ship_h == 0) sim_logf("商船扬帆远去了");
        return;
    }
    if (h != 8 || day < 12 || day - w->last_ship < 10 || sim_pop(w) < 2 || rn(w, 100) >= 30) return;
    if (w->weather >= W_STORM && w->weather != W_WIND) return;
    int a = anchorage(w);
    if (a < 0) return;
    w->ship_x = (uint8_t)(a % MAP_W), w->ship_y = (uint8_t)(a / MAP_W);
    w->ship_h = 10;
    w->ship_deals = 0;
    w->last_ship = (uint16_t)day;
    sim_logf("一艘商船靠岸了，船上有远方的货物");
    sim_sfx(SFX_CHIME);
}

/* 居民带着货走到船边：拿多余的东西换一样新东西 */
bool sim_do_trade(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int pop = sim_pop(w);
    const char *pay;
    if (w->food >= pop * 10 + 12) w->food = (uint16_t)(w->food - 12), pay = "粮食";
    else if (w->wood >= 24) w->wood = (uint16_t)(w->wood - 12), pay = "木材";
    else if (w->stone >= 24) w->stone = (uint16_t)(w->stone - 12), pay = "石料";
    else return false;
    int want[6], n = 0;
    for (int k = 0; k < 6; k++)
        if (!(w->trade & (1 << k)) && (k != 5 || sim_kind_count(w, A_DOG) == 0)) want[n++] = 1 << k;
    int got = n ? want[rn(w, n)] : 0;
    w->trade |= (uint8_t)got;
    w->trades++;
    w->ship_deals++;
    sim_logf("%s用%s换来了%s", sim_name(r), pay, sim_trade_name(got));
    sim_skill_up(w, r, SK_CRAFT, 2);
    if (got == TD_DOG) {
        sim_spawn_animal(w, r->tx, r->ty, A_DOG);
        for (int k = 0; k < MAX_ANI; k++)
            if (w->ani[k].alive && w->ani[k].kind == A_DOG && w->ani[k].tx == r->tx && w->ani[k].ty == r->ty) w->ani[k].owner = (uint8_t)i;
    } else if (got == TD_BOOKS) {
        for (int j = 0; j < MAX_RES; j++)
            if (here(&w->res[j]) && sim_age(&w->res[j]) < AGE_ADULT) sim_skill_up(w, &w->res[j], rn(w, SK_COUNT), 10);
    } else if (!got) { /* 新鲜东西都换过了：换回来的稀罕玩意让大家高兴 */
        for (int j = 0; j < MAX_RES; j++)
            if (here(&w->res[j])) w->res[j].fun = 1000;
    }
    sim_sfx(SFX_BLIP);
    return true;
}

/* ---------- 远航 ---------- */

static int ship_tile(const world_t *w)
{
    for (int i = 0; i < MAP_N; i++)
        if (OBJ(w->tiles[i]) == O_SHIP) return i;
    return -1;
}

static void voyage_day(world_t *w)
{
    int s = ship_tile(w);
    if (w->voyage_d) {
        if (--w->voyage_d) return;
        /* 回来了 */
        int names[2], n = 0;
        for (int i = 0; i < MAX_RES; i++) {
            resident_t *r = &w->res[i];
            if (!r->alive || !r->away) continue;
            r->away = 0;
            int x = w->camp_x, y = w->camp_y + 1;
            sim_camp_spot(w, i, &x, &y);
            r->tx = r->nx = (uint8_t)x, r->ty = r->ny = (uint8_t)y;
            r->state = S_IDLE, r->goal = G_NONE, r->step = 0, r->timer = 0;
            r->food = 700, r->mood = 1000;
            sim_note(w, r, EV_VOYAGE, 0);
            sim_skill_up(w, r, SK_FISH, 10);
            if (r->dream == DR_VOYAGE) sim_dream_done(w, i);
            if (n < 2) names[n++] = i;
        }
        if (s >= 0) w->tiles[s] = MKT(TERR(w->tiles[s]), O_SHIP, 0);
        w->voyages++;
        int k = rn(w, 100);
        char what[48];
        if (k < 35 && sim_pop(w) < MAX_RES) {
            int sex = rn(w, 2), t = sim_spawn_resident(w, sex, rn(w, TR_COUNT), 18 + rn(w, 8), -1);
            snprintf(what, sizeof what, "%s", t >= 0 ? "一位远方的朋友" : "满船的故事");
            if (t >= 0) sim_logf("远方来的%s在岛上住了下来", sim_name(&w->res[t]));
        } else if (k < 65) {
            w->food = (uint16_t)(w->food + 30 > 999 ? 999 : w->food + 30);
            w->wood = (uint16_t)(w->wood + 15 > 999 ? 999 : w->wood + 15);
            snprintf(what, sizeof what, "满舱的鱼干和木材");
        } else {
            for (int i = 0; i < MAX_RES; i++)
                if (here(&w->res[i])) w->res[i].fun = 1000, w->res[i].mood = 1000;
            w->stone = (uint16_t)(w->stone + 20 > 999 ? 999 : w->stone + 20);
            snprintf(what, sizeof what, "奇异的贝壳和远方的见闻");
        }
        if (n == 2) sim_logf("远航归来！%s和%s带回了%s", sim_name(&w->res[names[0]]), sim_name(&w->res[names[1]]), what);
        else if (n == 1) sim_logf("远航归来！%s带回了%s", sim_name(&w->res[names[0]]), what);
        sim_sfx(SFX_BLESS);
        sim_fx_add(FX_GLOW, 0, 0, 10);
        return;
    }
    /* 出发：有大船，天气好，隔上一阵子 */
    if (s < 0 || w->era < ERA_SAIL || sim_pop(w) < 4 || w->food < 20 || rn(w, 100) >= 30) return;
    if (w->weather >= W_STORM && w->weather != W_WIND) return;
    int pick[2], n = 0;
    for (int pass = 0; pass < 2 && n < 2; pass++)
        for (int i = 0; i < MAX_RES && n < 2; i++) {
            resident_t *r = &w->res[i];
            int age = sim_age(r);
            if (!here(r) || age < AGE_ADULT || age >= AGE_ELDER || r->preg || r->sick || r->hp < 600) continue;
            if (n == 1 && pick[0] == i) continue;
            if (pass == 0 && r->dream != DR_VOYAGE && r->trait != TR_BRAVE && sim_prof(r) != SK_FISH) continue;
            pick[n++] = i;
        }
    if (n == 0) return;
    w->food = (uint16_t)(w->food - 10);
    for (int k = 0; k < n; k++) {
        resident_t *r = &w->res[pick[k]];
        r->away = 1; /* 标记：船上的人不在岛上 */
        r->tx = r->nx = (uint8_t)(s % MAP_W), r->ty = r->ny = (uint8_t)(s / MAP_W);
        r->state = S_IDLE, r->goal = G_NONE, r->step = 0, r->carry = C_NONE, r->carry_n = 0;
        for (int a = 0; a < MAX_ANI; a++)
            if (w->ani[a].held == pick[k] + 1) w->ani[a].held = 0;
    }
    w->voyage_d = (uint8_t)(4 + rn(w, 5));
    w->tiles[s] = MKT(TERR(w->tiles[s]), O_SHIP, 1); /* 附加值 1：船出海了 */
    if (n == 2) sim_logf("%s和%s驾着大船出海远航了", sim_name(&w->res[pick[0]]), sim_name(&w->res[pick[1]]));
    else sim_logf("%s驾着大船出海远航了", sim_name(&w->res[pick[0]]));
    sim_sfx(SFX_CHIME);
}

/* ---------- 牧场与狗 ---------- */

static void pen_day(world_t *w)
{
    if (!sim_count_obj(w, O_PEN)) {
        w->livestock = 0;
        return;
    }
    if (!w->livestock) {
        w->livestock = 2;
        sim_logf("牧场里养上了两头猪");
    }
    int season = sim_season(w), pop = sim_pop(w);
    w->food = (uint16_t)(w->food + (w->livestock + 1) / 2 > 999 ? 999 : w->food + (w->livestock + 1) / 2); /* 猪仔、猪油 */
    if (season <= SEASON_SUMMER && w->livestock < 6 && rn(w, 100) < 50) w->livestock++;
    if (w->food < pop * 3 && w->livestock > 2) {
        w->livestock--;
        w->food = (uint16_t)(w->food + 10);
        sim_logf("粮食吃紧，宰了一头猪");
    }
    /* 驯服狼崽：有牧场、有猎人，岛上有狼 */
    if (sim_kind_count(w, A_DOG) < 2 && sim_kind_count(w, A_WOLF) && rn(w, 100) < 6)
        for (int i = 0; i < MAX_RES; i++) {
            resident_t *r = &w->res[i];
            if (!adult(r) || sim_prof(r) != SK_HUNT) continue;
            sim_spawn_animal(w, r->tx, r->ty, A_DOG);
            for (int k = 0; k < MAX_ANI; k++)
                if (w->ani[k].alive && w->ani[k].kind == A_DOG && w->ani[k].tx == r->tx && w->ani[k].ty == r->ty) w->ani[k].owner = (uint8_t)i;
            sim_logf("%s捡回一只狼崽，养成了看家狗", sim_name(r));
            break;
        }
}

/* ---------- 天象 ---------- */

static void sky_hour(world_t *w)
{
    if (w->sky_h && --w->sky_h == 0) w->sky = SKY_NONE;
}

/* ---------- 成就 ---------- */

static const char *const ACH[ACH_N] = {
    "第一间小屋", "青铜时代", "铁器时代", "航海时代", "四世同堂", "十代传承",
    "第一位大师", "大师云集", "人丁兴旺", "第一场婚礼", "第一笔买卖", "远航归来",
    "神像落成", "十年太平", "信仰之岛", "梦想成真", "救火英雄", "传说大鱼",
    "看家狗",   "六畜兴旺", "百年孤岛", "浴火重生", "和好如初", "天降奇观", "潮声之书", "见多识广", "工业时代", "现代都市"};
static const char *const ACH_HOW[ACH_N] = {
    "盖起第一间房子", "进入青铜时代", "进入铁器时代", "进入航海时代", "传到第四代", "传到第十代",
    "有人成为大师", "同时有四位大师", "岛上住满十人", "举行一场婚礼", "和商船做成交易", "大船远航回来",
    "岛民为神立起神像", "四十天无人去世", "信仰达到九十", "五个人实现梦想", "岛民扑灭三十处火", "钓到传说中的大鱼",
    "养一只看家狗", "牧场里有六头猪", "岛屿存在一百年", "灭绝后重新繁衍到住满", "仇人和好", "降下流星雨、彩虹或日食",
    "读完主线故事", "经历三十段奇遇", "进入工业时代", "进入现代"};
const char *sim_ach_name(int k) { return ACH[k % ACH_N]; }
const char *sim_ach_how(int k) { return ACH_HOW[k % ACH_N]; }
int sim_ach_count(const world_t *w)
{
    int n = 0;
    for (int k = 0; k < ACH_N; k++) n += (w->ach >> k) & 1;
    return n;
}

void sim_ach(world_t *w, int k)
{
    if (w->ach & (1u << k)) return;
    w->ach |= 1u << k;
    sim_logf("★达成成就：%s", ACH[k]);
    sim_sfx(SFX_BLESS);
}

static void ach_day(world_t *w)
{
    int masters = 0, pop = sim_pop(w), day = sim_day(w), last_death = 0;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive)
            for (int k = 0; k < SK_COUNT; k++)
                if (w->res[i].skill[k] >= 65) { masters++; break; }
    for (int k = 0; k < MAX_DEAD; k++)
        if (w->dead[k].used && w->dead[k].day > last_death) last_death = w->dead[k].day;
    if (sim_houses) sim_ach(w, 0);
    if (w->era >= ERA_BRONZE) sim_ach(w, 1);
    if (w->era >= ERA_IRON) sim_ach(w, 2);
    if (w->era >= ERA_SAIL) sim_ach(w, 3);
    if (w->era >= ERA_INDUSTRY) sim_ach(w, 26);
    if (w->era >= ERA_MODERN) sim_ach(w, 27);
    if (w->max_gen >= 4) sim_ach(w, 4);
    if (w->max_gen >= 10) sim_ach(w, 5);
    if (masters >= 1) sim_ach(w, 6);
    if (masters >= 4) sim_ach(w, 7);
    if (pop >= MAX_RES) sim_ach(w, 8);
    if (w->marriages) sim_ach(w, 9);
    if (w->trades) sim_ach(w, 10);
    if (w->voyages) sim_ach(w, 11);
    if (w->statue != NO_ID && w->statue < MAP_N && OBJ(w->tiles[w->statue]) == O_STATUE) sim_ach(w, 12);
    if (day - last_death >= 40 && day >= 40) sim_ach(w, 13);
    if (w->faith >= 90) sim_ach(w, 14);
    if (w->dreams_met >= 5) sim_ach(w, 15);
    if (w->fires_put >= 30) sim_ach(w, 16);
    if (sim_kind_count(w, A_DOG)) sim_ach(w, 18);
    if (w->livestock >= 6) sim_ach(w, 19);
    if (sim_year(w) >= 100) sim_ach(w, 20);
    if (w->power_used[P_GENESIS] && w->deaths && pop >= MAX_RES && w->reborn) sim_ach(w, 21);
}

/* ---------- 每小时 / 每天 ---------- */

void sim_life_hour(world_t *w)
{
    ceremony_hour(w);
    ship_hour(w);
    sky_hour(w);
    /* 新年：每年开春那天傍晚 */
    if (sim_season(w) == SEASON_SPRING && sim_hour(w) == 18 && sim_day(w) > 1 && sim_pop(w) >= 2 && !w->cer) {
        w->cer = CER_NEWYEAR, w->cer_h = 3, w->cer_a = w->cer_b = 0;
        w->cer_idx = (uint16_t)IDX(w->camp_x, w->camp_y);
    }
}

void sim_life_day(world_t *w)
{
    coming_of_age(w);
    dreams_day(w);
    feud_day(w);
    voyage_day(w);
    pen_day(w);
    ach_day(w);
}
