/*
 * REIS BOT - plataforma de acao 2D para PS2 (homebrew)
 * PS2SDK + gsKit. Tudo desenhado com retangulos (sem texturas/audio).
 *
 * Controles:
 *   D-pad esquerda/direita = andar
 *   X (cross)              = pular (segure para pular mais alto)
 *   Quadrado               = atirar (segure para carregar o tiro grande)
 *   Start                  = comecar / pausar
 */

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- configuracao ---------- */
#define SCALE 2            /* tela logica 320x224 -> 640x448 */
#define VW 320
#define VH 224
#define TS 16              /* tamanho do tile */
#define MW 150             /* largura da fase em tiles */
#define MH 14              /* altura da fase em tiles */

#define GRAV 0.28f
#define MAXFALL 6.0f
#define RUN 1.6f
#define JUMP 5.6f
#define MAXHP 28
#define CHARGE_FRAMES 60

#define MAX_BULLETS 24
#define MAX_ENEMIES 40
#define MAX_ITEMS 8
#define MAX_PARTS 48

#define RGB(r, g, b) GS_SETREG_RGBAQ((r), (g), (b), 0x80, 0x00)

enum { ST_TITLE, ST_PLAY, ST_OVER, ST_WIN };
enum { E_MET, E_BATTON, E_BOSS };

/* ---------- tipos ---------- */
typedef struct { float x, y, vx, vy; int w, h, dir, hp, inv, stun, ground, charge, lives; } Player;
typedef struct { int active, own, dmg; float x, y, vx, vy; } Bullet;   /* own: 0 jogador, 1 inimigo */
typedef struct { int active, type, hp, dir, t, st, w, h, flash, ground; float x, y, vx, vy, by; } Enemy;
typedef struct { int active; float x, y, vy; } Item;
typedef struct { int active, life, size, r, g, b; float x, y, vx, vy; } Part;

/* ---------- estado global ---------- */
static GSGLOBAL *g;
static char padBuf[256] __attribute__((aligned(64)));
static unsigned prevpad = 0;

static unsigned char map[MH][MW];
static Player p;
static Bullet bl[MAX_BULLETS];
static Enemy en[MAX_ENEMIES];
static Item it[MAX_ITEMS];
static Part pt[MAX_PARTS];

static int state = ST_TITLE;
static int paused = 0, ready = 0, frame = 0, score = 0;
static int bossActive = 0, bossDeath = 0, shake = 0;
static float cam = 0, cpx = 3 * TS;
static float bossX = 0, bossY = 0;

/* spawns: tipo, coluna, linha (met/boss: linha do chao; batton: linha exata) */
static const int spawns[][3] = {
    {E_MET, 18, 12}, {E_MET, 23, 9},  {E_BATTON, 26, 5}, {E_MET, 36, 12},
    {E_BATTON, 44, 4}, {E_MET, 49, 12}, {E_BATTON, 64, 5}, {E_MET, 66, 12},
    {E_MET, 76, 8},  {E_MET, 82, 12}, {E_BATTON, 90, 4}, {E_MET, 93, 12},
    {E_MET, 104, 12}, {E_BATTON, 110, 5}, {E_MET, 116, 12}, {E_BATTON, 118, 4},
    {E_MET, 122, 12}, {E_BOSS, 141, 12},
};
#define NSPAWN ((int)(sizeof(spawns) / sizeof(spawns[0])))

/* fonte 3x5 (A-Z, 0-9) */
static const unsigned char font[36][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
    {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},
    {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},
    {7,4,6,1,6},{3,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,6},
};

/* ---------- utilidades de desenho ---------- */
static void ui(float x, float y, float w, float h, u64 c)
{
    gsKit_prim_sprite(g, x * SCALE, y * SCALE, (x + w) * SCALE, (y + h) * SCALE, 1, c);
}

/* desenha no mundo (com camera e tremor de tela) */
static void put(float x, float y, float w, float h, u64 c)
{
    float sx = x - cam;
    if (shake > 0) sx += ((frame & 1) ? 1.0f : -1.0f) * (shake > 6 ? 2.0f : 1.0f);
    if (sx + w < 0 || sx > VW || y + h < 0 || y > VH) return;
    ui(sx, y, w, h, c);
}

/* retangulo em coordenadas locais do sprite; espelha se dir < 0 (w = largura do sprite) */
static void mput(float x, float w, int dir, float lx, float ly, float rw, float rh, u64 c)
{
    float ox = (dir > 0) ? lx : (w - lx - rw);
    put(x + ox, ly, rw, rh, c);
}

/* texto: cada linha da fonte vira no maximo um retangulo por sequencia de pixels */
static void text(float x, float y, const char *s, int sc, u64 c)
{
    for (; *s; s++, x += 4 * sc) {
        char ch = *s;
        int i = -1, r, col, start;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch >= 'A' && ch <= 'Z') i = ch - 'A';
        else if (ch >= '0' && ch <= '9') i = 26 + ch - '0';
        if (i < 0) continue;
        for (r = 0; r < 5; r++) {
            start = -1;
            for (col = 0; col <= 3; col++) {
                int on = (col < 3) && (font[i][r] & (4 >> col));
                if (on && start < 0) start = col;
                else if (!on && start >= 0) {
                    ui(x + start * sc, y + r * sc, (col - start) * sc, sc, c);
                    start = -1;
                }
            }
        }
    }
}

