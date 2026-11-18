#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

uint16_t fb_top[SCR_W * TOP_H];
uint16_t fb_map[SCR_W * MAPV_H];
uint16_t fb_bot[SCR_W * BOT_H];

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_SEA RGB(44, 112, 178)
#define C_SEA_HI RGB(78, 146, 204)
#define C_SHOAL RGB(92, 176, 214)
#define C_FOAM RGB(214, 240, 248)
#define C_SAND RGB(238, 216, 162)
#define C_SAND_D RGB(214, 188, 130)
#define C_GRASS RGB(116, 190, 96)
#define C_GRASS_D RGB(94, 168, 80)
#define C_GRASS_L RGB(146, 212, 116)
#define C_RIVER RGB(84, 164, 224)
#define C_RIVER_HI RGB(150, 208, 244)
#define C_LAKE RGB(58, 132, 204)
#define C_ASH RGB(98, 90, 84)
#define C_ASH_D RGB(70, 64, 60)
#define C_FLOOD RGB(96, 150, 196)
#define C_SNOW RGB(232, 238, 246)
#define C_SNOW_D RGB(196, 210, 230)
#define C_SOIL RGB(150, 108, 70)
#define C_SOIL_D RGB(118, 82, 52)
#define C_LAVA RGB(232, 84, 30)
#define C_LAVA_HI RGB(255, 208, 70)
#define C_LAVA_D RGB(120, 40, 28)

#define C_BG RGB(22, 26, 42)
#define C_PANEL RGB(40, 46, 70)
#define C_LINE RGB(64, 72, 104)
#define C_TEXT RGB(236, 238, 244)
#define C_DIM RGB(150, 158, 184)
#define C_GOLD RGB(255, 214, 92)
#define C_RED RGB(240, 96, 88)

/* ---------- 字体 ---------- */

static int glyph_index(uint32_t cp)
{
    int lo = 0, hi = font_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_cp[mid] == cp) return mid;
        if (font_cp[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) {
        c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
        p += 3;
    } else if (c >= 0xE0 && p[0] && p[1]) {
        c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63);
        p += 2;
    } else if (c >= 0xC0 && p[0]) {
        c = (c & 31) << 6 | (p[0] & 63);
        p += 1;
    }
    *s = (const char *)p;
    return c;
}

int font_width(const char *s)
{
    int w = 0;
    while (*s) w += utf8_next(&s) < 0x80 ? 6 : 12;
    return w;
}

int font_draw_scaled(uint16_t *fb, int fbw, int fbh, int x, int y, const char *s, uint16_t color, int scale)
{
    while (*s) {
        uint32_t cp = utf8_next(&s);
        int adv = (cp < 0x80 ? 6 : 12) * scale;
        int gi = glyph_index(cp);
        if (gi >= 0) {
            for (int r = 0; r < FONT_H * scale; r++) {
                int yy = y + r;
                if (yy < 0 || yy >= fbh) continue;
                uint16_t bits = font_rows[gi][r / scale];
                for (int c = 0; c < 12 * scale; c++) {
                    int xx = x + c;
                    if ((bits & (0x800 >> (c / scale))) && xx >= 0 && xx < fbw) fb[yy * fbw + xx] = color;
                }
            }
        }
        x += adv;
    }
    return x;
}

int font_draw(uint16_t *fb, int fbw, int fbh, int x, int y, const char *s, uint16_t color)
{
    return font_draw_scaled(fb, fbw, fbh, x, y, s, color, 1);
}

/* 自动换行：maxw 像素宽，返回画了几行；skip 行之前的不画。行首不放标点 */
static int font_wrap(uint16_t *fb, int fbw, int fbh, int x, int y, int maxw, int lh, const char *s, uint16_t color, int max_lines)
{
    int lines = 0, cx = x;
    while (*s && *s != '|') {
        const char *p = s;
        uint32_t cp = utf8_next(&p);
        int adv = cp < 0x80 ? 6 : 12;
        bool punct = cp == 0xFF0C || cp == 0x3002 || cp == 0xFF01 || cp == 0xFF1F || cp == 0x3001 || cp == 0xFF1A ||
                     cp == 0x201D || cp == 0xFF1B || cp == 0x2026 || cp == '"';
        if (cp == '\n' || (cx + adv > x + maxw && !punct)) {
            lines++;
            cx = x;
            if (lines >= max_lines) return lines;
            if (cp == '\n') { s = p; continue; }
        }
        char one[5] = {0};
        memcpy(one, s, (size_t)(p - s));
        font_draw(fb, fbw, fbh, cx, y + lines * lh, one, color);
        cx += adv;
        s = p;
    }
    return lines + 1;
}

/* ---------- 像素画 ---------- */

static uint16_t pal[128];
static int s_season;      /* 当前季节，render_map 开头设置 */
static bool s_snowy;      /* 地面被雪盖住（冬天或暴风雪） */
static uint8_t s_noise[4][64]; /* 草地、沙地的细碎纹理（4 种花样轮着用） */

/* 各季节的草地（底色、暗、亮）与树冠（G g D） */
static const uint8_t GRASS_RGB[4][3][3] = {
    {{124, 198, 98}, {100, 174, 84}, {160, 222, 122}},
    {{98, 176, 82}, {78, 152, 68}, {130, 200, 102}},
    {{178, 170, 86}, {150, 138, 66}, {206, 194, 112}},
    {{232, 238, 246}, {200, 212, 230}, {255, 255, 255}}};
static const uint8_t LEAF_RGB[4][3][3] = {
    {{70, 162, 82}, {122, 204, 104}, {42, 116, 66}},
    {{50, 140, 68}, {94, 182, 88}, {30, 98, 56}},
    {{212, 122, 44}, {242, 180, 72}, {152, 76, 36}},
    {{206, 218, 230}, {246, 248, 252}, {136, 152, 174}}};

/* 只把颜色压暗四分之一：用来画阴影 */
static inline uint16_t darker(uint16_t c) { return (uint16_t)(c - ((c >> 2) & 0x39E7)); }

void render_init(void)
{
    pal['G'] = RGB(62, 148, 76);
    pal['g'] = RGB(104, 188, 96);
    pal['D'] = RGB(40, 108, 62);
    pal['S'] = RGB(78, 146, 70);
    pal['t'] = RGB(134, 94, 56);
    pal['T'] = RGB(98, 66, 40);
    pal['r'] = RGB(226, 62, 92);
    pal['p'] = RGB(246, 150, 180);
    pal['w'] = RGB(250, 250, 250);
    pal['k'] = RGB(36, 30, 36);
    pal['y'] = RGB(255, 226, 96);
    pal['o'] = RGB(250, 150, 50);
    pal['R'] = RGB(226, 70, 50);
    pal['M'] = RGB(160, 48, 40);
    pal['a'] = RGB(158, 160, 170);
    pal['A'] = RGB(112, 114, 128);
    pal['l'] = RGB(204, 206, 214);
    pal['s'] = RGB(250, 208, 170);
    pal['b'] = RGB(140, 96, 66);
    pal['B'] = RGB(98, 64, 46);
    pal['n'] = RGB(186, 136, 76);
    pal['N'] = RGB(134, 92, 50);
    pal['e'] = RGB(52, 46, 46);
    pal['u'] = RGB(70, 84, 130);
    pal['Y'] = RGB(226, 190, 100);
    pal['O'] = RGB(178, 140, 64);
    pal['q'] = RGB(150, 208, 244);
    pal['W'] = RGB(132, 136, 150);
    pal['V'] = RGB(88, 92, 108);
    pal['x'] = RGB(238, 230, 214);
    pal['X'] = RGB(196, 184, 164);
    uint32_t x = 0x9E3779B9u;
    for (int v = 0; v < 4; v++)
        for (int i = 0; i < 64; i++) {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            s_noise[v][i] = (uint8_t)(x & 15);
        }
}

static const char *const SPR_TREE[14] = {"...DGGD...", "..DGgggD..", ".DGgGGGGD.", "DGgGGGGgGD", "GGGGGGGGGD", "GgGGGgGGGD", "DGGGGGGGGD", ".DGGGgGGD.", "..DGGGGD..", "...DDDD...", "....tT....", "....tT....", "....tT....", "...StTS..."};
static const char *const SPR_YOUNG[8] = {"........", "...GG...", "..GgGG..", ".GGGGGD.",
                                         ".DGGGDD.", "..DDDD..", "...tT...", "...tT..."};
static const char *const SPR_SAPLING[8] = {"........", "........", "........", "...g....",
                                           "..gGg...", "...GD...", "...t....", "........"};
static const char *const SPR_STUMP[8] = {"........", "........", "........", "........",
                                         "........", "..tttt..", "..tTTt..", "..TTTT.."};
static const char *const SPR_BUSH[8] = {"........", "........", "..GGG...", ".GGgGGD.",
                                        "GGGGGGDD", "DGGGGDDD", ".DDDDDD.", "........"};
static const char *const SPR_BERRY[8] = {"........", "........", "..GrG...", ".GGgGrD.",
                                         "GrGGGGDD", "DGGrGDrD", ".DDDDDD.", "........"};
static const char *const SPR_FLOWER[8] = {"........", "........", "........", "..y.....",
                                          ".ywy.p..", "..y.pwp.", "..G..p..", ".....G.."};
static const char *const SPR_FIRE_A[8] = {"........", "...o....", "..oyo...", "..yyo.o.",
                                          ".oywyo..", ".oyyyo..", "tTtTtTt.", ".T.t.T.."};
static const char *const SPR_FIRE_B[8] = {"....o...", "..o.....", "..oyoo..", ".oyyyo..",
                                          ".oywyo..", "..yyyo..", "tTtTtTt.", ".T.t.T.."};
static const char *const SPR_FIRE_OFF[8] = {"........", "........", "........", "........",
                                            "...a.a..", "..eaee..", "tTeTeTt.", ".T.t.T.."};
static const char *const SPR_FLAME_A[8] = {"...o....", "..oo.o..", ".oyo.oo.", ".oyyoyo.",
                                           "oyywyyo.", "oywwyyo.", ".oyyyo..", "..RRR..."};
static const char *const SPR_FLAME_B[8] = {".....o..", ".o.oo...", ".oooyo..", "oyoyyo..",
                                           "oyywyyo.", ".oywwyo.", ".oyyyoo.", "..RRRR.."};
static const char *const SPR_STORE[8] = {"..nnnn..", ".nNNNNn.", "nnnnnnnn", "nNnNNnNn",
                                         "nNnNNnNn", "nnnnnnnn", "nNnNNnNn", "NNNNNNNN"};
static const char *const SPR_GRAVE[8] = {"........", "..laa...", ".laaaA..", ".aaAaA..",
                                         ".aAAAA..", ".aaAaA..", ".aaAaA..", "SSSSSSS."};
static const char *const SPR_BURNT[8] = {"........", "...e....", "...e.e..", ".e.ee...",
                                         "..eee...", "...e....", "...e....", "..eee..."};
static const char *const SPR_ROCK[8] = {"...ll...", "..laaa..", ".laaaaA.", ".aaaaAA.",
                                        "laaaaaAA", "aaaaAAAA", "aaAAAAAA", ".AAAAAA."};
static const char *const SPR_HUT[12] = {".....YY.....", "....YOYY....", "...YYYOYY...", "..YOYYYYOY..", ".YYYYOYYYYY.", "OYYOYYYYOYYO", ".nnnnnnnnnn.", ".nqqnTTnqqn.", ".nqqnTTnqqn.", ".nnnnTTnnnn.", ".nnnnTTnnnn.", ".NNNNTTNNNN."};
static const char *const SPR_HOUSE[13] = {"........AA..", "...RRRR.AA..", "..RRMRRRRR..", ".RRRRRMRRRR.", "RRMRRRRRRMRR", "MMMMMMMMMMMM", ".nnnnnnnnnn.", ".nqqnnnnqqn.", ".nqqnTTnqqn.", ".nnnnTTnnnn.", ".nnnnTTnnnn.", ".nnnnTTnnnn.", ".NNNNTTNNNN."};
static const char *const SPR_SITE[8] = {"........", ".t....t.", ".tttttt.", ".t....t.",
                                        ".t....t.", ".tnnnnt.", ".tnnnnt.", "........"};
static const char *const SPR_RUIN[8] = {"........", "........", ".e......", ".n...e..",
                                        ".nA..n..", ".nnaAnn.", "anAnnaAn", "........"};
/* 居民 6x8：h 头发 s 皮肤 c 上衣 u 裤子 */
/* 成年人 8x11 */
/* 成年人 10x14：大头、圆脸、带腮红 */
static const char *const SPR_HUGEMAN_A[14] = {"..hhhhhh..", ".hhhhhhhh.", ".hhhhhhhh.", ".hssssssh.", ".skssssks.", ".pssssssp.", "..cccccc..", ".cccccccc.", "scccccccCs", "scccccccCs", ".ccccccCC.", "..uu..uu..", "..uu..uu..", ".eee..eee."};
static const char *const SPR_HUGEMAN_B[14] = {"..hhhhhh..", ".hhhhhhhh.", ".hhhhhhhh.", ".hssssssh.", ".skssssks.", ".pssssssp.", "..cccccc..", ".cccccccc.", "scccccccCs", "scccccccCs", ".ccccccCC.", "...uuuu...", "...u..u...", "..ee..ee.."};
static const char *const SPR_HUGEWOMAN_A[14] = {"..hhhhhh..", ".hhhhhhhh.", "hhhhhhhhhh", "hhsssssshh", "hskssssksh", "hpssssssph", "hh.ssss.hh", "h.cccccc.h", ".scccccCs.", ".scccccCs.", ".cccccccC.", "cccccccccC", "...s..s...", "..ee..ee.."};
static const char *const SPR_HUGEWOMAN_B[14] = {"..hhhhhh..", ".hhhhhhhh.", "hhhhhhhhhh", "hhsssssshh", "hskssssksh", "hpssssssph", "hh.ssss.hh", "h.cccccc.h", ".scccccCs.", ".scccccCs.", ".cccccccC.", "cccccccccC", "....ss....", "...eeee..."};
/* 成年人 12x17 */
static const char *const SPR_GIANTMAN_A[17] = {"...hhhhhh...", "..hhhhhhhh..", "..hhhhhhhhh.", "..hsssssshh.", ".hskssssksh.", "..spssssps..",
                                               "...ssssss...", "....ssss....", "..cccccccc..", ".scccccccCs.", ".scccccccCs.", ".sccccccCCs.",
                                               "..cccccccC..", "..uuu..uuu..", "..uuu..uuu..", "..uu....uu..", ".eee....eee."};
static const char *const SPR_GIANTMAN_B[17] = {"...hhhhhh...", "..hhhhhhhh..", "..hhhhhhhhh.", "..hsssssshh.", ".hskssssksh.", "..spssssps..",
                                               "...ssssss...", "....ssss....", "..cccccccc..", ".scccccccCs.", ".scccccccCs.", ".sccccccCCs.",
                                               "..cccccccC..", "...uuuuuu...", "....uu.uu...", "....uu..uu..", "...eee..eee."};
static const char *const SPR_GIANTWOMAN_A[17] = {"...hhhhhh...", "..hhhhhhhh..", ".hhhhhhhhhh.", ".hhsssssshh.", ".hskssssksh.", ".hspsssspsh.",
                                                 ".hh.ssss.hh.", ".h...ss...h.", "..cccccccc..", ".scccccccCs.", ".scccccccCs.", "..cccccccC..",
                                                 ".cccccccccC.", "cccccccccccC", "...s....s...", "...s....s...", "..ee....ee.."};
static const char *const SPR_GIANTWOMAN_B[17] = {"...hhhhhh...", "..hhhhhhhh..", ".hhhhhhhhhh.", ".hhsssssshh.", ".hskssssksh.", ".hspsssspsh.",
                                                 ".hh.ssss.hh.", ".h...ss...h.", "..cccccccc..", ".scccccccCs.", ".scccccccCs.", "..cccccccC..",
                                                 ".cccccccccC.", "cccccccccccC", "....s..s....", "....s..s....", "...ee..ee..."};
static const char *const SPR_RUBBLE[8] = {"........", "........", "..la....", ".laaA.l.", ".aAA.laA", "....aaAA", ".la..AA.", ".aA....."};
static const char *const SPR_WELL[9] = {".tTTTTt.", "tTTTTTTt", "..t..t..", "..t..t..", "..t..t..", ".laaaal.", "laqqqqal", "aaAaaAaa", ".AAAAAA."};
static const char *const SPR_GRANARY[12] = {"....YY....", "...YOYY...", "..YYYOYY..", ".YOYYYYOY.", "YYYYOYYYYO", ".nnnnnnnn.", ".nNnNNnNn.", ".nnnTTnnn.", ".nNnTTnNn.", ".nnnnnnnn.", ".T..T..T..", ".T..T..T.."};
static const char *const SPR_TOWER[16] = {"..RRRR..", ".RRRRRR.", "RRMRRMRR", ".tTttTt.", ".t....t.", ".tttttt.", ".t.TT.t.", ".tT..Tt.", ".t.TT.t.", ".tT..Tt.", ".t.TT.t.", ".tT..Tt.", ".t.TT.t.", ".tT..Tt.", "tt....tt", "T......T"};
static const char *const SPR_BOAT[4] = {"N......N", "nNNNNNNn", ".nnnnnn.", "..NNNN.."};
static const char *const SPR_NOTE[5] = {"..ww", "..w.", "..w.", "ww..", "ww.."};
static const char *const SPR_BRICK[13] = {"..AA........", "..AAWWWWW...", ".WWWWWWWWWW.", "WWVWWWWWWVWW", "VVVVVVVVVVVV", ".RMRRMRRMRR.",
                                          ".RqqRRRRqqR.", ".RqqRMRRqqM.", ".RRRRTTRRRR.", ".MRRMTTMRRM.", ".RRRRTTRRRR.", ".RMRRTTRMRR.",
                                          ".AAAATTAAAA."};
static const char *const SPR_HEART[4] = {".r.r.", "rrrrr", ".rrr.", "..r.."};
static const char *const SPR_PIG_A[4] = {".pp..", "ppppk", "ppppp", ".p.p."};
static const char *const SPR_PIG_B[4] = {".pp..", "ppppk", "ppppp", "p.p.."};
static const char *const SPR_DOG_A[6] = {"......Bb", "B....bbk", ".bbbbbbb", ".bbbbbb.", ".b.b.b.b", "........"};
static const char *const SPR_DOG_B[6] = {"......Bb", ".B...bbk", ".bbbbbbb", ".bbbbbb.", "..b.b.b.", "........"};
static const char *const SPR_STATUE[18] = {"....OO....", "...OyyO...", "...yyyO...", "O...yO...O", "yO.yyyO.Oy", ".yOyyyyOy.",
                                           "...yyyyO..", "...yyyyO..", "...yyyOO..", "...yy.yO..", "..yyO.yyO.", ".llllllll.",
                                           ".laaaaaaA.", ".aaaaaaaA.", ".aAaaAaaA.", "llllllllll", "aaaaaaaaaA", "AAAAAAAAAA"};
static const char *const SPR_SHIP[13] = {".......TRR......", ".......TRR......", ".......T........", "....wwwTwww.....",
                                         "...wwwwTwwww....", "...wwwwTwwwww...", "..wwwwwTwwwwww..", "..wwwwwTwwwwww..",
                                         ".......T........", "NN.....T......NN", "NnnnnnnnnnnnnnnN", ".nnNnnnnnnnNnnn.",
                                         "..NNNNNNNNNNNN.."};
static const char *const SPR_BABY[4] = {".ss.", "cssc", "cccc", ".cc."};
static const char *const SPR_SLEEP[4] = {".hhh......", "hhsscccccC", "hssscccccC", ".hhh.uuuu."};
static const char *const SPR_BOAR_A[6] = {"........", ".BbbbB..", "bbbbbbkb", "bbbbbbbp", ".bbbbbb.", ".B..B..."};
static const char *const SPR_BOAR_B[6] = {"........", ".BbbbB..", "bbbbbbkb", "bbbbbbbp", ".bbbbbb.", "..B..B.."};
static const char *const SPR_RABBIT_A[5] = {".x.x..", ".x.x..", "xxkx..", "XxxxxX", ".X..X."};
static const char *const SPR_RABBIT_B[5] = {"..x.x.", ".x.x..", "xxkx..", "XxxxxX", "..XX.."};
static const char *const SPR_WOLF_A[6] = {"......V.", "V.WWWWWW", "VWWWWWkW", ".WWWWWWV", ".WWVWW..", ".V..V..."};
static const char *const SPR_WOLF_B[6] = {"......V.", "V.WWWWWW", "VWWWWWkW", ".WWWWWWV", ".WWVWW..", "..V..V.."};
static const char *const SPR_SHRINE[10] = {"...ll...", "..laal..", "..lyal..", "..aayA..", "..aaaA..",
                                           "..ayaA..", "..aaAA..", ".aaaaAA.", "aAAAAAAA", "SSSSSSSS"};
static const char *const ICON_MIC[8] = {"..ll....", ".llll...", ".llll...", ".llll...",
                                        "a.ll.a..", ".aaaa...", "..aa....", ".aaaa..."};
static const char *const SPR_SUN[8] = {"...y....", ".y.y.y..", "..yyy...", "yyywyyy.",
                                       "..yyy...", ".y.y.y..", "...y....", "........"};
static const char *const SPR_MOON[8] = {"..lll...", ".lll....", "lll.....", "lll.....",
                                        "lll...l.", ".lllll..", "..lll...", "........"};
/* 天气小图标 */
static const char *const ICON_W[W_COUNT][8] = {
    {"........", "........", "........", "........", "........", "........", "........", "........"},
    {"........", "..ll....", ".lllll..", "lllllll.", "lllllll.", ".lllll..", "........", "........"},
    {"..ll....", ".lllll..", "lllllll.", ".lllll..", "........", "q..q..q.", ".q..q...", "........"},
    {"..aa....", ".aaaaa..", "aaaaaaa.", ".aaaaa..", "q.q.q.q.", ".q.q.q..", "q.q.q.q.", "........"},
    {"..aa....", ".aaaaa..", "aaaaaaa.", ".aayaa..", "...yy...", "..yy....", "...y....", "..y....."},
    {"........", "llll....", "....l...", "lllll...", "........", "llllll..", "......l.", ".llll..."},
    {"...w....", ".w.w.w..", "..www...", "wwwwwww.", "..www...", ".w.w.w..", "...w....", "........"}};

static void spr(uint16_t *fb, int fbw, int fbh, int x, int y, const char *const *rows, int w, int h, bool flip)
{
    for (int r = 0; r < h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= fbh) continue;
        const char *row = rows[r];
        for (int c = 0; c < w; c++) {
            char ch = row[flip ? w - 1 - c : c];
            int xx = x + c;
            if (ch == '.' || xx < 0 || xx >= fbw) continue;
            fb[yy * fbw + xx] = pal[(unsigned char)ch];
        }
    }
}
#define MSPR(x, y, rows, w, h, flip) spr(fb_map, SCR_W, MAPV_H, x, y, rows, w, h, flip)

static void mpx(int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < SCR_W && y < MAPV_H) fb_map[y * SCR_W + x] = c;
}

static void rect(uint16_t *fb, int fbw, int fbh, int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y < 0 ? 0 : y; yy < y + h && yy < fbh; yy++)
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < fbw; xx++) fb[yy * fbw + xx] = c;
}
#define MRECT(x, y, w, h, c) rect(fb_map, SCR_W, MAPV_H, x, y, w, h, c)

static uint32_t thash(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

static int terr_at(const world_t *w, int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return T_SEA;
    return TERR(w->tiles[y * MAP_W + x]);
}

/* 一块地面：底色 + 细碎的明暗纹理 */
static void textured(int x0, int y0, uint16_t base, uint16_t dark, uint16_t light, int variant)
{
    const uint8_t *n = s_noise[variant & 3];
    for (int r = 0; r < TILE; r++) {
        int y = y0 + r;
        if (y < 0 || y >= MAPV_H) continue;
        uint16_t *p = fb_map + y * SCR_W + x0;
        for (int c = 0; c < TILE; c++) {
            int v = n[r * 8 + c];
            p[c] = v < 2 ? dark : v == 15 ? light : base;
        }
    }
}

static bool near_terr(const world_t *w, int tx, int ty, int terr)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dx || dy) && terr_at(w, tx + dx, ty + dy) == terr) return true;
    return false;
}

static void tile_base(const world_t *w, int tx, int ty, uint32_t frame)
{
    int idx = ty * MAP_W + tx;
    int terr = terr_at(w, tx, ty);
    int x0 = tx * TILE, y0 = ty * TILE;
    uint32_t h = thash(tx, ty);
    const uint8_t (*g)[3] = GRASS_RGB[s_snowy ? SEASON_WINTER : s_season];
    uint16_t gb = RGB(g[0][0], g[0][1], g[0][2]), gd = RGB(g[1][0], g[1][1], g[1][2]), gl = RGB(g[2][0], g[2][1], g[2][2]);

    if (sim_flooded(w, idx)) { /* 被淹的陆地：浑浊的水面 */
        MRECT(x0, y0, TILE, TILE, C_FLOOD);
        int ph = (int)((frame / 2 + h) % 10);
        if (ph < 6) mpx(x0 + ph, y0 + 1 + (int)(h >> 5) % 6, C_FOAM), mpx(x0 + ph + 1, y0 + 1 + (int)(h >> 5) % 6, C_FOAM);
        return;
    }
    switch (terr) {
    case T_SEA:
    case T_LAKE: {
        /* 海：离岸越远越深；冬天的湖结一层冰 */
        bool shallow = terr == T_SEA ? near_terr(w, tx, ty, T_SHOAL) : false;
        uint16_t base = terr == T_LAKE ? (s_season == SEASON_WINTER ? RGB(176, 210, 232) : C_LAKE)
                                       : shallow ? RGB(56, 128, 192) : RGB(38, 96, 164);
        MRECT(x0, y0, TILE, TILE, base);
        if (terr == T_LAKE && s_season == SEASON_WINTER) {
            mpx(x0 + (int)(h % 6), y0 + (int)(h >> 4) % 6, pal['w']);
            mpx(x0 + (int)(h % 6) + 1, y0 + (int)(h >> 4) % 6 + 1, pal['w']);
            break;
        }
        int ph = (int)((h + frame / 3) % 24); /* 缓慢移动的波光 */
        if (ph < 5) {
            int yy = y0 + (int)(h >> 8) % 7, xx = x0 + ph;
            mpx(xx, yy, C_SEA_HI);
            mpx(xx + 1, yy, C_SEA_HI);
            mpx(xx + 2, yy, C_SEA_HI);
        }
        if (!shallow && terr == T_SEA && (h & 7) == 0) mpx(x0 + (int)(h >> 3) % 8, y0 + (int)(h >> 7) % 8, RGB(30, 82, 146));
        break;
    }
    case T_SHOAL: {
        MRECT(x0, y0, TILE, TILE, C_SHOAL);
        if ((h & 3) == 0) mpx(x0 + (int)(h >> 4) % 8, y0 + (int)(h >> 8) % 8, RGB(120, 196, 226));
        int wob = (int)((frame / 6 + h) % 4) == 0; /* 贴着沙滩的浪花，轻轻进退 */
        if (terr_at(w, tx, ty - 1) == T_SAND) MRECT(x0, y0, TILE, 1 + wob, C_FOAM);
        if (terr_at(w, tx, ty + 1) == T_SAND) MRECT(x0, y0 + 7 - wob, TILE, 1 + wob, C_FOAM);
        if (terr_at(w, tx - 1, ty) == T_SAND) MRECT(x0, y0, 1 + wob, TILE, C_FOAM);
        if (terr_at(w, tx + 1, ty) == T_SAND) MRECT(x0 + 7 - wob, y0, 1 + wob, TILE, C_FOAM);
        break;
    }
    case T_SAND: {
        if (s_snowy) textured(x0, y0, C_SNOW, C_SNOW_D, pal['w'], (int)h);
        else textured(x0, y0, C_SAND, C_SAND_D, RGB(248, 232, 186), (int)h);
        /* 靠水一侧是湿沙 */
        uint16_t wet = s_snowy ? C_SNOW_D : RGB(206, 180, 126);
        if (terr_at(w, tx, ty + 1) == T_SHOAL) MRECT(x0, y0 + 6, TILE, 2, wet);
        if (terr_at(w, tx, ty - 1) == T_SHOAL) MRECT(x0, y0, TILE, 1, wet);
        if (terr_at(w, tx - 1, ty) == T_SHOAL) MRECT(x0, y0, 1, TILE, wet);
        if (terr_at(w, tx + 1, ty) == T_SHOAL) MRECT(x0 + 7, y0, 1, TILE, wet);
        break;
    }
    case T_RIVER: {
        MRECT(x0, y0, TILE, TILE, s_season == SEASON_WINTER ? RGB(170, 206, 230) : C_RIVER);
        if (s_season == SEASON_WINTER) break;
        int ph = (int)((frame / 2 + h) % 8);
        mpx(x0 + ph, y0 + 2 + (int)(h % 4), C_RIVER_HI);
        mpx(x0 + (ph + 4) % 8, y0 + 1 + (int)(h >> 5) % 6, C_RIVER_HI);
        break;
    }
    case T_ASH:
        textured(x0, y0, C_ASH, C_ASH_D, RGB(126, 118, 110), (int)h);
        break;
    case T_ROAD: { /* 路：工业时代是石板路，电气以后是柏油路，中间有虚线 */
        bool paved = w->era >= ERA_ELECTRIC;
        uint16_t base = paved ? RGB(78, 80, 88) : RGB(150, 140, 122), edge = paved ? RGB(168, 170, 176) : RGB(120, 110, 96);
        MRECT(x0, y0, TILE, TILE, base);
        bool l = terr_at(w, tx - 1, ty) == T_ROAD, r = terr_at(w, tx + 1, ty) == T_ROAD;
        bool u = terr_at(w, tx, ty - 1) == T_ROAD, d = terr_at(w, tx, ty + 1) == T_ROAD;
        if (!paved) { /* 石板缝 */
            for (int k = 0; k < 3; k++) mpx(x0 + (int)((h >> (k * 3)) % 8), y0 + (int)((h >> (k * 3 + 9)) % 8), edge);
            mpx(x0 + 3, y0 + 3, RGB(170, 160, 140)), mpx(x0 + 6, y0 + 6, RGB(170, 160, 140));
        } else if ((l || r) && !(u || d)) {
            if ((tx & 1) == 0) MRECT(x0 + 2, y0 + 3, 4, 1, C_GOLD);
        } else if ((u || d) && !(l || r)) {
            if ((ty & 1) == 0) MRECT(x0 + 3, y0 + 2, 1, 4, C_GOLD);
        }
        if (!u) MRECT(x0, y0, TILE, 1, edge); /* 路沿 */
        if (!d) MRECT(x0, y0 + 7, TILE, 1, edge);
        if (!l) MRECT(x0, y0, 1, TILE, edge);
        if (!r) MRECT(x0 + 7, y0, 1, TILE, edge);
        if (s_snowy) for (int k = 0; k < 4; k++) mpx(x0 + (int)((h >> (k * 4)) % 8), y0 + (int)((h >> (k * 4 + 2)) % 8), C_SNOW);
        break;
    }
    case T_LAVA: { /* 翻滚的岩浆：亮斑游走，暗色硬壳 */
        MRECT(x0, y0, TILE, TILE, C_LAVA);
        int ph = (int)((frame / 3 + h) % 6);
        MRECT(x0 + (int)(h % 5), y0 + ph, 3, 2, C_LAVA_HI);
        mpx(x0 + (int)(h >> 9) % 7, y0 + (int)(h >> 13) % 7, C_LAVA_D);
        mpx(x0 + (int)(h >> 17) % 7, y0 + (int)(h >> 21) % 7, C_LAVA_D);
        break;
    }
    default: { /* 草地 / 山地 */
        textured(x0, y0, gb, gd, gl, (int)h);
        if (!s_snowy && (h & 3) != 3) { /* 草丛 */
            int ax = x0 + 1 + (int)(h % 5), ay = y0 + 2 + (int)(h >> 4) % 5;
            mpx(ax, ay, gd), mpx(ax + 2, ay, gd), mpx(ax + 1, ay - 1, gd);
        }
        /* 靠沙滩的草地边缘 */
        if (terr == T_GRASS && terr_at(w, tx, ty + 1) == T_SAND) MRECT(x0, y0 + 7, TILE, 1, gd);
        if (terr == T_ROCK) {
            MSPR(x0, y0, IS_RUBBLE(w->tiles[idx]) ? SPR_RUBBLE : SPR_ROCK, 8, 8, h & 1);
            if (s_snowy) mpx(x0 + 3, y0, pal['w']), mpx(x0 + 4, y0, pal['w']), mpx(x0 + 2, y0 + 1, pal['w']), mpx(x0 + 3, y0 + 1, pal['w']);
        }
        break;
    }
    }
}