static void text_s(float x, float y, const char *s, int sc, u64 c)   /* com sombra */
{
    float o = (sc > 3) ? 2 : 1;
    text(x + o, y + o, s, sc, RGB(0, 0, 30));
    text(x, y, s, sc, c);
}

static void text_center(float y, const char *s, int sc, u64 c)
{
    text_s((VW - (int)strlen(s) * 4 * sc + sc) / 2, y, s, sc, c);
}

static void panel(float x, float y, float w, float h)
{
    ui(x - 3, y - 3, w + 6, h + 6, RGB(150, 160, 205));
    ui(x - 1, y - 1, w + 2, h + 2, RGB(40, 44, 90));
    ui(x, y, w, h, RGB(10, 12, 40));
}

/* ---------- particulas ---------- */
static void burst(float x, float y, int n, int r, int gr, int b, float spd)
{
    int i, k;
    for (k = 0; k < n; k++) {
        for (i = 0; i < MAX_PARTS; i++) if (!pt[i].active) break;
        if (i >= MAX_PARTS) return;
        pt[i].active = 1;
        pt[i].life = 14 + rand() % 14;
        pt[i].size = 2 + rand() % 2;
        pt[i].x = x; pt[i].y = y;
        pt[i].vx = ((rand() % 201) - 100) / 100.0f * spd;
        pt[i].vy = ((rand() % 201) - 100) / 100.0f * spd - 0.6f;
        pt[i].r = r; pt[i].g = gr; pt[i].b = b;
    }
}

static void update_parts(void)
{
    int i;
    for (i = 0; i < MAX_PARTS; i++) {
        Part *q = &pt[i];
        if (!q->active) continue;
        q->x += q->vx; q->y += q->vy; q->vy += 0.08f;
        if (--q->life <= 0) q->active = 0;
    }
}

static void draw_parts(void)
{
    int i;
    for (i = 0; i < MAX_PARTS; i++) {
        Part *q = &pt[i];
        int f, sz;
        if (!q->active) continue;
        f = (q->life >= 12) ? 256 : q->life * 256 / 12;
        sz = (q->life > 8) ? q->size : 1;
        put(q->x - sz / 2.0f, q->y - sz / 2.0f, sz, sz, RGB(q->r * f / 256, q->g * f / 256, q->b * f / 256));
    }
}

/* ---------- colisao ---------- */
static int tile_at(int tx, int ty)
{
    if (tx < 0 || tx >= MW) return 1;
    if (ty < 0 || ty >= MH) return 0;
    return map[ty][tx];
}

static int hits(float x, float y, int w, int h, int type)
{
    int x0 = (int)floorf(x / TS), x1 = (int)floorf((x + w - 1) / TS);
    int y0 = (int)floorf(y / TS), y1 = (int)floorf((y + h - 1) / TS);
    int i, j;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++)
            if (tile_at(i, j) == type) return 1;
    return 0;
}

static int ov(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static int move_x(float *x, float y, int w, int h, float *vx)
{
    *x += *vx;
    if (hits(*x, y, w, h, 1)) {
        if (*vx > 0) *x = floorf((*x + w - 1) / TS) * TS - w;
        else *x = (floorf(*x / TS) + 1) * TS;
        *vx = 0;
        return 1;
    }
    return 0;
}

static int move_y(float x, float *y, int w, int h, float *vy)   /* retorna 1 se pousou */
{
    int landed = 0;
    *y += *vy;
    if (hits(x, *y, w, h, 1)) {
        if (*vy > 0) { *y = floorf((*y + h - 1) / TS) * TS - h; landed = 1; }
        else *y = (floorf(*y / TS) + 1) * TS;
        *vy = 0;
    }
    return landed;
}

/* ---------- fase ---------- */
static void fill(int x, int y, int w, int h, int t)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i >= 0 && x + i < MW && y + j >= 0 && y + j < MH) map[y + j][x + i] = t;
}

static void build_level(void)
{
    memset(map, 0, sizeof(map));
    fill(0, 12, MW, 2, 1);                  /* chao */
    fill(0, 0, 1, 12, 1);                   /* parede esquerda */
    fill(MW - 1, 0, 1, 12, 1);              /* parede direita */

    fill(30, 12, 3, 2, 0);                  /* buracos */
    fill(58, 12, 3, 2, 0);
    fill(96, 12, 3, 2, 0);

    fill(14, 10, 3, 1, 1);                  /* plataformas e obstaculos */
    fill(22, 9, 4, 1, 1);
    fill(40, 10, 2, 2, 1);
    fill(46, 9, 2, 3, 1);
    fill(52, 11, 3, 1, 2);                  /* espinhos */
    fill(62, 9, 3, 1, 1);

    fill(70, 11, 1, 1, 1);                  /* escada */
    fill(71, 10, 1, 2, 1);
    fill(72, 9, 1, 3, 1);
    fill(73, 8, 1, 4, 1);
    fill(74, 8, 5, 1, 1);

    fill(100, 9, 3, 1, 1);
    fill(105, 7, 3, 1, 1);
    fill(108, 11, 3, 1, 2);
    fill(112, 9, 1, 3, 1);
    fill(141, 9, 3, 1, 1);                  /* plataforma na arena do chefe */
}