static void draw_farm(int x0, int y0, int v, uint32_t h)
{
    if (s_season == SEASON_WINTER) { /* 冬天：田里覆着雪，只看得见垄沟 */
        MRECT(x0, y0, TILE, TILE, C_SNOW);
        for (int r = 2; r < TILE; r += 3) MRECT(x0 + 1, y0 + r, TILE - 2, 1, C_SNOW_D); /* 雪下隐约的垄沟 */
        return;
    }
    MRECT(x0, y0, TILE, TILE, C_SOIL);
    for (int r = 1; r < TILE; r += 2) MRECT(x0, y0 + r, TILE, 1, C_SOIL_D);
    int stage = v >= 100 ? 3 : v >= 60 ? 2 : v >= 25 ? 1 : 0;
    for (int c = 1; c < TILE; c += 2) {
        int jig = (int)((h >> c) & 1);
        for (int r = 0; r < 3; r++) {
            int yy = y0 + 1 + r * 2 + (r == 1 ? 1 : 0) + jig;
            if (stage == 0) {
                if ((c + r) & 2) mpx(x0 + c, yy + 1, pal['g']);
            } else if (stage == 1) {
                mpx(x0 + c, yy + 1, pal['g']);
            } else if (stage == 2) {
                mpx(x0 + c, yy + 1, pal['G']);
                mpx(x0 + c, yy, pal['g']);
            } else {
                mpx(x0 + c, yy + 1, pal['O']);
                mpx(x0 + c, yy, pal['y']);
            }
        }
    }
}

/* 地上的一小片椭圆阴影 */
static void shadow(int x, int y, int wpx)
{
    for (int c = 0; c < wpx; c++) {
        int xx = x + c;
        if (xx < 0 || xx >= SCR_W) continue;
        for (int r = 0; r < 2; r++) {
            if (r == 1 && (c == 0 || c == wpx - 1)) continue;
            int yy = y + r;
            if (yy >= 0 && yy < MAPV_H) fb_map[yy * SCR_W + xx] = darker(fb_map[yy * SCR_W + xx]);
        }
    }
}

/* 屋顶在冬天积雪：临时把屋顶的颜色换成雪色 */
static void snow_roof(bool on)
{
    static uint16_t save[4];
    static const char K[4] = {'Y', 'O', 'R', 'M'};
    for (int i = 0; i < 4; i++) {
        if (on) save[i] = pal[(int)K[i]], pal[(int)K[i]] = i & 1 ? C_SNOW_D : C_SNOW;
        else pal[(int)K[i]] = save[i];
    }
}

static bool is_water(int t) { return t == T_SEA || t == T_SHOAL; }

static bool s_night; /* 天黑了：窗户、路灯亮起来 */

/* 楼：一块墙面加一格格窗户；夜里有些窗户亮着灯 */
static void draw_block(int x, int y, int bw, int bh, uint16_t wall, uint16_t dark, uint32_t h, uint32_t frame, bool winter)
{
    (void)frame;
    MRECT(x, y, bw, bh, wall);
    MRECT(x + bw - 2, y, 2, bh, dark); /* 背光的一面 */
    MRECT(x, y, bw, 1, dark);
    for (int r = 2; r < bh - 5; r += 4)
        for (int c = 1; c < bw - 3; c += 3) {
            bool lit = s_night && (thash(x + c, y + r) + h) % 3 != 0;
            uint16_t win = lit ? RGB(255, 222, 130) : RGB(70, 96, 130);
            MRECT(x + c, y + r, 2, 2, win);
        }
    MRECT(x + bw / 2 - 1, y + bh - 4, 3, 4, RGB(60, 50, 44)); /* 大门 */
    if (winter) MRECT(x, y - 1, bw, 1, C_SNOW);
}

static void tile_obj(const world_t *w, int tx, int ty, uint32_t frame)
{
    int x0 = tx * TILE, y0 = ty * TILE, idx = ty * MAP_W + tx;
    uint16_t t = w->tiles[idx];
    uint32_t h = thash(tx, ty);
    bool winter = s_season == SEASON_WINTER;
    switch (OBJ(t)) {
    case O_SAPLING: MSPR(x0, y0, SPR_SAPLING, 8, 8, false); break;
    case O_YOUNG:
        shadow(x0 + 1, y0 + 6, 6);
        MSPR(x0, y0, SPR_YOUNG, 8, 8, false);
        break;
    case O_TREE:
        shadow(x0, y0 + 6, 9);
        MSPR(x0 - 1, y0 - 6, SPR_TREE, 10, 14, h & 4);
        if (s_season == SEASON_SPRING && !s_snowy) /* 春天开花 */
            for (int k = 0; k < 3; k++) mpx(x0 + (int)((h >> (k * 5)) % 8), y0 - 5 + (int)((h >> (k * 5 + 3)) % 7), pal['p']);
        if (s_season == SEASON_AUTUMN && ((frame / 6 + h) % 7) == 0) /* 秋天飘落的叶子 */
            mpx(x0 + (int)(h % 8) - 2 + (int)(frame / 6 % 3), y0 + (int)(frame / 2 % 10) - 3, pal['o']);
        break;
    case O_STUMP: MSPR(x0, y0, SPR_STUMP, 8, 8, false); break;
    case O_BUSH: MSPR(x0, y0, SPR_BUSH, 8, 8, false); break;
    case O_BERRY: MSPR(x0, y0, SPR_BERRY, 8, 8, false); break;
    case O_FLOWER:
        if (!winter) MSPR(x0, y0, SPR_FLOWER, 8, 8, h & 4);
        break;
    case O_FIRE:
        MSPR(x0, y0, !w->fire_lit ? SPR_FIRE_OFF : (frame / 3) & 1 ? SPR_FIRE_A : SPR_FIRE_B, 8, 8, false);
        break;
    case O_STORE:
        shadow(x0, y0 + 7, 8);
        MSPR(x0, y0, SPR_STORE, 8, 8, false);
        if (w->food > 0) { /* 仓库顶上露出食物 */
            mpx(x0 + 2, y0 - 1, pal['R']);
            mpx(x0 + 3, y0 - 1, pal['y']);
            if (w->food > 6) mpx(x0 + 4, y0 - 1, pal['R']), mpx(x0 + 5, y0 - 1, pal['o']);
        }
        break;
    case O_GRAVE: MSPR(x0, y0, SPR_GRAVE, 8, 8, false); break;
    case O_BURNT: MSPR(x0, y0, SPR_BURNT, 8, 8, false); break;
    case O_FARM:
        if (!sim_flooded(w, idx)) draw_farm(x0, y0, VAL(t), h);
        break;
    case O_SITE:
        MSPR(x0, y0, SPR_SITE, 8, 8, false);
        if (VAL(t) && idx == w->proj_idx && w->proj_need) { /* 公共工地：头顶一条进度 */
            MRECT(x0, y0 - 3, 8, 2, pal['e']);
            MRECT(x0, y0 - 3, 8 * w->proj_prog / w->proj_need, 2, C_GOLD);
        }
        break;
    case O_HOUSE1:
        shadow(x0 - 1, y0 + 7, 11);
        if (winter) snow_roof(true);
        MSPR(x0 - 2, y0 - 4, SPR_HUT, 12, 12, false);
        if (winter) snow_roof(false);
        if (VAL(t) < 70) mpx(x0 + 1, y0 - 1, pal['e']), mpx(x0 + 5, y0 - 3, pal['e']), mpx(x0 + 7, y0, pal['e']);
        break;
    case O_HOUSE2: {
        if (w->era >= ERA_MODERN) { /* 现代：楼房 */
            shadow(x0 - 1, y0 + 7, 11);
            draw_block(x0 - 2, y0 - 14, 12, 22, RGB(214, 206, 196), RGB(176, 166, 156), h, frame, winter);
            break;
        }
        if (w->era >= ERA_INDUSTRY) { /* 工业、电气：红砖房，烟囱冒烟 */
            shadow(x0 - 1, y0 + 7, 11);
            if (winter) snow_roof(true);
            uint16_t q0 = pal['q'];
            if (s_night) pal['q'] = RGB(255, 222, 130);
            MSPR(x0 - 2, y0 - 5, SPR_BRICK, 12, 13, false);
            pal['q'] = q0;
            if (winter) snow_roof(false);
            if (!((frame / 4 + h) % 3)) mpx(x0 + 2, y0 - 7 - (int)(frame / 4 % 3), pal['l']), mpx(x0 + 3, y0 - 8 - (int)(frame / 4 % 3), pal['l']);
            break;
        }
        shadow(x0 - 1, y0 + 7, 11);
        uint16_t n0 = pal['n'], N0 = pal['N'];
        if (w->era >= ERA_IRON) pal['n'] = RGB(178, 176, 184), pal['N'] = RGB(124, 122, 136); /* 铁器时代：石墙 */
        if (winter) snow_roof(true);
        MSPR(x0 - 2, y0 - 5, SPR_HOUSE, 12, 13, false);
        if (winter) snow_roof(false);
        pal['n'] = n0, pal['N'] = N0;
        if (VAL(t) < 70) mpx(x0 + 1, y0 - 2, pal['e']), mpx(x0 + 5, y0 - 3, pal['e']), mpx(x0 + 6, y0 - 1, pal['e']);
        if (!((frame / 4 + h) % 3)) mpx(x0 + 7, y0 - 6 - (int)(frame / 4 % 2), pal['l']); /* 炊烟 */
        break;
    }
    case O_RUIN: MSPR(x0, y0, SPR_RUIN, 8, 8, false); break;
    case O_SHRINE:
        shadow(x0 + 1, y0 + 7, 7);
        MSPR(x0, y0 - 2, SPR_SHRINE, 8, 10, false);
        if (w->faith >= 50 && !((frame / 5 + h) % 4)) mpx(x0 + 3 + (int)(frame / 5 % 2), y0 - 4, pal['y']); /* 香火 */
        break;
    case O_WELL:
        shadow(x0, y0 + 7, 8);
        MSPR(x0, y0 - 1, SPR_WELL, 8, 9, false);
        if (winter) MRECT(x0 + 1, y0 - 1, 6, 1, C_SNOW);
        break;
    case O_GRANARY:
        shadow(x0 - 1, y0 + 7, 10);
        if (winter) snow_roof(true);
        MSPR(x0 - 1, y0 - 4, SPR_GRANARY, 10, 12, false);
        if (winter) snow_roof(false);
        break;
    case O_TOWER:
        shadow(x0, y0 + 7, 9);
        if (winter) snow_roof(true);
        MSPR(x0, y0 - 8, SPR_TOWER, 8, 16, false);
        if (winter) snow_roof(false);
        if (sim_kind_count(w, A_WOLF) && (frame & 2)) MRECT(x0 + 3, y0 - 11, 2, 2, pal['R']); /* 警报 */
        break;
    case O_FENCE: { /* 栅栏：和相邻的栅栏连成一排 */
        uint16_t post = pal['t'], top = pal['T'];
        MRECT(x0 + 3, y0 + 1, 2, 6, post);
        MRECT(x0 + 3, y0 + 1, 2, 1, top);
        int right = tx + 1 < MAP_W && OBJ(w->tiles[idx + 1]) == O_FENCE, left = tx > 0 && OBJ(w->tiles[idx - 1]) == O_FENCE;
        int down = ty + 1 < MAP_H && OBJ(w->tiles[idx + MAP_W]) == O_FENCE;
        if (right) MRECT(x0 + 5, y0 + 2, 3, 1, post), MRECT(x0 + 5, y0 + 5, 3, 1, post);
        if (left) MRECT(x0, y0 + 2, 3, 1, post), MRECT(x0, y0 + 5, 3, 1, post);
        if (down) MRECT(x0 + 3, y0 + 7, 2, 2, post);
        if (winter) mpx(x0 + 3, y0, C_SNOW), mpx(x0 + 4, y0, C_SNOW);
        break;
    }
    case O_DOCK: { /* 码头：木板伸进水里，旁边停着渔船 */
        int dx = 0, dy = 0;
        for (int d = 0; d < 4; d++) {
            static const int8_t DX4[4] = {1, -1, 0, 0}, DY4[4] = {0, 0, 1, -1};
            if (is_water(terr_at(w, tx + DX4[d], ty + DY4[d]))) { dx = DX4[d], dy = DY4[d]; break; }
        }
        for (int k = 0; k < 2; k++) { /* 两格长的栈桥 */
            int px = x0 + dx * TILE * k, py = y0 + dy * TILE * k;
            for (int r = 0; r < TILE; r += 2) MRECT(px + (dy ? 2 : 0), py + r, dy ? 4 : TILE, 1, pal['n']);
            for (int r = 1; r < TILE; r += 2) MRECT(px + (dy ? 2 : 0), py + r, dy ? 4 : TILE, 1, pal['N']);
        }
        /* 有人在码头捕鱼时，船开出去；否则泊在栈桥边 */
        bool out = false;
        for (int i = 0; i < MAX_RES; i++) {
            const resident_t *r = &w->res[i];
            if (r->alive && r->state == S_ACT && r->goal == G_FISH && r->tx == tx && r->ty == ty) out = true;
        }
        int dist = out ? 3 + (int)((frame / 8 + h) % 3) : 1;
        int bx = x0 + dx * TILE * dist + (dy ? 6 : 0), by = y0 + dy * TILE * dist + (dx ? 4 : 0) + (int)(frame / 6 % 2);
        MSPR(bx, by, SPR_BOAT, 8, 4, dx < 0);
        if (out) MRECT(bx + 3, by - 3, 2, 3, pal['s']), mpx(bx + 6, by - 4, pal['t']), mpx(bx + 7, by - 5, pal['t']);
        break;
    }
    case O_FLAME: MSPR(x0, y0 - 1, ((frame / 2 + h) & 1) ? SPR_FLAME_A : SPR_FLAME_B, 8, 8, (frame / 4 + h) & 1); break;
    case O_FACTORY: { /* 工厂：锯齿屋顶，高烟囱，滚滚白烟 */
        shadow(x0 - 2, y0 + 7, 12);
        MRECT(x0 - 2, y0 - 2, 12, 10, RGB(150, 70, 56));
        for (int r = 0; r < 10; r += 3) MRECT(x0 - 2, y0 - 2 + r, 12, 1, RGB(120, 54, 44));
        for (int k = 0; k < 3; k++) { /* 锯齿屋顶 */
            MRECT(x0 - 2 + k * 4, y0 - 4, 4, 2, RGB(96, 98, 110));
            mpx(x0 - 2 + k * 4 + 3, y0 - 5, RGB(96, 98, 110));
        }
        MRECT(x0, y0 + 1, 2, 2, RGB(255, 214, 120)), MRECT(x0 + 5, y0 + 1, 2, 2, RGB(255, 214, 120));
        MRECT(x0 + 3, y0 + 4, 2, 4, pal['T']);
        MRECT(x0 + 6, y0 - 13, 3, 11, RGB(130, 60, 50)); /* 烟囱 */
        MRECT(x0 + 6, y0 - 13, 3, 1, RGB(90, 40, 34));
        for (int k = 0; k < 6; k++) { /* 烟 */
            int up = (int)((frame / 2 + (uint32_t)k * 5) % 24);
            int sx = x0 + 7 + up / 4 + (int)((h >> k) & 1), sy = y0 - 15 - up;
            uint16_t c = up < 12 ? pal['l'] : pal['a'];
            mpx(sx, sy, c), mpx(sx + 1, sy, c);
            if (up > 6) mpx(sx, sy - 1, c);
        }
        break;
    }
    case O_LAMP: { /* 路灯 */
        MRECT(x0 + 3, y0 - 6, 1, 13, RGB(60, 62, 72));
        MRECT(x0 + 2, y0 + 6, 3, 1, RGB(60, 62, 72));
        MRECT(x0 + 3, y0 - 7, 3, 1, RGB(60, 62, 72));
        MRECT(x0 + 4, y0 - 6, 2, 2, s_night ? RGB(255, 236, 150) : RGB(200, 200, 190));
        break;
    }
    case O_SKYSCRAPER: /* 摩天楼：玻璃幕墙，楼顶的航标灯一闪一闪 */
        shadow(x0 - 3, y0 + 7, 14);
        draw_block(x0 - 3, y0 - 38, 14, 46, RGB(120, 170, 210), RGB(80, 120, 168), h, frame, winter);
        MRECT(x0 + 3, y0 - 44, 1, 6, RGB(150, 150, 160));
        if ((frame / 5) & 1) MRECT(x0 + 2, y0 - 45, 3, 2, RGB(255, 60, 60));
        break;
    case O_PEN: { /* 牧场：一圈木栏，里面拱来拱去的猪 */
        uint16_t post = pal['t'];
        MRECT(x0 - 1, y0, 10, 1, post), MRECT(x0 - 1, y0 + 7, 10, 1, post);
        MRECT(x0 - 1, y0, 1, 8, post), MRECT(x0 + 8, y0, 1, 8, post);
        MRECT(x0 - 1, y0 + 3, 10, 1, pal['T']);
        MRECT(x0, y0 + 1, 8, 6, winter ? C_SNOW_D : C_SOIL);
        int n = w->livestock > 3 ? 3 : w->livestock;
        for (int k = 0; k < n; k++) {
            int ph = (int)((frame / 6 + (uint32_t)k * 5 + h) % 8);
            int px = x0 + (k * 3 + (ph < 4 ? ph : 7 - ph) / 2) % 4, py = y0 + 1 + (k % 2) * 2;
            MSPR(px, py, (frame / 3 + (uint32_t)k) & 1 ? SPR_PIG_A : SPR_PIG_B, 5, 4, k & 1);
        }
        if (winter) MRECT(x0 - 1, y0, 10, 1, C_SNOW);
        break;
    }
    case O_STATUE: /* 神像：金身立在石座上，信仰越高越亮 */
        shadow(x0 - 1, y0 + 7, 11);
        MSPR(x0 - 1, y0 - 10, SPR_STATUE, 10, 18, false);
        if (w->faith >= 60 && !((frame / 3 + h) % 3)) mpx(x0 + 3 + (int)(frame / 3 % 3), y0 - 12, pal['w']);
        if (winter) MRECT(x0 - 1, y0 - 1, 10, 1, C_SNOW);
        break;
    case O_SHIP: { /* 码头边的大船；出海时只剩空船台 */
        int dx = 0, dy = 0;
        for (int d = 0; d < 4; d++) {
            static const int8_t DX4[4] = {1, -1, 0, 0}, DY4[4] = {0, 0, 1, -1};
            if (is_water(terr_at(w, tx + DX4[d], ty + DY4[d]))) { dx = DX4[d], dy = DY4[d]; break; }
        }
        MRECT(x0, y0 + 2, 8, 1, pal['N']), MRECT(x0, y0 + 5, 8, 1, pal['N']); /* 船台 */
        if (VAL(t) == 1) break;
        int bx = x0 + dx * 8 - 4, by = y0 + dy * 8 - 6 + (int)(frame / 6 % 2);
        MSPR(bx, by, SPR_SHIP, 16, 13, dx < 0);
        break;
    }
    default: break;
    }
}