static void spawn_enemies(void)
{
    int i;
    memset(en, 0, sizeof(en));
    for (i = 0; i < NSPAWN && i < MAX_ENEMIES; i++) {
        Enemy *e = &en[i];
        e->active = 1;
        e->type = spawns[i][0];
        e->x = spawns[i][1] * TS;
        e->dir = -1;
        switch (e->type) {
        case E_MET:    e->w = 14; e->h = 14; e->hp = 1;  e->y = spawns[i][2] * TS - e->h; break;
        case E_BATTON: e->w = 14; e->h = 10; e->hp = 1;  e->y = spawns[i][2] * TS; e->by = e->y; break;
        case E_BOSS:   e->w = 24; e->h = 28; e->hp = 28; e->y = spawns[i][2] * TS - e->h; break;
        }
    }
}

static void respawn_player(void)
{
    p.x = cpx; p.y = 10 * TS; p.vx = p.vy = 0;
    p.w = 12; p.h = 20; p.dir = 1; p.hp = MAXHP;
    p.inv = 0; p.stun = 0; p.ground = 0; p.charge = 0;
    memset(bl, 0, sizeof(bl));
    memset(it, 0, sizeof(it));
    memset(pt, 0, sizeof(pt));
    build_level();
    spawn_enemies();
    bossActive = 0; bossDeath = 0; shake = 0;
    ready = 90;
}

static void new_game(void)
{
    p.lives = 3; score = 0; cpx = 3 * TS;
    respawn_player();
    state = ST_PLAY; paused = 0;
}

/* ---------- jogador ---------- */
static void fire(int own, int dmg, float x, float y, float vx, float vy)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++)
        if (!bl[i].active) {
            bl[i].active = 1; bl[i].own = own; bl[i].dmg = dmg;
            bl[i].x = x; bl[i].y = y; bl[i].vx = vx; bl[i].vy = vy;
            return;
        }
}

static int small_shots(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_BULLETS; i++)
        if (bl[i].active && bl[i].own == 0 && bl[i].dmg == 1) n++;
    return n;
}

static void kill_player(void)
{
    p.lives--;
    if (p.lives <= 0) { state = ST_OVER; return; }
    respawn_player();
}

static void hurt_player(int dmg)
{
    if (p.inv > 0 || p.hp <= 0) return;
    p.hp -= dmg;
    p.inv = 90; p.stun = 14; p.vy = -3.0f; p.vx = -p.dir * 1.2f; p.charge = 0;
    shake = 10;
    burst(p.x + p.w / 2, p.y + p.h / 2, 8, 120, 180, 255, 1.8f);
    if (p.hp <= 0) kill_player();
}

static void update_player(unsigned pad, unsigned pressed)
{
    int was;
    float fall;

    if (p.inv > 0) p.inv--;

    if (p.stun > 0) {
        p.stun--;
    } else {
        if (pad & PAD_LEFT) { p.vx = -RUN; p.dir = -1; }
        else if (pad & PAD_RIGHT) { p.vx = RUN; p.dir = 1; }
        else p.vx = 0;

        if ((pressed & PAD_CROSS) && p.ground) { p.vy = -JUMP; p.ground = 0; }
        if (!(pad & PAD_CROSS) && p.vy < -2.0f) p.vy = -2.0f;   /* pulo variavel */

        if (pressed & PAD_SQUARE) {
            if (small_shots() < 3)
                fire(0, 1, p.x + (p.dir > 0 ? 12 : -6), p.y + 10, p.dir * 4.0f, 0);
            p.charge = 0;
        }
        if (pad & PAD_SQUARE) {
            if (p.charge < 200) p.charge++;
            if (p.charge >= CHARGE_FRAMES && (frame % 4) == 0)
                burst(p.x + (p.dir > 0 ? 16 : -4), p.y + 12, 1, 140, 240, 255, 0.8f);
        } else {
            if (p.charge >= CHARGE_FRAMES)
                fire(0, 3, p.x + (p.dir > 0 ? 10 : -12), p.y + 7, p.dir * 5.0f, 0);
            p.charge = 0;
        }
    }

    p.vy += GRAV;
    if (p.vy > MAXFALL) p.vy = MAXFALL;
    was = p.ground;
    fall = p.vy;
    move_x(&p.x, p.y, p.w, p.h, &p.vx);
    p.ground = move_y(p.x, &p.y, p.w, p.h, &p.vy);
    if (p.ground && !was && fall > 2.5f)                    /* poeira ao pousar */
        burst(p.x + p.w / 2, p.y + p.h, 5, 200, 200, 215, 1.2f);

    if (hits(p.x + 2, p.y + 2, p.w - 4, p.h - 4, 2)) { p.hp = 0; kill_player(); return; }
    if (p.y > MH * TS + 20) { p.hp = 0; kill_player(); return; }

    if (p.x > 118 * TS) cpx = 118 * TS;
    else if (p.x > 84 * TS) cpx = 84 * TS;

    if (!bossActive && p.x > 131 * TS) {            /* fecha a porta do chefe */
        bossActive = 1;
        fill(129, 0, 1, 12, 1);
    }
}