/* 石器时代穿兽皮 */
static const uint16_t LEATHER[MAX_RES] = {RGB(150, 104, 64), RGB(128, 88, 56),  RGB(172, 126, 80), RGB(112, 80, 52),
                                          RGB(160, 116, 70), RGB(138, 98, 62), RGB(182, 140, 92), RGB(122, 92, 60),
                                          RGB(166, 110, 74), RGB(146, 106, 70)};
static const uint16_t SHIRT[MAX_RES] = {RGB(226, 78, 70),  RGB(74, 126, 226), RGB(246, 200, 66),  RGB(160, 100, 214),
                                        RGB(60, 180, 170), RGB(244, 140, 60), RGB(240, 130, 180), RGB(240, 240, 236),
                                        RGB(120, 190, 80), RGB(96, 104, 140)};

static int s_sub; /* 两个 tick 之间的进度，由 render_map 设置 */

/* 居民贴图（10x14）左上角的像素位置；走路时按 tick 内进度插值，画面更顺 */
static void res_pos(const resident_t *r, int *x, int *y)
{
    int off = r->step;
    if (off && r->state == S_MOVE) off += 2 * s_sub / 256;
    if (off > TILE) off = TILE;
    *x = r->tx * TILE + (r->nx - r->tx) * off - 1;
    *y = r->ty * TILE + (r->ny - r->ty) * off - 7;
}

/* 头顶的小气泡：一眼看出这个人此刻的心情 */
static void bubble(int x, int y)
{
    MRECT(x + 1, y, 5, 1, pal['w']); /* 圆角 */
    MRECT(x, y + 1, 7, 4, pal['w']);
    MRECT(x + 1, y + 5, 5, 1, pal['w']);
    mpx(x + 2, y + 6, pal['w']);
}
static void emote(const world_t *w, int i, int mid, int top, uint32_t frame)
{
    const resident_t *r = &w->res[i];
    if ((frame / 10 + (uint32_t)i * 7) % 9 != 0) return; /* 每隔一会儿冒一下 */
    int bx = mid - 3, by = top - 10, kind = 0;
    for (int j = 0; j < MAX_RES && !kind; j++)
        if (w->feud[i][j] >= 50 && w->res[j].alive && abs(w->res[j].tx - r->tx) + abs(w->res[j].ty - r->ty) <= 6) kind = 1; /* 仇人就在附近 */
    if (!kind && r->devo <= -40 && (w->weather == W_THUNDER || w->weather == W_STORM)) kind = 2;
    if (!kind && r->energy < 250) kind = 3;
    if (!kind && r->mood < 300) kind = 4;
    if (!kind && r->mood >= 780) {
        const resident_t *p = r->partner != NO_SLOT ? &w->res[r->partner] : NULL;
        kind = (p && p->alive && abs(p->tx - r->tx) + abs(p->ty - r->ty) <= 4) ? 5 : 6;
    }
    if (!kind && r->fun < 200) kind = 7;
    if (!kind) return;
    bubble(bx, by);
    switch (kind) {
    case 1: /* 生气 */
        mpx(bx + 2, by + 1, pal['R']), mpx(bx + 4, by + 1, pal['R']), mpx(bx + 3, by + 2, pal['R']);
        mpx(bx + 2, by + 3, pal['R']), mpx(bx + 4, by + 3, pal['R']);
        break;
    case 2: /* 害怕 */
        MRECT(bx + 3, by + 1, 1, 3, pal['u']), mpx(bx + 3, by + 4, pal['u']);
        break;
    case 3: /* 困 */
        MRECT(bx + 1, by + 1, 3, 1, pal['u']), mpx(bx + 3, by + 2, pal['u']), mpx(bx + 2, by + 3, pal['u']), MRECT(bx + 1, by + 4, 3, 1, pal['u']);
        mpx(bx + 5, by + 1, pal['u']);
        break;
    case 4: /* 难过：一滴眼泪 */
        mpx(bx + 3, by + 1, pal['q']), MRECT(bx + 2, by + 2, 3, 2, pal['q']);
        break;
    case 5: /* 爱心 */
        mpx(bx + 2, by + 1, pal['r']), mpx(bx + 4, by + 1, pal['r']), MRECT(bx + 1, by + 2, 5, 1, pal['r']);
        MRECT(bx + 2, by + 3, 3, 1, pal['r']), mpx(bx + 3, by + 4, pal['r']);
        break;
    case 6: /* 开心：音符 */
        MRECT(bx + 4, by + 1, 1, 3, pal['k']), mpx(bx + 5, by + 1, pal['k']), MRECT(bx + 2, by + 3, 2, 2, pal['k']);
        break;
    default: /* 无聊：…… */
        mpx(bx + 1, by + 3, pal['a']), mpx(bx + 3, by + 3, pal['a']), mpx(bx + 5, by + 3, pal['a']);
        break;
    }
}

static void draw_resident(const world_t *w, int i, uint32_t frame)
{
    const resident_t *r = &w->res[i];
    int x, y, age = sim_age(r);
    res_pos(r, &x, &y);
    uint16_t c = w->era == ERA_STONE ? LEATHER[i] : SHIRT[i];
    pal['c'] = c;
    pal['C'] = (uint16_t)(((c >> 1) & 0x7BEF)); /* 上衣暗部 */
    static const uint16_t HAIR[4] = {RGB(48, 36, 40), RGB(112, 76, 44), RGB(150, 76, 40), RGB(70, 52, 38)};
    pal['h'] = HAIR[r->look & 3];
    if (age >= AGE_ELDER) pal['h'] = RGB(200, 200, 206); /* 老人白了头 */

    if (r->state == S_ACT && r->goal == G_FISH && OBJ(w->tiles[r->ty * MAP_W + r->tx]) == O_DOCK) return; /* 出海了，人在船上 */
    bool indoors = sim_in_house(w, r);
    if (!indoors && !(r->state == S_ACT && r->goal == G_SWIM)) shadow(x + 1, y + 13, 8);
    bool napping = r->state == S_ACT && r->goal == G_NAP;
    if (indoors && (r->state == S_SLEEP || age < AGE_CHILD || napping ||
                    (r->state == S_ACT && (r->goal == G_SHELTER || r->goal == G_COWER)))) {
        if (r->state == S_SLEEP || napping) { /* 人在屋里看不见，屋顶上飘着 z */
            int ph = (int)((frame / 4 + (uint32_t)i * 3) % 6);
            if (ph < 4) mpx(x + 6 + ph / 2, y + 1 - ph, pal['w']), mpx(x + 7 + ph / 2, y + 1 - ph, pal['w']);
        }
        return;
    }
    if (age < AGE_CHILD) {
        MSPR(x + 3, y + 10, SPR_BABY, 4, 4, false);
        return;
    }
    if (r->state == S_ACT && r->goal == G_SWIM) { /* 游泳：人在水里，只露出头和肩膀，周围一圈水波 */
        static const int8_t DXS[4] = {1, -1, 0, 0}, DYS[4] = {0, 0, 1, -1};
        int d = r->tgt & 3, sx = x + DXS[d] * TILE, sy = y + DYS[d] * TILE + 6 + (int)((frame / 3 + (uint32_t)i) & 1);
        const char *const *sp = age < AGE_WORK ? (r->sex ? SPR_HUGEWOMAN_A : SPR_HUGEMAN_A) : (r->sex ? SPR_GIANTWOMAN_A : SPR_GIANTMAN_A);
        int sw = age < AGE_WORK ? 10 : 12, rows = age < AGE_WORK ? 7 : 8;
        if (sw == 12) sy -= 1;
        MSPR(sx + (10 - sw) / 2, sy, sp, sw, rows, r->face);
        int ph = (int)((frame / 2 + (uint32_t)i) % 4), wy = sy + rows;
        for (int k = 0; k < 3; k++) mpx(sx - 1 - ph / 2 + k, wy, pal['w']), mpx(sx + 8 + ph / 2 + k, wy, pal['w']);
        if (ph == 0) mpx(sx + (r->face ? -2 : 11), wy - 2, pal['q']); /* 溅起的水花 */
        return;
    }
    if (r->state == S_SLEEP || napping) {
        MSPR(x, y + 10, SPR_SLEEP, 10, 4, false);
        int ph = (int)((frame / 4 + (uint32_t)i * 3) % 6); /* 飘起来的 z */
        if (ph < 4) {
            int zx = x + 7 + ph / 2, zy = y + 8 - ph;
            mpx(zx, zy, pal['w']);
            mpx(zx + 1, zy, pal['w']);
            mpx(zx, zy + 1, pal['w']);
        }
        return;
    }
    bool walking = r->state == S_MOVE;
    bool acting = false;
    if (r->state == S_ACT) switch (r->goal) {
        case G_CHOP: case G_BERRY: case G_HUNT: case G_EAT_BERRY: case G_BUILD: case G_UPGRADE: case G_REPAIR:
        case G_TILL: case G_HARVEST: case G_MINE: case G_CHAT: case G_SHRINE: case G_FEAST: case G_FIGHT:
        case G_DANCE: case G_PLAY: case G_SNOW: case G_STORY: case G_QUARREL: case G_CLEAR: case G_FEEDPEN: case G_DOUSE:
        case G_WEDDING: case G_TRADE:
            acting = true;
            break;
        default: break;
        }
    bool fr = walking ? ((frame + (uint32_t)i) / 2) & 1 : false;
    int bob = acting && ((frame + (uint32_t)i) / 2 & 1) ? 1 : 0;
    int top, hx, hy, side = r->face ? -1 : 1;
    if (age < AGE_WORK) { /* 孩子：10x14 */
        const char *const *k = r->sex ? (fr ? SPR_HUGEWOMAN_B : SPR_HUGEWOMAN_A) : (fr ? SPR_HUGEMAN_B : SPR_HUGEMAN_A);
        MSPR(x, y + bob, k, 10, 14, r->face);
        top = y;
        hx = r->face ? x - 1 : x + 10;
        hy = y + 9;
    } else { /* 成年人：12x17 */
        const char *const *sp = r->sex ? (fr ? SPR_GIANTWOMAN_B : SPR_GIANTWOMAN_A) : (fr ? SPR_GIANTMAN_B : SPR_GIANTMAN_A);
        MSPR(x - 1, y - 3 + bob, sp, 12, 17, r->face);
        top = y - 3;
        hx = r->face ? x - 2 : x + 11;
        hy = y + 7;
        if (age >= AGE_OLD && r->goal != G_DANCE) { /* 老人拄着拐杖 */
            int cx = r->face ? x - 2 : x + 11;
            MRECT(cx, y + 6, 1, 8, pal['t']);
            mpx(cx + (r->face ? 1 : -1), y + 6, pal['t']);
        }
    }
    int mid = x + 5; /* 头顶正中 */

    if (r->state == S_ACT) {
        switch (r->goal) {
        case G_PRAY: /* 祈祷时头顶的微光 */
            if ((frame / 3 + (uint32_t)i) & 1) mpx(mid - 2, top - 3, pal['y']), mpx(mid, top - 2, pal['y']), mpx(mid + 2, top - 3, pal['y']);
            else mpx(mid, top - 4, pal['w']);
            break;
        case G_COWER: /* 发抖时的汗珠 */
            mpx(r->face ? x : x + 9, top + 3 + (int)(frame / 2 & 1), pal['q']);
            break;
        case G_DANCE: /* 跳舞：双手轮流举起，左右扭动，头顶飘音符 */
            if (((frame / 3) + (uint32_t)i) & 1) mpx(x, top + 3, pal['s']), mpx(x - 1, top + 2, pal['s']);
            else mpx(x + 9, top + 3, pal['s']), mpx(x + 10, top + 2, pal['s']);
            if ((frame / 6 + (uint32_t)i) % 4 == 0) MSPR(mid + 3, top - 7, SPR_NOTE, 4, 5, false);
            break;
        case G_STORY: /* 讲故事：比划着手，头顶一个大对话泡 */
            mpx(hx, hy - 3 - bob, pal['s']);
            MRECT(mid - 1, top - 8, 8, 5, pal['w']);
            mpx(mid, top - 3, pal['w']);
            for (int k = 0; k < 3; k++) mpx(mid + 1 + k * 2, top - 6, (frame / 4 + (uint32_t)k) & 1 ? pal['k'] : pal['w']);
            break;
        case G_LISTEN: /* 听入了迷：头顶问号一样的小点 */
            if ((frame / 5 + (uint32_t)i) & 1) mpx(mid, top - 3, pal['y']), mpx(mid + 1, top - 4, pal['y']);
            break;
        case G_GAZE: { /* 望着天，头顶的星星一闪一闪 */
            int ph = (int)((frame / 3 + (uint32_t)i) % 6);
            if (ph < 3) mpx(mid - 3 + ph * 3, top - 5 - (ph & 1), pal['y']);
            break;
        }
        case G_DATE: case G_GIFT: /* 冒爱心 */
            if ((frame / 4 + (uint32_t)i) % 3 != 2) MSPR(mid - 2, top - 6 - (int)((frame / 4) & 1), SPR_HEART, 5, 4, false);
            break;
        case G_SNOW: { /* 扔雪球：一个白点划着弧线飞向对方 */
            const resident_t *o = &w->res[r->tgt % MAX_RES];
            if (!o->alive || (i & 1) != (int)((frame / 8) & 1)) break; /* 两人轮流扔 */
            int ox, oy, ph = (int)(frame % 8);
            res_pos(o, &ox, &oy);
            int bx = hx + (ox + 5 - hx) * ph / 8, by = hy - 2 + (oy + 4 - hy) * ph / 8 - (ph * (8 - ph)) / 3;
            MRECT(bx, by, 2, 2, pal['w']);
            mpx(hx, hy - 2, pal['s']);
            break;
        }
        case G_FEAST: /* 举起双手 */
            mpx(x, top + 5 - bob * 2, pal['s']), mpx(x + 9, top + 6 - bob * 2, pal['s']);
            break;
        case G_QUARREL: { /* 吵架：头顶冒火，手臂乱挥 */
            uint16_t c = pal['R'];
            int ox = mid + 2 + (int)(frame & 1);
            mpx(ox, top - 5, c), mpx(ox + 2, top - 5, c), mpx(ox + 1, top - 4, c), mpx(ox, top - 3, c), mpx(ox + 2, top - 3, c);
            mpx(hx, hy - 3 - bob * 2, pal['s']);
            break;
        }
        case G_MOURN: /* 送葬：低头，落泪 */
            if ((frame / 4 + (uint32_t)i) % 3 == 0) mpx(r->face ? x + 2 : x + 7, top + 6 + (int)(frame / 2 % 3), pal['q']);
            break;
        case G_WEDDING: { /* 婚礼：新人戴花冠、冒爱心；客人拍手 */
            bool star = w->cer == CER_WEDDING && ((int)i == w->cer_a || (int)i == w->cer_b);
            if (star) {
                for (int k = 0; k < 5; k++) mpx(x + 2 + k * 2 - (k > 2), top - 1, k & 1 ? pal['p'] : pal['w']);
                if ((frame / 5 + (uint32_t)i) % 3 == 0) MSPR(mid - 2, top - 7, SPR_HEART, 5, 4, false);
            } else if ((frame / 2 + (uint32_t)i) & 1) {
                mpx(mid - 1, top + 7 - bob, pal['s']), mpx(mid + 1, top + 7 - bob, pal['s']);
            }
            break;
        }
        case G_DOUSE: { /* 泼水：一道水花飞向火 */
            int ph = (int)(frame % 6);
            for (int k = 0; k < 3; k++) mpx(hx + side * (ph * 2 + k), hy - 4 + ph - k, pal['q']);
            break;
        }
        case G_TRADE: /* 交易：手里比划，头顶一枚金币 */
            if ((frame / 4 + (uint32_t)i) & 1) MRECT(mid, top - 5, 3, 3, pal['y']), mpx(mid + 1, top - 4, pal['O']);
            break;
        case G_CHOP: case G_BUILD: case G_UPGRADE: case G_REPAIR: case G_MINE: case G_TILL: case G_SHRINE: case G_CLEAR:
        case G_FIGHT: { /* 手里的工具：石头、青铜、铁 */
            static const uint16_t HEAD[ERA_COUNT] = {RGB(150, 150, 156), RGB(214, 150, 70), RGB(222, 226, 236), RGB(236, 240, 250)};
            uint16_t tool = HEAD[w->era % ERA_COUNT];
            mpx(hx, hy - 3 + bob * 3, tool);
            mpx(hx + side, hy - 3 + bob * 3, tool);
            mpx(hx + side, hy - 2 + bob * 3, tool);
            for (int k = -2; k <= 0; k++) mpx(hx, hy + k + bob * 3, pal['t']);
            break;
        }
        case G_FISH: /* 鱼竿和浮漂 */
            for (int k = 0; k < 6; k++) mpx(hx + side * k, hy - k, pal['t']);
            mpx(hx + side * 6, hy - 3 + (int)(frame / 4 & 1), pal['R']);
            break;
        case G_EAT_CAMP: MRECT(hx, hy - 1, 1, 2, pal['R']); break;
        case G_CHAT: /* 头顶的对话泡 */
            if ((frame / 4 + (uint32_t)i) & 1) MRECT(mid + 2, top - 6, 5, 4, pal['w']), mpx(mid + 2, top - 2, pal['w']);
            break;
        case G_HELP: case G_TEND:
            mpx(mid - 1, top - 4, pal['r']), mpx(mid + 1, top - 4, pal['r']), MRECT(mid - 1, top - 3, 3, 1, pal['r']), mpx(mid, top - 2, pal['r']);
            break;
        default: break;
        }
    }
    if (r->carry == C_WATER) { /* 一桶水 */
        MRECT(hx - (side < 0), hy - 1, 2, 3, pal['T']);
        mpx(hx - (side < 0), hy - 1, pal['q']), mpx(hx - (side < 0) + 1, hy - 1, pal['q']);
    } else if (r->carry == C_GOODS) { /* 一包要换的货 */
        MRECT(mid - 3, top - 3, 6, 3, pal['n']);
        MRECT(mid - 1, top - 3, 1, 3, pal['y']);
    }
    if (r->carry == C_FLOWER) { /* 手里攥着一朵野花 */
        mpx(hx, hy, pal['S']), mpx(hx, hy - 1, pal['S']);
        mpx(hx, hy - 3, pal['p']), mpx(hx - 1, hy - 2, pal['p']), mpx(hx + 1, hy - 2, pal['p']), mpx(hx, hy - 2, pal['y']);
    }
    if (r->carry_n) { /* 头顶扛着的东西 */
        uint16_t cc = r->carry == C_WOOD ? pal['t'] : r->carry == C_STONE ? pal['a'] : pal['R'];
        MRECT(mid - 3, top - 3, 6, 3, cc);
        MRECT(mid - 2, top - 3, 2, 1, r->carry == C_WOOD ? pal['T'] : r->carry == C_STONE ? pal['l'] : pal['y']);
    } else if (r->sick) { /* 病人头顶冒绿点 */
        uint16_t gcol = RGB(120, 230, 90);
        mpx(mid - 2, top - 2 - (int)(frame / 3 & 1), gcol);
        mpx(mid + 2, top - 3 + (int)(frame / 3 & 1), gcol);
    } else if (r->goal == G_FLEE && (frame & 2)) { /* 逃命时的惊叹号 */
        MRECT(mid - 1, top - 7, 2, 4, pal['R']);
        MRECT(mid - 1, top - 2, 2, 1, pal['R']);
    } else if (r->state != S_ACT || r->goal == G_CHOP || r->goal == G_MINE || r->goal == G_TILL || r->goal == G_HARVEST ||
               r->goal == G_BERRY || r->goal == G_REST || r->goal == G_WANDER) {
        emote(w, i, mid, top, frame);
    }
    /* 路上碰见熟人：挥挥手 */
    if (walking)
        for (int j = 0; j < MAX_RES; j++) {
            const resident_t *o = &w->res[j];
            if (j == i || !o->alive || o->away || o->state != S_MOVE || abs(o->tx - r->tx) + abs(o->ty - r->ty) != 1) continue;
            if (w->rel[i][j] < 20 || w->feud[i][j] >= 40) continue;
            if (((frame / 3) & 1)) mpx(hx, hy - 6, pal['s']), mpx(hx, hy - 5, pal['s']);
            else mpx(hx + side, hy - 6, pal['s']), mpx(hx, hy - 5, pal['s']);
            break;
        }
}

static void draw_animal(const animal_t *a, int k, uint32_t frame)
{
    int off = a->step;
    if (off) off += (a->kind == A_WOLF || a->kind == A_DOG ? 2 : 1) * s_sub / 256;
    if (off > TILE) off = TILE;
    int x = a->tx * TILE + (a->nx - a->tx) * off;
    int y = a->ty * TILE + (a->ny - a->ty) * off + 1;
    bool fr = a->step ? ((frame + (uint32_t)k) / 2) & 1 : false;
    if (a->held && (frame & 1)) x += 1; /* 被缠住时抖动 */
    shadow(x + 1, y + 5, a->kind == A_RABBIT ? 5 : 7);
    if (a->kind == A_DOG) {
        MSPR(x, y, fr || ((frame / 3 + (uint32_t)k) & 1) ? SPR_DOG_B : SPR_DOG_A, 8, 6, a->face);
        return;
    }
    if (a->kind == A_RABBIT) {
        MSPR(x + 1, y + 1 - (fr ? 1 : 0), fr ? SPR_RABBIT_B : SPR_RABBIT_A, 6, 5, a->face);
    } else if (a->kind == A_WOLF) {
        uint16_t w0 = pal['W'], v0 = pal['V'], k0 = pal['k'];
        if (a->fierce) { /* 恶狼：一身黑毛，眼睛发红 */
            pal['W'] = RGB(64, 58, 72);
            pal['V'] = RGB(34, 30, 42);
            pal['k'] = RGB(255, 60, 40);
        }
        MSPR(x, y, fr ? SPR_WOLF_B : SPR_WOLF_A, 8, 6, a->face);
        pal['W'] = w0, pal['V'] = v0, pal['k'] = k0;
    } else {
        MSPR(x, y, fr ? SPR_BOAR_B : SPR_BOAR_A, 8, 6, a->face);
    }
}

static int night_level(const world_t *w)
{
    static const uint8_t LV[24] = {6, 6, 6, 6, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 5, 6, 6};
    static const uint8_t DIM[W_COUNT] = {0, 1, 1, 3, 3, 0, 1}; /* 阴雨天整体暗一些 */
    int l = LV[sim_hour(w)] + DIM[w->weather % W_COUNT];
    if (w->sky == SKY_ECLIPSE) l = 6; /* 日食：白天黑了下来 */
    if (w->sky == SKY_METEOR && l < 4) l = 4; /* 流星雨要在暗处才看得清 */
    return l > 7 ? 7 : l;
}

/* 夜色与阴天：整体压暗偏蓝，篝火周围留一圈暖光 */
static void shade_night(const world_t *w, int level)
{
    if (!level) return;
    int k[3][3];
    for (int z = 0; z < 3; z++) {
        int l = level - z * 3;
        if (l < 0) l = 0;
        k[z][0] = 32 - l * 3;
        k[z][1] = 32 - l * 5 / 2;
        k[z][2] = 32 - l;
    }
    int fx = w->camp_x * TILE + 4, fy = w->camp_y * TILE + 4;
    for (int y = 0; y < MAPV_H; y++) {
        uint16_t *p = fb_map + y * SCR_W;
        int dy = y - fy;
        bool near = w->fire_lit && dy > -30 && dy < 30;
        for (int x = 0; x < SCR_W; x++) {
            int z = 0;
            if (near) {
                int dx = x - fx, d2 = dx * dx + dy * dy;
                if (d2 < 16 * 16) z = 2;
                else if (d2 < 28 * 28) z = 1;
            }
            uint16_t c = p[x];
            int r = (c >> 11) * k[z][0] >> 5, g = ((c >> 5) & 63) * k[z][1] >> 5, b = (c & 31) * k[z][2] >> 5;
            p[x] = (uint16_t)(r << 11 | g << 5 | b);
        }
    }
}

/* 雨丝、雪花、风线 */
static void draw_weather(const world_t *w, uint32_t frame)
{
    int wt = w->weather;
    if (wt == W_RAIN && s_season == SEASON_WINTER) { /* 冬天的小雨是小雪 */
        for (int i = 0; i < 60; i++) {
            uint32_t h = thash(i, 99);
            int x = (int)((h + frame + (frame / 5 + (uint32_t)i) % 3) % SCR_W), y = (int)(((h >> 9) + frame * (1 + (i % 2))) % MAPV_H);
            mpx(x, y, pal['w']);
        }
        return;
    }
    if (wt == W_RAIN || wt == W_STORM || wt == W_THUNDER) {
        int n = wt == W_RAIN ? 70 : 170, len = wt == W_RAIN ? 3 : 5;
        uint16_t c = RGB(170, 206, 240);
        for (int i = 0; i < n; i++) {
            uint32_t h = thash(i, 77);
            int x = (int)((h + frame * 3) % (SCR_W + 40)) - 20, y = (int)(((h >> 9) + frame * 9) % (MAPV_H + 16)) - 8;
            for (int k = 0; k < len; k++) mpx(x - k / 2, y + k, c);
        }
    } else if (wt == W_SNOW) {
        for (int i = 0; i < 150; i++) {
            uint32_t h = thash(i, 99);
            int x = (int)((h + frame * (1 + (i & 1)) + (frame / 5 + (uint32_t)i) % 3) % SCR_W);
            int y = (int)(((h >> 9) + frame * (2 + (i % 3))) % MAPV_H);
            mpx(x, y, pal['w']);
            if (i & 4) mpx(x + 1, y, pal['w']);
        }
    } else if (wt == W_WIND) {
        for (int i = 0; i < 16; i++) {
            uint32_t h = thash(i, 55);
            int x = (int)((h + frame * 13) % (SCR_W + 60)) - 30, y = (int)((h >> 9) % MAPV_H);
            for (int k = 0; k < 12 + (int)(h & 7); k++) mpx(x + k, y + ((k / 5) & 1), RGB(226, 236, 240));
        }
    }
    if (wt == W_THUNDER && thash((int)(frame / 2), 7) % 23 == 0) /* 远处的闪电把天照亮一瞬 */
        for (int p = 0; p < SCR_W * MAPV_H; p++) fb_map[p] |= 0x4208;
}

static void draw_fx(uint32_t frame)
{
    for (int i = 0; i < FX_N; i++) {
        const sim_fx_t *f = &g_fx[i];
        if (!f->t) continue;
        int cx = f->x * TILE + 4, cy = f->y * TILE + 4;
        if (f->type == FX_HEAL) {
            uint16_t c = RGB(140, 255, 150);
            int yy = cy - 6 - (14 - f->t) / 2; /* 上升的绿色十字 */
            mpx(cx, yy, c), mpx(cx - 1, yy, c), mpx(cx + 1, yy, c), mpx(cx, yy - 1, c), mpx(cx, yy + 1, c);
        } else if (f->type == FX_SPLASH) { /* 浇灭的火苗：水花和白烟 */
            int up = 8 - f->t;
            for (int k = 0; k < 6; k++) {
                int dx = (k - 3) * (1 + up / 2), dy = -up - (k & 1) * 2 + (up * up) / 8;
                mpx(cx + dx, cy + dy, k & 1 ? pal['q'] : pal['w']);
            }
            mpx(cx, cy - 4 - up, pal['l']), mpx(cx + 1, cy - 5 - up, pal['l']);
        } else if (f->type == FX_HEART) { /* 飘起的爱心 */
            int up = 12 - f->t;
            MSPR(cx - 2, cy - 14 - up, SPR_HEART, 5, 4, false);
        } else if (f->type == FX_NOTE) { /* 欢庆时飘起的音符 */
            int up = 14 - f->t;
            MSPR(cx - 2 + (up / 3 & 1), cy - 14 - up, SPR_NOTE, 4, 5, false);
        } else if (f->type == FX_SPARK) { /* 神迹的光环：两圈向外扩散的金光 */
            static const int8_t D[16][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4}, {3, 3}, {-3, 3}, {3, -3}, {-3, -3},
                                            {4, 2}, {-4, 2}, {4, -2}, {-4, -2}, {2, 4}, {-2, 4}, {2, -4}, {-2, -4}};
            int rad = 22 - f->t;
            if (rad < 2) rad = 2;
            uint16_t c = (frame & 1) ? pal['y'] : pal['w'];
            for (int d = 0; d < 16; d++) {
                int px = cx + D[d][0] * rad / 4, py = cy + D[d][1] * rad / 4;
                mpx(px, py, c), mpx(px + 1, py, c), mpx(px, py + 1, c);
                mpx(cx + D[d][0] * rad / 8, cy + D[d][1] * rad / 8, pal['y']);
            }
            MRECT(cx - 1, cy - 1, 3, 3, pal['w']);
        } else if (f->type == FX_BOLT) {
            if (f->t > 6) { /* 闪电本体 */
                int x = cx;
                for (int y = 0; y <= cy; y++) {
                    if (y % 6 == 5) x += (int)(thash(y, f->t) % 7) - 3;
                    if (cy - y < 8) x += (x < cx) - (x > cx);
                    mpx(x, y, pal['w']);
                    mpx(x + 1, y, pal['y']);
                }
            }
            for (int n = 0; n < 14; n++) { /* 火星 */
                uint32_t h = thash(n, (int)frame);
                mpx(cx + (int)(h % 33) - 16, cy + (int)(h >> 8) % 33 - 16, (h & 0x10000) ? pal['o'] : pal['y']);
            }
        }
    }
}