/* ---------- inimigos ---------- */
static void kill_enemy(Enemy *e)
{
    e->active = 0;
    if (e->type == E_BOSS) {
        score += 2000; bossDeath = 150; shake = 30;
        bossX = e->x; bossY = e->y;
        burst(e->x + e->w / 2, e->y + e->h / 2, 30, 255, 200, 80, 3.0f);
        memset(bl, 0, sizeof(bl));
        return;
    }
    burst(e->x + e->w / 2, e->y + e->h / 2, 12, 255, 170, 60, 2.0f);
    score += (e->type == E_MET) ? 100 : 150;
    if (rand() % 4 == 0) {
        int i;
        for (i = 0; i < MAX_ITEMS; i++)
            if (!it[i].active) { it[i].active = 1; it[i].x = e->x; it[i].y = e->y; it[i].vy = -2; break; }
    }
}

static void update_enemy(Enemy *e)
{
    float px = p.x + p.w / 2, ex = e->x + e->w / 2;
    if (e->flash > 0) e->flash--;
    e->t++;

    if (e->type == E_MET) {
        if (fabsf(px - ex) > 240) return;
        if (e->st == 0) {                        /* anda */
            e->dir = (px < ex) ? -1 : 1;
            e->vx = 0.5f * e->dir;
            if (e->t > 100) { e->st = 1; e->t = 0; e->vx = 0; }
        } else if (e->st == 1) {                 /* escondido (invulneravel) */
            e->vx = 0;
            if (e->t > 50) { e->st = 2; e->t = 0; }
        } else {                                 /* aparece e atira */
            e->vx = 0;
            if (e->t == 12) fire(1, 2, ex, e->y + 4, (px < ex ? -1.8f : 1.8f), 0);
            if (e->t > 45) { e->st = 0; e->t = 0; }
        }
        e->vy += GRAV; if (e->vy > MAXFALL) e->vy = MAXFALL;
        if (move_x(&e->x, e->y, e->w, e->h, &e->vx)) e->t += 20;
        move_y(e->x, &e->y, e->w, e->h, &e->vy);
        if (e->y > MH * TS + 40) e->active = 0;
    } else if (e->type == E_BATTON) {
        if (e->st == 0) {
            if (fabsf(px - ex) < 110) { e->st = 1; e->t = 0; }
            return;
        }
        e->x += (px < ex ? -1.0f : 1.0f);
        e->y = e->by + sinf(e->t * 0.12f) * 22.0f;
        if (e->t > 400) e->active = 0;
    } else if (!bossActive) {
        return;
    } else {                                     /* chefe */
        e->dir = (px < ex) ? -1 : 1;
        if (e->st == 0) {
            e->vx = 0;
            if (e->t > 50) { e->st = 1; e->t = 0; e->vy = -6.5f; e->vx = 2.0f * e->dir; }
        } else if (e->st == 1) {
            if (e->t > 8 && e->ground) { e->st = 2; e->t = 0; e->vx = 0; }
        } else {                                 /* leque de 3 tiros */
            if (e->t == 12) {
                float dx = e->dir * 2.6f;
                fire(1, 3, ex, e->y + 8, dx, -0.9f);
                fire(1, 3, ex, e->y + 8, dx, 0.0f);
                fire(1, 3, ex, e->y + 8, dx, 0.9f);
            }
            if (e->t > 40) { e->st = 0; e->t = 0; }
        }
        e->vy += GRAV; if (e->vy > MAXFALL) e->vy = MAXFALL;
        move_x(&e->x, e->y, e->w, e->h, &e->vx);
        e->ground = move_y(e->x, &e->y, e->w, e->h, &e->vy);
    }
}

/* ---------- balas e itens ---------- */
static void update_bullets(void)
{
    int i, k;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bl[i];
        if (!b->active) continue;
        b->x += b->vx; b->y += b->vy;
        if (b->x < cam - 24 || b->x > cam + VW + 24 || b->y < -20 || b->y > VH + 20 ||
            tile_at((int)floorf(b->x / TS), (int)floorf(b->y / TS)) == 1) { b->active = 0; continue; }

        if (b->own == 0) {
            for (k = 0; k < MAX_ENEMIES; k++) {
                Enemy *e = &en[k];
                if (!e->active) continue;
                if (e->type == E_BOSS && !bossActive) continue;
                if (!ov(b->x - 3, b->y - 3, 6, 6, e->x, e->y, e->w, e->h)) continue;
                b->active = 0;
                if (e->type == E_MET && e->st == 1) {           /* capacete: sem dano */
                    burst(b->x, b->y, 3, 180, 180, 200, 1.0f);
                    break;
                }
                e->hp -= b->dmg; e->flash = 4;
                burst(b->x, b->y, 3, 255, 255, 255, 1.2f);
                if (e->type == E_BOSS) shake = 3;
                if (e->hp <= 0) kill_enemy(e);
                break;
            }
        } else if (ov(b->x - 3, b->y - 3, 6, 6, p.x, p.y, p.w, p.h)) {
            b->active = 0;
            hurt_player(b->dmg);
        }
    }
}

static void update_items(void)
{
    int i;
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *m = &it[i];
        if (!m->active) continue;
        m->vy += GRAV; if (m->vy > MAXFALL) m->vy = MAXFALL;
        move_y(m->x, &m->y, 8, 8, &m->vy);
        if (m->y > MH * TS + 20) m->active = 0;
        if (ov(m->x, m->y, 8, 8, p.x, p.y, p.w, p.h)) {
            m->active = 0;
            p.hp += 6; if (p.hp > MAXHP) p.hp = MAXHP;
            burst(m->x + 4, m->y + 4, 6, 120, 255, 140, 1.4f);
        }
    }
}