/* 火山口上方的烟柱 */
static void draw_smoke(const world_t *w, uint32_t frame)
{
    if (!w->volcano_h) return;
    int cx = w->crater % MAP_W * TILE + 4, cy = w->crater / MAP_W * TILE;
    for (int n = 0; n < 40; n++) {
        uint32_t h = thash(n, 31);
        int up = (int)((h + frame * 2) % 44);
        int x = cx + (int)(h >> 8) % (5 + up / 3) - (2 + up / 6), y = cy - up;
        uint16_t c = up < 8 ? pal['o'] : (h & 0x10000) ? pal['A'] : pal['e'];
        mpx(x, y, c);
        mpx(x + 1, y, c);
    }
}

/* ---------- 氛围：海鸥、蝴蝶、萤火虫、跃出水面的鱼、云影、灯火、晚霞、天象 ---------- */

/* 一片暖光：以 (cx,cy) 为中心、半径 rad 像素，越靠中心越亮 */
static void glow(int cx, int cy, int rad, int str)
{
    for (int y = cy - rad; y <= cy + rad; y++) {
        if (y < 0 || y >= MAPV_H) continue;
        uint16_t *p = fb_map + y * SCR_W;
        for (int x = cx - rad; x <= cx + rad; x++) {
            if (x < 0 || x >= SCR_W) continue;
            int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d2 > rad * rad) continue;
            int k = str * (rad * rad - d2) / (rad * rad);
            uint16_t c = p[x];
            int r = (c >> 11) + k, g = ((c >> 5) & 63) + k * 3 / 2, b = (c & 31) + k / 4;
            p[x] = (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
        }
    }
}

/* 夜里亮灯：有人在家的屋子窗口透出暖光，瞭望塔和神像也点着火 */
static void night_lights(const world_t *w, int level)
{
    if (level < 3) return;
    int str = level >= 5 ? 6 : 3;
    for (int i = 0; i < MAP_N; i++) {
        int o = OBJ(w->tiles[i]), x = i % MAP_W * TILE, y = i / MAP_W * TILE;
        if (o == O_HOUSE1 || o == O_HOUSE2) {
            bool home = false;
            for (int k = 0; k < MAX_RES && !home; k++)
                home = w->res[k].alive && !w->res[k].away && w->res[k].home == i && sim_in_house(w, &w->res[k]);
            if (!home) continue;
            glow(x + 4, y + 3, 9, str);
            mpx(x + 1, y + 3, C_GOLD), mpx(x + 6, y + 3, C_GOLD); /* 窗口 */
        } else if (o == O_LAMP) {
            glow(x + 5, y - 3, 11, str);
        } else if (o == O_FACTORY) {
            glow(x + 2, y + 2, 9, str / 2 + 1);
        } else if (o == O_SKYSCRAPER) {
            glow(x + 4, y - 14, 16, str / 2 + 1);
        } else if (o == O_TOWER) {
            glow(x + 4, y - 8, 8, str);
        } else if (o == O_STATUE) {
            glow(x + 4, y - 4, 14, str);
        }
    }
    if (w->ship_h) glow(w->ship_x * TILE + 4, w->ship_y * TILE - 2, 8, str); /* 商船的桅灯 */
}

/* 晨昏：太阳低的时候，世界染上一层橘红 */
static void dusk_tint(const world_t *w)
{
    int h = sim_hour(w), s;
    if (h == 17 || h == 6) s = 1;
    else if (h == 18 || h == 5) s = 2;
    else if (h == 19) s = 3;
    else return;
    if (w->weather >= W_RAIN && w->weather != W_WIND) return;
    for (int p = 0; p < SCR_W * MAPV_H; p++) {
        uint16_t c = fb_map[p];
        int r = (c >> 11) + s, g = (c >> 5) & 63, b = c & 31;
        g -= g * s / 24;
        b -= b * s / 8;
        fb_map[p] = (uint16_t)((r > 31 ? 31 : r) << 11 | g << 5 | b);
    }
}

/* 云影：两片大大的影子慢慢飘过 */
static void cloud_shadows(const world_t *w, uint32_t frame)
{
    if (w->weather != W_SUNNY && w->weather != W_CLOUDY) return;
    int n = w->weather == W_CLOUDY ? 3 : 2;
    for (int c = 0; c < n; c++) {
        uint32_t h = thash(c, 404);
        int rx = 34 + (int)(h % 20), ry = 14 + (int)(h >> 8) % 8;
        int cx = (int)((frame / 3 + h) % (SCR_W + rx * 2)) - rx, cy = 20 + (int)((h >> 12) % (MAPV_H - 40));
        for (int y = cy - ry; y <= cy + ry; y++) {
            if (y < 0 || y >= MAPV_H) continue;
            int dy = y - cy, span = rx * rx - dy * dy * rx * rx / (ry * ry);
            if (span <= 0) continue;
            int dx = 0;
            while ((dx + 1) * (dx + 1) <= span) dx++;
            dx += (int)(thash(y, c) % 3); /* 毛边 */
            for (int x = cx - dx; x <= cx + dx; x++)
                if (x >= 0 && x < SCR_W && ((x + y) & 1)) fb_map[y * SCR_W + x] = darker(fb_map[y * SCR_W + x]);
        }
    }
}

static void ambient_life(const world_t *w, uint32_t frame)
{
    int h = sim_hour(w);
    bool day = h >= 6 && h < 19, calm = w->weather == W_SUNNY || w->weather == W_CLOUDY;
    /* 海鸥 */
    if (day && w->weather != W_SNOW && w->weather != W_STORM)
        for (int g = 0; g < 3; g++) {
            uint32_t hs = thash(g, 77);
            int x = (int)((frame * (2 + g % 2) + hs) % (SCR_W + 60)) - 30;
            int y = 8 + (int)(hs >> 8) % (MAPV_H - 30) + (int)((frame / 5 + hs) % 4 == 0);
            bool up = ((frame / 3 + hs) & 1);
            mpx(x, y, pal['w']), mpx(x + 4, y, pal['w']);
            mpx(x + 1, y + (up ? -1 : 1), pal['w']), mpx(x + 3, y + (up ? -1 : 1), pal['w']);
            mpx(x + 2, y, pal['l']);
            mpx(x + 2, y + 12, darker(darker(fb_map[(y + 12 < MAPV_H ? y + 12 : y) * SCR_W + (x + 2 < 0 ? 0 : x + 2 >= SCR_W ? SCR_W - 1 : x + 2)]))); /* 影子 */
        }
    /* 蝴蝶：春夏白天，在花丛边上下翻飞 */
    if (day && calm && (s_season == SEASON_SPRING || s_season == SEASON_SUMMER)) {
        int n = 0;
        for (int i = 0; i < MAP_N && n < 6; i++) {
            if (OBJ(w->tiles[i]) != O_FLOWER || thash(i, 5) % 4) continue;
            n++;
            uint32_t hs = thash(i, 9);
            int ph = (int)((frame + hs) % 32);
            static const int8_t OX[32] = {0, 1, 2, 3, 4, 4, 5, 5, 5, 4, 4, 3, 2, 1, 0, -1, -2, -2, -3, -3, -3, -2, -2, -1, 0, 0, 1, 1, 0, 0, -1, -1};
            int x = i % MAP_W * TILE + 3 + OX[ph], y = i / MAP_W * TILE - 2 + OX[(ph + 8) % 32] / 2;
            uint16_t c = (hs & 1) ? pal['y'] : (hs & 2) ? pal['p'] : pal['w'];
            if ((frame + hs) & 1) mpx(x - 1, y, c), mpx(x + 1, y, c);
            else mpx(x - 1, y - 1, c), mpx(x + 1, y - 1, c);
            mpx(x, y, pal['e']);
        }
    }
    /* 跃出水面的鱼 */
    if (day && w->fish > 4) {
        uint32_t slot = frame / 24, ph = frame % 24;
        uint32_t hs = thash((int)slot, 13);
        int tx = (int)(hs % MAP_W), ty = (int)((hs >> 8) % MAP_H);
        if (ph < 12 && terr_at(w, tx, ty) == T_SEA) {
            int x = tx * TILE + (int)ph / 2, y = ty * TILE + 4 - (int)(ph * (12 - ph)) / 6;
            mpx(x, y, pal['l']), mpx(x + 1, y, pal['a']), mpx(x - 1, y + 1, pal['l']);
            if (ph < 2 || ph > 10) mpx(x - 1, ty * TILE + 5, pal['w']), mpx(x + 2, ty * TILE + 5, pal['w']);
        }
    }
    /* 海面的粼光 */
    if (day && calm)
        for (int k = 0; k < 14; k++) {
            uint32_t hs = thash(k, (int)(frame / 4));
            int x = (int)(hs % SCR_W), y = (int)((hs >> 9) % MAPV_H);
            if (terr_at(w, x / TILE, y / TILE) == T_SEA) mpx(x, y, pal['w']), mpx(x + 1, y, C_SEA_HI);
        }
}

/* 路上的小车（电气时代以后）：沿着路一格格开，岔路口随便拐 */
static struct { int16_t at, to, prev; uint8_t prog, alive; } s_car[3];
static void cars(const world_t *w, uint32_t frame)
{
    if (w->era < ERA_ELECTRIC || w->roads < 6) return;
    static const uint16_t COL[3] = {RGB(220, 60, 60), RGB(60, 120, 220), RGB(240, 200, 60)};
    int ncar = w->era >= ERA_MODERN ? 3 : 1;
    for (int k = 0; k < ncar; k++) {
        if (!s_car[k].alive || TERR(w->tiles[s_car[k].at]) != T_ROAD || TERR(w->tiles[s_car[k].to]) != T_ROAD) {
            int start = (int)(thash(k, (int)(frame / 50)) % MAP_N), found = -1;
            for (int i = 0; i < MAP_N && found < 0; i++)
                if (TERR(w->tiles[(start + i) % MAP_N]) == T_ROAD) found = (start + i) % MAP_N;
            if (found < 0) return;
            s_car[k].at = s_car[k].to = s_car[k].prev = (int16_t)found;
            s_car[k].prog = 8;
            s_car[k].alive = 1;
        }
        if (++s_car[k].prog >= 8) { /* 到了下一格：挑下一个方向，不走回头路 */
            s_car[k].prev = s_car[k].at;
            s_car[k].at = s_car[k].to;
            s_car[k].prog = 0;
            int ax = s_car[k].at % MAP_W, ay = s_car[k].at / MAP_W, opts[4], n = 0, back = -1;
            static const int8_t DXS[4] = {1, -1, 0, 0}, DYS[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; d++) {
                int nx = ax + DXS[d], ny = ay + DYS[d];
                if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H || TERR(w->tiles[ny * MAP_W + nx]) != T_ROAD) continue;
                if (ny * MAP_W + nx == s_car[k].prev) back = ny * MAP_W + nx;
                else opts[n++] = ny * MAP_W + nx;
            }
            s_car[k].to = (int16_t)(n ? opts[thash(k, (int)frame) % (uint32_t)n] : back >= 0 ? back : s_car[k].at);
        }
        int ax = s_car[k].at % MAP_W * TILE, ay = s_car[k].at / MAP_W * TILE;
        int bx = s_car[k].to % MAP_W * TILE, by = s_car[k].to / MAP_W * TILE;
        int x = ax + (bx - ax) * s_car[k].prog / 8, y = ay + (by - ay) * s_car[k].prog / 8;
        bool horiz = ay == by;
        int cw = horiz ? 6 : 4, ch = horiz ? 4 : 6;
        x += (TILE - cw) / 2, y += (TILE - ch) / 2;
        MRECT(x, y + ch, cw, 1, darker(darker(fb_map[(y + ch < MAPV_H ? y + ch : y) * SCR_W + x]))); /* 影子 */
        MRECT(x, y, cw, ch, COL[k]);
        if (horiz) MRECT(x + 1 + (bx > ax), y + 1, 3, 1, RGB(170, 210, 240));
        else MRECT(x + 1, y + 1 + (by > ay), 1, 3, RGB(170, 210, 240));
        if (s_night) mpx(horiz ? (bx > ax ? x + cw : x - 1) : x + 1, horiz ? y + 1 : (by > ay ? y + ch : y - 1), RGB(255, 240, 160));
    }
}

/* 萤火虫：夏夜草丛和树林边一闪一闪（画在夜色之上） */
static void fireflies(const world_t *w, uint32_t frame)
{
    int h = sim_hour(w);
    if (s_season != SEASON_SUMMER || !(h >= 20 || h < 4) || w->weather >= W_RAIN) return;
    for (int k = 0; k < 24; k++) {
        uint32_t hs = thash(k, 21);
        int tx = (int)(hs % MAP_W), ty = (int)((hs >> 8) % MAP_H);
        if (terr_at(w, tx, ty) != T_GRASS) continue;
        if ((frame / 2 + hs) % 5 == 0) continue;
        int x = tx * TILE + (int)((frame / 4 + hs) % 7), y = ty * TILE + (int)((frame / 5 + (hs >> 4)) % 6);
        mpx(x, y, RGB(220, 255, 120));
        if ((frame / 3 + hs) % 3 == 0) mpx(x + 1, y, RGB(140, 200, 70));
    }
}

/* 天象 */
static void sky_wonders(const world_t *w, uint32_t frame)
{
    if (w->sky == SKY_METEOR) {
        for (int k = 0; k < 6; k++) {
            uint32_t slot = (frame + (uint32_t)k * 7) / 10, ph = (frame + (uint32_t)k * 7) % 10;
            uint32_t hs = thash((int)slot, k);
            int x0 = (int)(hs % SCR_W) + 40, y0 = (int)((hs >> 8) % 80);
            int x = x0 - (int)ph * 8, y = y0 + (int)ph * 4;
            for (int t = 0; t < 10; t++) mpx(x + t * 2, y - t, t < 2 ? pal['w'] : t < 5 ? pal['y'] : pal['l']);
        }
    } else if (w->sky == SKY_RAINBOW) { /* 一道彩虹横跨全岛 */
        static const uint16_t BAND[7] = {RGB(240, 80, 80), RGB(250, 160, 60), RGB(250, 230, 90), RGB(110, 210, 110),
                                         RGB(90, 170, 240), RGB(110, 110, 220), RGB(170, 110, 220)};
        int cx = SCR_W / 2, cy = MAPV_H + 60, r0 = 150;
        for (int y = 0; y < MAPV_H; y++)
            for (int x = 0; x < SCR_W; x++) {
                int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (d2 < (r0 - 21) * (r0 - 21) || d2 >= r0 * r0) continue;
                int band = 0;
                while (band < 6 && d2 < (r0 - 3 * (band + 1)) * (r0 - 3 * (band + 1))) band++;
                uint16_t a = fb_map[y * SCR_W + x], b = BAND[band];
                fb_map[y * SCR_W + x] = (uint16_t)(((a >> 1) & 0x7BEF) + ((b >> 1) & 0x7BEF));
            }
    }
}

/* 新年夜：篝火上空的烟花 */
static void fireworks(const world_t *w, uint32_t frame)
{
    if (w->cer != CER_NEWYEAR || sim_hour(w) < 18) return;
    static const uint16_t COL[4] = {RGB(255, 120, 120), RGB(255, 230, 110), RGB(130, 220, 255), RGB(200, 140, 255)};
    for (int k = 0; k < 3; k++) {
        uint32_t slot = (frame + (uint32_t)k * 9) / 18, ph = (frame + (uint32_t)k * 9) % 18;
        uint32_t hs = thash((int)slot, k + 50);
        int cx = w->camp_x * TILE + 4 + (int)(hs % 60) - 30, cy = w->camp_y * TILE - 30 - (int)((hs >> 8) % 30);
        if (cy < 6) cy = 6;
        uint16_t c = COL[(hs >> 4) & 3];
        if (ph < 5) { /* 升空 */
            mpx(cx, cy + 30 - (int)ph * 6, pal['y']);
            continue;
        }
        int r = (int)(ph - 5) * 3 / 2 + 2;
        static const int8_t D[12][2] = {{8, 0}, {-8, 0}, {0, 8}, {0, -8}, {6, 6}, {-6, 6}, {6, -6}, {-6, -6},
                                        {7, 3}, {-7, 3}, {3, -7}, {-3, -7}};
        if (ph < 8) glow(cx, cy, 10, 4 - (int)ph / 2);
        for (int d = 0; d < 12; d++) {
            int x = cx + D[d][0] * r / 8, y = cy + D[d][1] * r / 8 + (int)(ph - 5) * (int)(ph - 5) / 12;
            if (ph < 14 || (frame & 1)) MRECT(x, y, 2, 2, ph < 9 ? pal['w'] : c), mpx(x - D[d][0] / 4, y - D[d][1] / 4, c);
        }
    }
}

/* 停在海上的商船 */
static void trade_ship(const world_t *w, uint32_t frame)
{
    if (!w->ship_h) return;
    int x = w->ship_x * TILE - 4, y = w->ship_y * TILE - 6 + (int)(frame / 6 % 2);
    uint16_t sail = pal['w'];
    pal['w'] = RGB(240, 226, 196); /* 远航的帆布有点旧 */
    MSPR(x, y, SPR_SHIP, 16, 13, w->ship_x < w->camp_x);
    pal['w'] = sail;
    for (int k = 0; k < 4; k++) mpx(x + 1 + k * 4 + (int)(frame / 4 % 2), y + 13, pal['w']); /* 船边的浪 */
}

#define MAPT(x, y, s, c) font_draw(fb_map, SCR_W, MAPV_H, x, y, s, c)

/* 故事：地图上铺开一页旧纸 */
#define C_PAPER RGB(244, 232, 204)
#define C_PAPER_D RGB(222, 204, 166)
#define C_INK RGB(70, 52, 40)
#define C_INK_T RGB(150, 70, 40)

static int story_pages(const char *t)
{
    int n = 1;
    for (; *t; t++) n += *t == '|';
    return n;
}
static const char *story_page_at(const char *t, int k)
{
    while (k > 0 && *t) {
        if (*t++ == '|') k--;
    }
    return t;
}

static void render_story(const world_t *w, const ui_t *ui)
{
    static char text[1200];
    char title[64];
    for (int p = 0; p < SCR_W * MAPV_H; p++) fb_map[p] = darker(darker(fb_map[p]));
    int x0 = 8, y0 = 6, x1 = 232, y1 = 202;
    MRECT(x0, y0, x1 - x0, y1 - y0, C_PAPER);
    MRECT(x0, y0, x1 - x0, 2, C_PAPER_D), MRECT(x0, y1 - 2, x1 - x0, 2, C_PAPER_D);
    MRECT(x0, y0, 2, y1 - y0, C_PAPER_D), MRECT(x1 - 2, y0, 2, y1 - y0, C_PAPER_D);
    for (int k = 0; k < 6; k++) /* 纸上的斑点 */
        mpx(x0 + 6 + (int)(thash(k, 3) % 200), y0 + 6 + (int)(thash(k, 4) % 180), C_PAPER_D);
    if (ui->story_res) snprintf(title, sizeof title, "%s", g_story_rtitle);
    else sim_story_title(w, title, sizeof title);
    int tw = font_width(title);
    MAPT(120 - tw / 2, y0 + 7, title, C_INK_T);
    MRECT(30, y0 + 22, 180, 1, C_PAPER_D);
    if (ui->story_res) {
        font_wrap(fb_map, SCR_W, MAPV_H, x0 + 10, y0 + 30, x1 - x0 - 20, 15, g_story_result, C_INK, 11);
        return;
    }
    sim_story_text(w, text, sizeof text);
    int pages = story_pages(text), pg = ui->story_page < pages ? ui->story_page : pages - 1;
    font_wrap(fb_map, SCR_W, MAPV_H, x0 + 10, y0 + 30, x1 - x0 - 20, 15, story_page_at(text, pg), C_INK, 11);
    if (pages > 1) {
        char buf[24];
        snprintf(buf, sizeof buf, "%d/%d", pg + 1, pages);
        MAPT(x1 - 10 - font_width(buf), y1 - 16, buf, C_PAPER_D);
    }
    if (pg < pages - 1 && ((ui->frame / 5) & 1)) /* 还有下一页 */
        for (int r = 0; r < 3; r++) MRECT(118 + r, y1 - 12 + r, 5 - r * 2, 1, C_INK_T);
}

static const char *fmt_person(const world_t *w, const person_t *p, char *buf, size_t n)
{
    const resident_t *r = sim_resident_by_uid(w, p->uid);
    if (r) snprintf(buf, n, "%d岁", sim_age(r));
    else if (p->age == 0xFF) snprintf(buf, n, "下落不明");
    else snprintf(buf, n, "已故 享年%d", p->age);
    return buf;
}

static void render_tree(const world_t *w, const ui_t *ui)
{
    char buf[96], tmp[40];
    MRECT(0, 0, SCR_W, MAPV_H, C_BG);
    MAPT(12, 5, "家谱", C_GOLD);
    snprintf(buf, sizeof buf, "在册%d人 已传%d代", sim_tree_count(), w->max_gen);
    MAPT(228 - font_width(buf), 5, buf, C_DIM);

    const person_t *p = sim_person(ui->tree_uid);
    if (!p) {
        MAPT(12, 40, "这个人的记录已经湮没", C_DIM);
        return;
    }
    const resident_t *r = sim_resident_by_uid(w, p->uid);
    MRECT(8, 22, 224, 46, C_PANEL);
    MRECT(8, 22, 3, 46, C_GOLD);
    int x = MAPT(16, 26, sim_name_of(p->sex, p->name), C_GOLD);
    if (p->gen == 1) snprintf(buf, sizeof buf, " %s 第1代 登岛先民", p->sex ? "女" : "男");
    else snprintf(buf, sizeof buf, " %s 第%d代 生于第%d天", p->sex ? "女" : "男", p->gen, p->born);
    MAPT(x, 26, buf, C_TEXT);
    if (r) snprintf(buf, sizeof buf, "在世 %d岁 %s", sim_age(r), sim_trait(r));
    else if (p->age == 0xFF) snprintf(buf, sizeof buf, "下落不明");
    else snprintf(buf, sizeof buf, "享年%d岁 死于%s", p->age, sim_cause_name(p->cause));
    MAPT(16, 39, buf, r ? C_TEXT : C_DIM);
    int living = 0, total = sim_descendants(p->uid, &living);
    if (!total) snprintf(buf, sizeof buf, "没有后代");
    else if (!living) snprintf(buf, sizeof buf, "后代%d人 血脉已断", total);
    else snprintf(buf, sizeof buf, "后代%d人 在世%d人", total, living);
    MAPT(16, 52, buf, (total && !living) ? C_RED : C_DIM);

    uint16_t rel[TREE_ROWS];
    uint8_t role[TREE_ROWS];
    int n = sim_relatives(w, p->uid, rel, role, TREE_ROWS);
    if (!n) MAPT(16, 80, "没有登记在册的亲属", C_DIM);
    for (int k = 0; k < n; k++) {
        const person_t *q = sim_person(rel[k]);
        int y = 74 + k * 13;
        bool cur = k == ui->tree_cur;
        if (cur) {
            MRECT(8, y - 1, 224, 13, C_PANEL);
            for (int t = 0; t < 5; t++) MRECT(10, y + 3 + t, (t < 3 ? t : 4 - t) + 1, 1, C_GOLD);
        }
        const char *tag = role[k] == 0 ? "父" : role[k] == 1 ? "母" : role[k] == 2 ? "偶" : q->sex ? "女" : "子";
        MAPT(18, y, tag, C_DIM);
        MAPT(36, y, sim_name_of(q->sex, q->name), cur ? C_GOLD : C_TEXT);
        MAPT(68, y, fmt_person(w, q, tmp, sizeof tmp), sim_resident_by_uid(w, q->uid) ? C_TEXT : C_DIM);
        snprintf(buf, sizeof buf, "第%d代", q->gen);
        MAPT(228 - font_width(buf), y, buf, C_DIM);
    }
}

/* 以 (cx,cy) 为中心把地图放大两倍。原地完成，不需要额外的缓冲：
 * 先把 120x104 的源窗口挪到左上角，再从右下往左上逐像素铺开。 */
static void zoom2x(int cx, int cy)
{
    int sx = cx - SCR_W / 4, sy = cy - MAPV_H / 4;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > SCR_W / 2) sx = SCR_W / 2;
    if (sy > MAPV_H / 2) sy = MAPV_H / 2;
    for (int r = 0; r < MAPV_H / 2; r++) memmove(fb_map + r * SCR_W, fb_map + (sy + r) * SCR_W + sx, (SCR_W / 2) * sizeof(uint16_t));
    for (int y = MAPV_H - 1; y >= 0; y--) {
        const uint16_t *src = fb_map + (y / 2) * SCR_W;
        uint16_t *dst = fb_map + y * SCR_W;
        for (int x = SCR_W - 1; x >= 0; x--) dst[x] = src[x / 2];
    }
}

void render_map(const world_t *w, const ui_t *ui)
{
    uint32_t frame = ui->frame;
    s_sub = ui->sub;
    s_season = sim_season(w);
    s_snowy = s_season == SEASON_WINTER || w->weather == W_SNOW;
    s_night = night_level(w) >= 3;
    const uint8_t (*lf)[3] = LEAF_RGB[s_snowy ? SEASON_WINTER : s_season]; /* 树叶随季节变色 */
    pal['G'] = RGB(lf[0][0], lf[0][1], lf[0][2]);
    pal['g'] = RGB(lf[1][0], lf[1][1], lf[1][2]);
    pal['D'] = RGB(lf[2][0], lf[2][1], lf[2][2]);
    if (ui->inspect == 4) { render_tree(w, ui); return; }
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) tile_base(w, tx, ty, frame);
    trade_ship(w, frame);
    /* 自上而下逐行画：先地物，再站在这一行的生物，下一行的树会自然压住它们 */
    for (int ty = 0; ty < MAP_H; ty++) {
        for (int tx = 0; tx < MAP_W; tx++) tile_obj(w, tx, ty, frame);
        for (int k = 0; k < MAX_ANI; k++)
            if (w->ani[k].alive && w->ani[k].ty == ty) draw_animal(&w->ani[k], k, frame);
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && !w->res[i].away && w->res[i].ty == ty) draw_resident(w, i, frame);
    }
    ambient_life(w, frame);
    cars(w, frame);
    cloud_shadows(w, frame);
    int nl = night_level(w);
    shade_night(w, nl);
    dusk_tint(w);
    night_lights(w, nl);
    fireflies(w, frame);
    draw_weather(w, frame);
    sky_wonders(w, frame);
    fireworks(w, frame);
    draw_smoke(w, frame);
    bool quake = false;
    for (int i = 0; i < FX_N; i++) {
        if (!g_fx[i].t) continue;
        if (g_fx[i].type == FX_QUAKE) quake = true;
        if (g_fx[i].type == FX_GLOW || g_fx[i].type == FX_DOOM) { /* 祝福时全岛泛金，降灾时天色发红 */
            int k = g_fx[i].t > 8 ? 8 : g_fx[i].t;
            bool glow = g_fx[i].type == FX_GLOW;
            for (int p = 0; p < SCR_W * MAPV_H; p++) {
                uint16_t c = fb_map[p];
                int r = (c >> 11) + k, g = (c >> 5) & 63, b = c & 31;
                if (glow) g += k, b -= k / 3;
                else r += k, g -= g * k / 20, b -= b * k / 16;
                if (r > 31) r = 31;
                if (g > 63) g = 63;
                if (b < 0) b = 0;
                fb_map[p] = (uint16_t)(r << 11 | g << 5 | b);
            }
        }
        if (g_fx[i].type == FX_BOLT && g_fx[i].t > 8) /* 闪电瞬间把整张地图照亮 */
            for (int p = 0; p < SCR_W * MAPV_H; p++) fb_map[p] |= 0x8410;
    }
    draw_fx(frame);
    if (ui->inspect == 7) { /* 读故事：地图不放大，铺上旧纸 */
        render_story(w, ui);
        return;
    }
    if ((ui->inspect == 1 || ui->inspect == 5) && w->res[ui->who].alive) { /* 被观察者头顶的箭头 */
        int x, y;
        res_pos(&w->res[ui->who], &x, &y);
        x += 3;
        y -= 7 + (int)(frame / 3 & 1);
        for (int r = 0; r < 3; r++)
            for (int c = r; c < 5 - r; c++) mpx(x + c, y + r, C_GOLD);
        mpx(x + 2, y + 3, C_GOLD);
    }
    if (quake) { /* 地震：整屏左右错位 */
        for (int y = 0; y < MAPV_H; y++) {
            int s = (int)(thash(y / 6, (int)frame) % 7) - 3;
            uint16_t *p = fb_map + y * SCR_W;
            if (s > 0) memmove(p + s, p, (size_t)(SCR_W - s) * 2);
            else if (s < 0) memmove(p, p - s, (size_t)(SCR_W + s) * 2);
        }
    }
    /* 镜头：观察某个人时跟着他拉近；设置里选了"近景"则对着营地 */
    if ((ui->inspect == 1 || ui->inspect == 5) && w->res[ui->who].alive) {
        int x, y;
        res_pos(&w->res[ui->who], &x, &y);
        zoom2x(x + 5, y + 8);
    } else if (w->zoom) {
        zoom2x(w->camp_x * TILE + 4, w->camp_y * TILE + 4);
    }
    /* 施放神力时地图上不再出字，施放了什么看底部的消息 */
}

/* ---------- 顶栏与底部面板 ---------- */

#define TOPT(x, y, s, c) font_draw(fb_top, SCR_W, TOP_H, x, y, s, c)
#define BOTT(x, y, s, c) font_draw(fb_bot, SCR_W, BOT_H, x, y, s, c)
#define BRECT(x, y, w, h, c) rect(fb_bot, SCR_W, BOT_H, x, y, w, h, c)

void render_top(const world_t *w, const ui_t *ui)
{
    (void)ui;
    char buf[40];
    rect(fb_top, SCR_W, TOP_H, 0, 0, SCR_W, TOP_H, C_BG);
    rect(fb_top, SCR_W, TOP_H, 0, TOP_H - 1, SCR_W, 1, C_LINE);
    int h = sim_hour(w), wt = w->weather;
    if (wt == W_RAIN && sim_season(w) == SEASON_WINTER) wt = W_SNOW; /* 小雪的图标 */
    spr(fb_top, SCR_W, TOP_H, 14, 8, (h >= 6 && h < 19) ? SPR_SUN : SPR_MOON, 8, 8, false);
    spr(fb_top, SCR_W, TOP_H, 24, 8, ICON_W[wt % W_COUNT], 8, 8, false);
    static const uint16_t SEASON_COL[4] = {RGB(150, 230, 140), RGB(255, 214, 92), RGB(244, 150, 70), RGB(190, 214, 255)};
    int x = w->weather == W_SUNNY ? 26 : 35;
    snprintf(buf, sizeof buf, "%d年", sim_year(w));
    x = TOPT(x, 6, buf, C_TEXT);
    x = TOPT(x, 6, sim_season_name(sim_season(w)), SEASON_COL[sim_season(w)]);
    snprintf(buf, sizeof buf, " %d时", h);
    TOPT(x, 6, buf, C_TEXT);
    /* 中间：暂停 / 倍速，平时显示时代 */
    if (w->paused) snprintf(buf, sizeof buf, "暂停");
    else if (sim_speed(w) > 1) snprintf(buf, sizeof buf, "%d倍", sim_speed(w));
    else snprintf(buf, sizeof buf, "%s", sim_era_name(w->era));
    TOPT(132, 6, buf, (w->paused || sim_speed(w) > 1) ? C_GOLD : C_DIM);
    snprintf(buf, sizeof buf, "%d人", sim_pop(w));
    TOPT(224 - font_width(buf), 6, buf, C_TEXT);
    if (w->voice_on) spr(fb_top, SCR_W, TOP_H, 186, 8, ICON_MIC, 8, 8, false);
}