/* ---------- loop de logica ---------- */
static void update(unsigned pad, unsigned pressed)
{
    int i;
    if (state == ST_TITLE || state == ST_OVER || state == ST_WIN) {
        if (pressed & PAD_START) new_game();
        return;
    }
    if (pressed & PAD_START) paused = !paused;
    if (paused) return;
    if (shake > 0) shake--;
    if (ready > 0) { ready--; goto camera; }

    update_player(pad, pressed);
    if (state != ST_PLAY) return;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &en[i];
        if (!e->active) continue;
        update_enemy(e);
        if (!e->active) continue;
        if (e->type == E_BOSS && !bossActive) continue;
        if (ov(p.x, p.y, p.w, p.h, e->x + 1, e->y + 1, e->w - 2, e->h - 2))
            hurt_player(e->type == E_BOSS ? 4 : 3);
        if (state != ST_PLAY) return;
    }
    update_bullets();
    update_items();
    update_parts();

    if (bossDeath > 0) {                         /* explosoes em cadeia do chefe */
        if (bossDeath % 6 == 0) {
            burst(bossX + rand() % 24, bossY + rand() % 28, 8, 255, 220, 120, 2.5f);
            shake = 8;
        }
        if (--bossDeath == 0) state = ST_WIN;
    }

camera:
    cam = p.x + p.w / 2 - VW / 2;
    if (cam < 0) cam = 0;
    if (cam > MW * TS - VW) cam = MW * TS - VW;
    if (bossActive && cam < 130 * TS) cam = 130 * TS;
}

/* ---------- desenho ---------- */
static void draw_background(float c)
{
    int i, w;
    float par = c * 0.4f, par2 = c * 0.15f;
    int base = (int)(par / 60), base2 = (int)(par2 / 80);

    for (i = 0; i < 14; i++)                                     /* ceu em degrade */
        ui(0, i * 16, VW, 16, RGB(12 + i * 5, 14 + i * 4, 46 + i * 7));

    for (i = 0; i < 24; i++) {                                   /* estrelas */
        int sx = ((i * 47 + 13 - (int)(c * 0.05f)) % VW + VW) % VW;
        int sy = (i * 29) % 110;
        int tw = ((frame / 20) + i) % 4;
        ui(sx, sy, 1, 1, tw == 0 ? RGB(120, 120, 170) : RGB(235, 235, 255));
    }

    ui(252, 22, 8, 14, RGB(238, 238, 200));                      /* lua */
    ui(250, 24, 12, 10, RGB(238, 238, 200));
    ui(253, 25, 3, 3, RGB(210, 210, 170));
    ui(257, 29, 2, 2, RGB(210, 210, 170));

    for (i = 0; i < 6; i++) {                                    /* montanhas ao fundo */
        int idx = base2 + i;
        float bx = idx * 80 - par2;
        float h = 24 + ((idx * 53) % 4) * 8;
        ui(bx + 28, 192 - h, 24, h, RGB(44, 44, 100));
        ui(bx + 14, 192 - h * 0.66f, 52, h * 0.66f, RGB(44, 44, 100));
        ui(bx, 192 - h * 0.33f, 80, h * 0.33f, RGB(44, 44, 100));
    }

    for (i = 0; i < 7; i++) {                                    /* predios */
        int idx = base + i;
        float bx = idx * 60 - par;
        float h = 40 + ((idx * 37) % 5) * 12;
        ui(bx, 192 - h, 50, h, RGB(26, 36, 84));
        ui(bx, 192 - h, 50, 2, RGB(48, 62, 120));
        for (w = 0; w < 4; w++) {
            int lit = ((idx * 13 + w * 7) % 3) != 0;
            ui(bx + 8 + (w % 2) * 22, 192 - h + 10 + (w / 2) * 14, 6, 6,
               lit ? RGB(250, 230, 120) : RGB(40, 52, 100));
        }
    }

    ui(0, 192, VW, 32, RGB(8, 8, 28));                           /* abismo dos buracos */
}

static void draw_floor_strip(void)                               /* chao da tela de titulo */
{
    int i;
    ui(0, 192, VW, 32, RGB(84, 88, 122));
    ui(0, 192, VW, 3, RGB(150, 160, 205));
    ui(0, 192, VW, 1, RGB(205, 215, 250));
    ui(0, 199, VW, 1, RGB(58, 60, 92));
    ui(0, 206, VW, 18, RGB(70, 74, 106));
    for (i = 0; i < 21; i++) ui(i * 16 + ((i & 1) ? 4 : 10), 195, 1, 4, RGB(58, 60, 92));
}

static void draw_tiles(void)
{
    int tx0 = (int)(cam / TS), tx, ty, k;
    for (tx = tx0; tx <= tx0 + VW / TS + 1 && tx < MW; tx++)
        for (ty = 0; ty < MH; ty++) {
            int t = map[ty][tx];
            float x = tx * TS, y = ty * TS;
            if (t == 1) {
                int alt = (tx + ty) & 1;
                put(x, y, TS, TS, RGB(84, 88, 122));
                put(x, y + 7, TS, 1, RGB(58, 60, 92));
                put(x + (alt ? 4 : 10), y, 1, 7, RGB(58, 60, 92));
                put(x + (alt ? 10 : 4), y + 8, 1, 6, RGB(58, 60, 92));
                put(x, y + TS - 2, TS, 2, RGB(52, 54, 84));
                if (tile_at(tx, ty - 1) != 1) {
                    put(x, y, TS, 3, RGB(150, 160, 205));
                    put(x, y, TS, 1, RGB(205, 215, 250));
                }
            } else if (t == 2) {
                put(x, y + 12, TS, 4, RGB(90, 94, 110));
                for (k = 0; k < 4; k++) {
                    put(x + k * 4, y + 8, 4, 4, RGB(170, 175, 195));
                    put(x + k * 4 + 1, y + 5, 2, 3, RGB(205, 210, 228));
                    put(x + k * 4 + 1, y + 2, 2, 3, RGB(255, 255, 255));
                }
            }
        }
}

#define PM(lx, ly, rw, rh, c) mput(x, 12, p.dir, (lx), y + (ly), (rw), (rh), (c))

static void draw_player(void)
{
    u64 body = RGB(235, 110, 40), light = RGB(255, 165, 95), dark = RGB(170, 70, 20);     /* moletom */
    u64 line = RGB(40, 22, 16), skin = RGB(245, 200, 160), glove = RGB(70, 205, 235);
    u64 jeans = RGB(50, 75, 150), jeans2 = RGB(32, 50, 110), hair = RGB(60, 36, 24);
    u64 cap = RGB(210, 45, 55), shoe = RGB(245, 245, 245);
    float x = p.x, y = p.y;
    int moving = (p.vx != 0 && p.ground);
    int step = (frame / 5) % 2;
    int bob = (moving && step) ? 1 : 0;
    int lxa = 2, lxb = 6, ha = 6;

    if (p.inv > 0 && (frame / 3) % 2) return;
    if (p.charge >= CHARGE_FRAMES && (frame / 3) % 2) { body = RGB(255, 225, 90); light = RGB(255, 250, 200); }
    else if (p.charge > 20 && (frame / 6) % 2) { body = RGB(255, 150, 80); light = RGB(255, 205, 155); }

    if (!p.ground) { lxa = 1; lxb = 7; ha = 4; }
    else if (moving) { lxa = step ? 3 : 0; lxb = step ? 5 : 8; }

    PM(lxa, 14, 4, ha, jeans);                                   /* pernas (jeans) e tenis */
    PM(lxb, 14, 4, ha, jeans2);
    PM(lxa, 11 + ha, 5, 3, shoe);
    PM(lxb, 11 + ha, 5, 3, shoe);
    PM(lxa, 13 + ha, 5, 1, line);
    PM(lxb, 13 + ha, 5, 1, line);

    PM(-1, 7 + bob, 14, 9, line);                                /* moletom */
    PM(0, 8 + bob, 12, 7, body);
    PM(2, 9 + bob, 4, 2, light);
    PM(0, 14 + bob, 12, 1, dark);
    PM(3, 12 + bob, 6, 2, dark);                                 /* bolso */
    PM(1, 7 + bob, 10, 2, dark);                                 /* gola do capuz */
    PM(5, 8 + bob, 1, 3, RGB(245, 245, 245));                    /* cordoes */
    PM(7, 8 + bob, 1, 3, RGB(245, 245, 245));

    PM(0, -2 + bob, 12, 11, line);                               /* cabeca */
    PM(4, 2 + bob, 7, 6, skin);                                  /* rosto */
    PM(1, 3 + bob, 4, 5, hair);                                  /* cabelo de tras */
    PM(1, -1 + bob, 10, 4, cap);                                 /* bone */
    PM(8, 2 + bob, 5, 1, RGB(150, 25, 35));                      /* aba do bone */
    PM(4, 0 + bob, 3, 1, RGB(255, 255, 255));                    /* logo do bone */
    PM(8, 4 + bob, 1, 2, line);                                  /* olho */
    PM(8, 7 + bob, 2, 1, RGB(190, 95, 85));                      /* sorriso */

    PM(6, 9 + bob, 7, 5, line);                                  /* braco com a luva */
    PM(7, 10 + bob, 6, 3, body);
    PM(7, 10 + bob, 4, 1, light);
    PM(12, 9 + bob, 5, 5, line);
    PM(12, 10 + bob, 3, 3, glove);
    PM(15, 10 + bob, 1, 3, RGB(255, 255, 255));

    if (p.charge > 20) {                                         /* brilho da luva carregando */
        int full = (p.charge >= CHARGE_FRAMES);
        int sz = full ? 6 : 2 + (p.charge - 20) / 12;
        PM(17, 12 + bob - sz / 2, sz, sz, (full && (frame / 2) % 2) ? RGB(255, 255, 255) : glove);
    }
}
#undef PM

#define EM(lx, ly, rw, rh, c) mput(x, e->w, d, (lx), y + (ly), (rw), (rh), (c))