static void bar(int x, int y, int wpx, int v, uint16_t c)
{
    BRECT(x, y, wpx, 8, C_LINE);
    BRECT(x + 1, y + 1, (wpx - 2) * v / 1000, 6, c);
}

static void hint(const char *s) { BOTT(120 - font_width(s) / 2, 73, s, C_DIM); }

static void panel_resident(const world_t *w, const ui_t *ui)
{
    char buf[96];
    const resident_t *r = &w->res[ui->who];
    BRECT(10, 4, 4, 10, w->era == ERA_STONE ? LEATHER[ui->who] : SHIRT[ui->who]);
    int x = BOTT(18, 3, sim_name(r), C_GOLD);
    snprintf(buf, sizeof buf, " %s %d岁 %s", r->sex ? "女" : "男", sim_age(r), sim_trait(r));
    x = BOTT(x, 3, buf, C_TEXT);
    if (r->sick) x = BOTT(x + 6, 3, "染病", C_RED);
    if (r->devo >= 40) x = BOTT(x + 6, 3, "虔诚", C_GOLD);
    else if (r->devo <= -40) x = BOTT(x + 6, 3, "畏神", RGB(170, 150, 220));
    if (r->preg) BOTT(x + 6, 3, "有孕", RGB(240, 130, 190));
    int p = sim_prof(r);
    if (p >= 0) snprintf(buf, sizeof buf, "%s·%s  %s", sim_prof_name(r), sim_skill_level(r->skill[p]), sim_status(w, r));
    else snprintf(buf, sizeof buf, "%s  %s", sim_prof_name(r), sim_status(w, r));
    BOTT(10, 16, buf, C_TEXT);
    /* 家庭：两行信息轮流显示 */
    if ((ui->frame / 35) & 1) {
        if (r->fname != NO_SLOT || r->mname != NO_SLOT)
            snprintf(buf, sizeof buf, "父%s 母%s", r->fname != NO_SLOT ? sim_name_of(0, r->fname) : "不详",
                     r->mname != NO_SLOT ? sim_name_of(1, r->mname) : "不详");
        else snprintf(buf, sizeof buf, "初代居民 %s", r->home != NO_ID ? "有房" : "住在营地");
    } else {
        int n = 0;
        if (r->partner != NO_SLOT && w->res[r->partner].alive)
            n = snprintf(buf, sizeof buf, "伴侣%s ", sim_name(&w->res[r->partner]));
        else n = snprintf(buf, sizeof buf, "单身 ");
        snprintf(buf + n, sizeof buf - (size_t)n, "子女%d 第%d代", r->kids, r->gen);
    }
    BOTT(10, 29, buf, C_DIM);
    BOTT(10, 43, "生命", C_DIM);
    bar(38, 45, 72, r->hp, C_RED);
    BOTT(124, 43, "饱食", C_DIM);
    bar(152, 45, 72, r->food, RGB(246, 170, 70));
    BOTT(10, 57, "体力", C_DIM);
    bar(38, 59, 72, r->energy, RGB(90, 170, 240));
    BOTT(124, 57, "心情", C_DIM);
    bar(152, 59, 72, r->mood, RGB(240, 130, 190));
    hint(((ui->frame / 30) & 1) ? "长按上：赐福 长按下：降罚" : "确认：心声与生平");
}

/* 心声与生平 */
static void panel_bio(const world_t *w, const ui_t *ui)
{
    char buf[96];
    const resident_t *r = &w->res[ui->who];
    char th[64];
    sim_thought(w, r, th, sizeof th);
    int x = BOTT(10, 3, sim_name(r), C_GOLD);
    snprintf(buf, sizeof buf, "：%s", th);
    BOTT(x, 3, buf, RGB(255, 190, 220));
    sim_want(w, r, th, sizeof th);
    if (r->dream && ((ui->frame / 30) & 1)) /* 心愿和一生的梦想轮流显示 */
        snprintf(buf, sizeof buf, "梦想：%s%s", sim_dream_name(r->dream), (r->flags & RF_DREAM) ? "（已实现）" : "");
    else snprintf(buf, sizeof buf, "心愿：%s", th);
    BOTT(10, 16, buf, C_GOLD);
    BOTT(10, 29, "娱乐", C_DIM);
    bar(38, 31, 72, r->fun, RGB(120, 220, 140));
    BOTT(124, 29, "社交", C_DIM);
    bar(152, 31, 72, r->social, RGB(250, 200, 90));
    for (int k = 0; k < 2; k++) {
        sim_event_text(r, k, buf, sizeof buf);
        if (!buf[0]) break;
        BOTT(10, 44 + k * 13, buf, k ? C_DIM : C_TEXT);
    }
    hint("上下换人 确认返回");
}

/* 成就：两页，每页十二项，两列 */
static void panel_ach(const world_t *w, const ui_t *ui)
{
    char buf[48];
    snprintf(buf, sizeof buf, "成就 %d/%d", sim_ach_count(w), ACH_N);
    BOTT(10, 2, buf, C_GOLD);
    snprintf(buf, sizeof buf, "%d/3", ui->ach_page + 1);
    BOTT(230 - font_width(buf), 2, buf, C_DIM);
    for (int k = 0; k < 12; k++) {
        int a = ui->ach_page * 12 + k;
        if (a >= ACH_N) break;
        bool got = (w->ach >> a) & 1;
        int x = k < 6 ? 10 : 124, y = 15 + (k % 6) * 12;
        BRECT(x, y + 3, 5, 5, got ? C_GOLD : C_LINE);
        BOTT(x + 8, y, got || (ui->frame / 40 + (uint32_t)a) % 3 ? sim_ach_name(a) : sim_ach_how(a), got ? C_TEXT : C_DIM);
    }
}

/* 故事页下面的提示 */
static void panel_story(const world_t *w, const ui_t *ui)
{
    static char text[1200];
    if (ui->story_res || !sim_story_pending(w)) {
        BOTT(10, 24, "故事有了结局", C_GOLD);
        hint("确认：合上");
        return;
    }
    sim_story_text(w, text, sizeof text);
    if (ui->story_page < story_pages(text) - 1) {
        BOTT(10, 20, "岛上发生了一件事……", C_DIM);
        hint("确认：往下读　上：往回翻");
        return;
    }
    if (sim_story_nopts(w) >= 2) {
        BOTT(10, 4, "接下来半天里，你的神迹会改变结局：", C_TEXT);
        BOTT(10, 22, "降福（祝福、创造、彩虹……）", RGB(150, 230, 140));
        BOTT(10, 38, "降威（雷电、暴雨、灾害……）", C_RED);
        BOTT(10, 54, "什么都不做，就顺其自然", C_DIM);
    }
    hint("确认：合上，看他们怎么样");
}

static void meter(int x, int y, int v, uint16_t c)
{
    BRECT(x, y, 44, 8, C_LINE);
    BRECT(x + 1, y + 1, 42 * v / 100, 6, c);
}

static void panel_records(const world_t *w)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s时代 逝者%d 新生%d 传%d代", sim_era_name(w->era), w->deaths, w->births, w->max_gen);
    BOTT(10, 3, buf, C_GOLD);
    BOTT(10, 17, "信仰", C_DIM);
    meter(38, 19, w->faith, C_GOLD);
    BOTT(92, 17, "恐惧", C_DIM);
    meter(120, 19, w->fear, C_RED);
    BOTT(174, 17, w->statue != NO_ID ? "有神像" : w->shrine != NO_ID ? "有祭石" : "无祭石", C_DIM);
    snprintf(buf, sizeof buf, "家猪%d 看家狗%d 交易%d次 远航%d次", w->livestock, sim_kind_count(w, A_DOG), w->trades, w->voyages);
    BOTT(10, 31, buf, C_TEXT);
    int shown = 0;
    for (int k = 1; k <= MAX_DEAD && shown < 2; k++) { /* 最近去世的排在最前 */
        const dead_t *d = &w->dead[(uint8_t)(w->dead_n - k) % MAX_DEAD];
        if (!d->used) continue;
        snprintf(buf, sizeof buf, "%s %s %d岁 %s 第%d代", sim_name_of(d->sex, d->name), d->sex ? "女" : "男", d->age,
                 sim_cause_name(d->cause), d->gen);
        BOTT(10, 45 + shown * 13, buf, C_DIM);
        shown++;
    }
    if (!shown) BOTT(10, 45, "岛上还没有人离世", C_DIM);
    hint("确认：查看成就");
}

/* 离线补算的进度 */
static void panel_catchup(const world_t *w, const ui_t *ui)
{
    char buf[96];
    BOTT(10, 6, "你不在的时候，岛上的日子照常过", C_GOLD);
    snprintf(buf, sizeof buf, "第%d天  人口%d", sim_day(w), sim_pop(w));
    BOTT(10, 24, buf, C_TEXT);
    BRECT(10, 44, 220, 10, C_LINE);
    uint32_t done = ui->catch_total - ui->catch_left;
    BRECT(11, 45, (int)(218 * (uint64_t)done / (ui->catch_total ? ui->catch_total : 1)), 8, C_GOLD);
    hint("确认键跳过");
}

/* 归来纪事 */
static void panel_chronicle(const world_t *w, const ui_t *ui)
{
    char buf[128];
    const chron_t *c = &ui->chron;
    uint32_t h = c->away_s / 3600, m = c->away_s % 3600 / 60;
    int n = 0;
    if (h) n = snprintf(buf, sizeof buf, "你离开了%u小时", (unsigned)h);
    else n = snprintf(buf, sizeof buf, "你离开了%u分钟", (unsigned)m);
    snprintf(buf + n, sizeof buf - (size_t)n, " 岛上过了%d天", c->days);
    BOTT(10, 3, buf, C_GOLD);
    snprintf(buf, sizeof buf, "新生%d人 去世%d人 结伴%d对", c->births, c->deaths, c->marriages);
    BOTT(10, 18, buf, C_TEXT);
    snprintf(buf, sizeof buf, "新盖房屋%d间 现有%d人 第%d代", c->built, sim_pop(w), w->max_gen);
    BOTT(10, 32, buf, C_TEXT);
    /* 期间离世的人里最近的两位 */
    n = 0;
    int shown = 0;
    buf[0] = 0;
    for (int k = 1; k <= MAX_DEAD && shown < 2; k++) {
        const dead_t *d = &w->dead[(uint8_t)(w->dead_n - k) % MAX_DEAD];
        if (!d->used || d->day < c->from_day) continue;
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%s%s%d岁%s", shown ? " " : "送别：", sim_name_of(d->sex, d->name),
                      d->age, sim_cause_name(d->cause));
        shown++;
    }
    if (!shown) snprintf(buf, sizeof buf, "%s", sim_pop(w) ? "这段日子无人离世" : "岛上已无人烟");
    BOTT(10, 46, buf, C_DIM);
    hint("确认继续");
}

void render_bot(const world_t *w, const ui_t *ui)
{
    char buf[96];
    BRECT(0, 0, SCR_W, BOT_H, C_BG);
    BRECT(0, 0, SCR_W, 1, C_LINE);

    if (ui->catching) { panel_catchup(w, ui); return; }
    if (ui->inspect == 1 && w->res[ui->who].alive) { panel_resident(w, ui); return; }
    if (ui->inspect == 5 && w->res[ui->who].alive) { panel_bio(w, ui); return; }
    if (ui->inspect == 2) { panel_records(w); return; }
    if (ui->inspect == 6) {
        panel_ach(w, ui);
        return;
    }
    if (ui->inspect == 7) {
        panel_story(w, ui);
        return;
    }
    if (ui->inspect == 3) { panel_chronicle(w, ui); return; }
    if (ui->inspect == 4) {
        BOTT(10, 10, "上下：选择一位亲属", C_TEXT);
        BOTT(10, 26, "确认：转到此人的家谱", C_TEXT);
        BOTT(10, 42, "长按确认：返回岛屿", C_TEXT);
        return;
    }

    snprintf(buf, sizeof buf, "粮%d 木%d 石%d 野猪%d 兔%d 狼%d", w->food, w->wood, w->stone, sim_kind_count(w, A_BOAR),
             sim_kind_count(w, A_RABBIT), sim_kind_count(w, A_WOLF));
    BOTT(10, 4, buf, C_TEXT);
    const char *wn = (w->weather == W_RAIN && sim_season(w) == SEASON_WINTER) ? "小雪" : sim_weather_name(w->weather);
    BOTT(230 - font_width(wn), 4, wn, w->weather >= W_STORM ? C_GOLD : C_DIM);
    /* 最近两条事件，旧的在上、颜色更暗；岛民有所祈求时，上面一行换成祈求 */
    if (sim_story_pending(w) && w->evt_seen) {
        char t[48];
        sim_story_title(w, t, sizeof t);
        snprintf(buf, sizeof buf, "故事：%s（%d时内）", t, w->evt_h);
        BOTT(10, 19, buf, ((ui->frame / 8) & 1) ? RGB(255, 214, 140) : RGB(240, 190, 110));
    } else if (w->wish) {
        snprintf(buf, sizeof buf, "岛民祈求：%s（还等%d时）", sim_wish_name(w->wish), w->wish_h);
        BOTT(10, 19, buf, ((ui->frame / 8) & 1) ? RGB(255, 170, 210) : RGB(240, 130, 190));
    } else if (!sim_pop(w)) {
        BOTT(10, 19, "岛上已无人烟，等待新的先民", C_RED);
    } else if (g_log.seq >= 2) {
        BOTT(10, 19, g_log.msg[(g_log.seq - 2) % LOG_N], C_DIM);
    }
    if (g_log.seq >= 1) BOTT(10, 33, g_log.msg[(g_log.seq - 1) % LOG_N], C_GOLD);

    /* 神力选择条：类别 · 名称 */
    BRECT(40, 50, 160, 19, C_PANEL);
    BRECT(40, 50, 160, 1, C_LINE);
    BRECT(40, 68, 160, 1, C_LINE);
    const char *cat = sim_power_cat(ui->sel);
    if (ui->sel == P_SPEED) snprintf(buf, sizeof buf, "流速%d倍", sim_speed(w));
    else if (ui->sel == P_SOUND) snprintf(buf, sizeof buf, "音效 %s", w->sound_on ? "开" : "关");
    else if (ui->sel == P_VOICE) snprintf(buf, sizeof buf, "声控神力 %s", w->voice_on ? "开" : "关");
    else if (ui->sel == P_ZOOM) snprintf(buf, sizeof buf, "镜头 %s", w->zoom ? "近景" : "全岛");
    else if (ui->sel == P_NEWWORLD && ui->confirm_t) snprintf(buf, sizeof buf, "再按一次确认");
    else snprintf(buf, sizeof buf, "%s", sim_power_name(ui->sel));
    int total = font_width(cat) + 14 + font_width(buf);
    int x = 120 - total / 2;
    x = BOTT(x, 54, cat, C_DIM);
    BRECT(x + 6, 59, 2, 2, C_DIM);
    bool red = sim_power_cat_id(ui->sel) == 4 || ui->sel == P_WOLF || ui->sel == P_NEWWORLD;
    BOTT(x + 14, 54, buf, red ? C_RED : C_TEXT);
    for (int r = 0; r < 5; r++) { /* 左右两个小三角 */
        int half = r < 3 ? r : 4 - r;
        BRECT(31 - half, 57 + r, half + 1, 1, C_GOLD);
        BRECT(207, 57 + r, half + 1, 1, C_GOLD);
    }
    if (ui->sel == P_NEWWORLD) hint(ui->confirm_t ? "整座岛和所有人都会消失" : "确认：毁掉这座岛，另造一座");
    else if (!sim_pop(w) && ui->sel == P_GENESIS) hint("确认：降下五位新的先民");
    else if ((ui->frame / 40) & 1) hint("长按上下换类别 长按确认暂停");
    else hint(ui->sel <= P_RECORDS ? "上下选择 确认查看" : ui->sel >= P_SPEED ? "上下选择 确认切换" : "上下选择 确认施放");
}