static void draw_enemies(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &en[i];
        float x = e->x, y = e->y;
        int d = e->dir, f = e->flash;
        u64 white = RGB(255, 255, 255);
        if (!e->active) continue;

        if (e->type == E_MET) {
            u64 c = f ? white : RGB(250, 200, 50), hi = f ? white : RGB(255, 238, 150);
            u64 sh = f ? white : RGB(200, 140, 30), line = RGB(70, 40, 10);
            if (e->st == 1) {                                    /* escondido sob o capacete */
                EM(4, 5, 6, 2, c); EM(2, 7, 10, 3, c); EM(1, 10, 12, 2, c);
                EM(4, 6, 3, 2, hi); EM(0, 12, 14, 2, sh);
            } else {
                int s = (e->st == 0 && (frame / 8) % 2);
                EM(4, 0, 6, 2, c); EM(2, 2, 10, 3, c); EM(1, 5, 12, 2, c);
                EM(4, 1, 3, 2, hi); EM(0, 7, 14, 1, sh);
                EM(2, 8, 10, 4, RGB(50, 34, 28));
                EM(3, 9, 3, 3, white); EM(8, 9, 3, 3, white);
                EM(5, 10, 1, 2, line); EM(10, 10, 1, 2, line);
                EM(2 + s, 12, 4, 2, line); EM(8 - s, 12, 4, 2, line);
            }
        } else if (e->type == E_BATTON) {
            u64 c = f ? white : RGB(150, 70, 190), wc = f ? white : RGB(110, 40, 150);
            int up = (frame / 6) % 2;
            if (up) {
                put(x, y, 4, 2, wc); put(x + 1, y + 2, 4, 2, wc);
                put(x + 10, y, 4, 2, wc); put(x + 9, y + 2, 4, 2, wc);
            } else {
                put(x, y + 6, 4, 2, wc); put(x + 1, y + 4, 4, 2, wc);
                put(x + 10, y + 6, 4, 2, wc); put(x + 9, y + 4, 4, 2, wc);
            }
            put(x + 4, y + 1, 6, 8, c);
            put(x + 4, y, 2, 2, wc); put(x + 8, y, 2, 2, wc);
            put(x + 5, y + 3, 1, 2, RGB(255, 80, 80)); put(x + 8, y + 3, 1, 2, RGB(255, 80, 80));
            put(x + 6, y + 6, 1, 2, white); put(x + 7, y + 6, 1, 2, white);
        } else {
            u64 c = f ? white : RGB(210, 50, 50), hi = f ? white : RGB(240, 110, 110);
            u64 dk = f ? white : RGB(120, 20, 20), hd = f ? white : RGB(160, 30, 30);
            u64 line = RGB(40, 6, 6), glow = RGB(255, 235, 90);
            EM(3, 22, 7, 6, dk); EM(14, 22, 7, 6, dk);          /* pernas e botas */
            EM(2, 26, 9, 2, line); EM(13, 26, 9, 2, line);
            EM(0, 8, 24, 15, line);                             /* tronco */
            EM(1, 9, 22, 13, c);
            EM(3, 10, 9, 3, hi);
            EM(1, 18, 22, 2, dk);
            EM(10, 13, 4, 4, glow);                             /* nucleo de energia */
            EM(3, -1, 18, 12, line);                            /* cabeca */
            EM(4, 0, 16, 10, hd);
            EM(3, -4, 3, 5, RGB(235, 235, 235));                /* chifres */
            EM(18, -4, 3, 5, RGB(235, 235, 235));
            EM(11, 3, 9, 3, glow);                              /* visor */
            EM(18, 12, 8, 7, line);                             /* canhao */
            EM(19, 13, 6, 5, dk);
            if (e->st == 2 && e->t > 8 && e->t < 18) EM(24, 12, 4, 7, white);
            else EM(23, 14, 3, 3, RGB(255, 160, 60));
        }
    }
}
#undef EM

static void draw_bullets_items(void)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bl[i];
        float s;
        if (!b->active) continue;
        s = (b->vx > 0) ? -1.0f : 1.0f;
        if (b->own == 0 && b->dmg == 1) {
            put(b->x - 4, b->y - 3, 8, 6, RGB(200, 120, 30));
            put(b->x - 3, b->y - 2, 6, 4, RGB(255, 240, 120));
            put(b->x - 1, b->y - 1, 3, 2, RGB(255, 255, 255));
        } else if (b->own == 0) {
            put(b->x + s * 8 - 3, b->y - 2, 6, 4, RGB(60, 150, 200));        /* rastro */
            put(b->x + s * 14 - 2, b->y - 1, 4, 2, RGB(40, 100, 160));
            put(b->x - 7, b->y - 3, 14, 6, RGB(60, 190, 240));
            put(b->x - 5, b->y - 6, 10, 12, RGB(120, 230, 255));
            put(b->x - 3, b->y - 3, 6, 6, RGB(255, 255, 255));
        } else {
            put(b->x - 4, b->y - 2, 8, 4, RGB(200, 70, 20));
            put(b->x - 2, b->y - 4, 4, 8, RGB(200, 70, 20));
            put(b->x - 3, b->y - 3, 6, 6, RGB(255, 120, 40));
            put(b->x - 1, b->y - 1, 2, 2, RGB(255, 255, 160));
        }
    }
    for (i = 0; i < MAX_ITEMS; i++)
        if (it[i].active) {
            float bob = ((frame / 10) % 2) ? 0.0f : -1.0f;
            put(it[i].x - 1, it[i].y - 1 + bob, 10, 10, RGB(120, 20, 20));
            put(it[i].x, it[i].y + bob, 8, 8, RGB(255, 255, 255));
            put(it[i].x + 3, it[i].y + 1 + bob, 2, 6, RGB(230, 40, 40));
            put(it[i].x + 1, it[i].y + 3 + bob, 6, 2, RGB(230, 40, 40));
        }
}

static void draw_bar(float x, int hp, u64 col, u64 hi)
{
    int i;
    ui(x - 3, 5, 12, MAXHP * 3 + 6, RGB(150, 160, 205));
    ui(x - 2, 6, 10, MAXHP * 3 + 4, RGB(0, 0, 0));
    ui(x, 8, 6, MAXHP * 3, RGB(36, 38, 60));
    if (hp > 0) {
        ui(x, 8 + (MAXHP - hp) * 3, 6, hp * 3, col);
        ui(x, 8 + (MAXHP - hp) * 3, 2, hp * 3, hi);
    }
    for (i = 1; i < MAXHP; i++) ui(x, 8 + i * 3 - 1, 6, 1, RGB(0, 0, 0));
}

static void draw_hud(void)
{
    char buf[32];
    draw_bar(8, p.hp, RGB(250, 240, 130), RGB(255, 255, 220));
    if (bossActive) {
        int i, hp = 0;
        for (i = 0; i < MAX_ENEMIES; i++) if (en[i].active && en[i].type == E_BOSS) hp = en[i].hp;
        draw_bar(VW - 14, hp, RGB(255, 90, 90), RGB(255, 170, 170));
    }
    snprintf(buf, sizeof(buf), "SCORE %d", score);
    text_s(26, 8, buf, 2, RGB(255, 255, 255));
    snprintf(buf, sizeof(buf), "VIDAS %d", p.lives);
    text_s(26, 20, buf, 2, RGB(255, 255, 255));
}

static void draw(void)
{
    gsKit_clear(g, RGB(0, 0, 0));

    if (state == ST_TITLE) {
        draw_background(frame * 0.7f);
        draw_floor_strip();
        text_center(46, "REIS BOT", 6, RGB(120, 230, 255));
        text_center(90, "UMA AVENTURA PARA PS2", 2, RGB(255, 255, 255));
        if ((frame / 30) % 2) text_center(140, "APERTE START", 3, RGB(250, 240, 130));
        text_center(172, "X PULA  QUADRADO ATIRA", 2, RGB(200, 200, 220));
    } else if (state == ST_OVER) {
        draw_background(cam);
        panel(40, 56, 240, 148);
        text_center(80, "GAME OVER", 5, RGB(255, 90, 90));
        text_center(140, "SCORE", 2, RGB(255, 255, 255));
        { char b[16]; snprintf(b, sizeof(b), "%d", score); text_center(158, b, 3, RGB(250, 240, 130)); }
        if ((frame / 30) % 2) text_center(186, "START PARA TENTAR DE NOVO", 2, RGB(255, 255, 255));
    } else if (state == ST_WIN) {
        draw_background(cam);
        panel(40, 56, 240, 148);
        text_center(70, "FASE COMPLETA", 4, RGB(120, 230, 255));
        text_center(130, "SCORE", 2, RGB(255, 255, 255));
        { char b[16]; snprintf(b, sizeof(b), "%d", score); text_center(148, b, 3, RGB(250, 240, 130)); }
        if ((frame / 30) % 2) text_center(186, "START PARA JOGAR DE NOVO", 2, RGB(255, 255, 255));
    } else {
        draw_background(cam);
        draw_tiles();
        draw_enemies();
        draw_bullets_items();
        draw_player();
        draw_parts();
        draw_hud();
        if (ready > 0 && (frame / 8) % 2) text_center(100, "READY", 4, RGB(255, 255, 255));
        if (paused) {
            panel(110, 90, 100, 36);
            text_center(100, "PAUSA", 4, RGB(255, 255, 255));
        }
    }

    gsKit_queue_exec(g);
    gsKit_sync_flip(g);
}

/* ---------- inicializacao ---------- */
static void init_pad(void)
{
    int s, tries = 0;
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    do { s = padGetState(0, 0); tries++; }
    while (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1 && tries < 1000000);
}

static unsigned read_pad(void)
{
    struct padButtonStatus b;
    int s = padGetState(0, 0);
    if (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1) return 0;
    if (padRead(0, 0, &b) == 0) return 0;
    return 0xffff ^ b.btns;
}

static void init_gs(void)
{
    g = gsKit_init_global_custom(256 * 1024, 1024 * 1024);   /* fila de desenho maior (muitos retangulos por quadro) */
    g->Mode = GS_MODE_NTSC;
    g->Width = 640;
    g->Height = 448;
    g->PSM = GS_PSM_CT24;
    g->PSMZ = GS_PSMZ_16S;
    g->DoubleBuffering = GS_SETTING_OFF;
    g->ZBuffering = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(g);
    gsKit_mode_switch(g, GS_ONESHOT);
}

int main(void)
{
    unsigned pad, pressed;
    init_pad();
    init_gs();
    srand(12345);
    build_level();

    for (;;) {
        pad = read_pad();
        pressed = pad & ~prevpad;
        prevpad = pad;
        frame++;
        update(pad, pressed);
        draw();
    }
    return 0;
}
