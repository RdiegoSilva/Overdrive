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
#include <libmc.h>
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
#define MW 312             /* largura da fase em tiles */
#define MH 14              /* altura da fase em tiles */
#define GOAL_X (298 * TS)  /* gatilho da cena final da Parte 1 */
#define KING_X (MW * TS - 110)   /* posicao do chefao final na cena */
#define KING_Y 136

#define GRAV 0.28f
#define MAXFALL 6.0f
#define RUN 1.6f
#define JUMP 5.6f
#define MAXHP 28
#define SLIDE 1.0f          /* velocidade de queda deslizando na parede */
#define WALLJUMP 5.4f       /* forca do pulo na parede */
#define DASHV 4.2f          /* velocidade do dash */
#define DASHT 12            /* duracao do dash (quadros) */
#define DASHCD 28           /* recarga entre dashes (quadros) */
#define KICKV 2.0f          /* impulso horizontal do pulo na parede */
#define GATE_L 261          /* portoes da arena do mini-chefe */
#define GATE_R 280
#define ARENA_X (GATE_L * TS)
#define MINI_TRIGGER (266 * TS)
#define CHARGE_FRAMES 60

#define MAX_BULLETS 24
#define MAX_ENEMIES 48
#define MAX_ITEMS 8
#define MAX_PARTS 48

#define RGB(r, g, b) GS_SETREG_RGBAQ((r), (g), (b), 0x80, 0x00)
#define RGBA(r, g, b, a) GS_SETREG_RGBAQ((r), (g), (b), (a), 0x00)   /* a: 0..128 (128 = opaco) */

enum { ST_TITLE, ST_PLAY, ST_OVER, ST_WIN, ST_INTRO, ST_CUT };
enum { E_TATU, E_BATTON, E_BOSS, E_MINI };

/* ---------- tipos ---------- */
typedef struct { float x, y, vx, vy; int w, h, dir, hp, inv, stun, ground, charge, lives, coyote, jbuf, wall, kick, kdir, wcoy, wdir, dash, dcool, tapdir, taptime; } Player;
typedef struct { int active, own, dmg; float x, y, vx, vy; } Bullet;   /* own: 0 jogador, 1 inimigo */
typedef struct { int active, type, hp, dir, t, st, w, h, flash, ground, a; float x, y, vx, vy, by; } Enemy;
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
/* menu do titulo: 0 principal, 1 opcoes, 2 creditos */
enum { MN_MAIN, MN_OPT, MN_CRED, MN_PASS };
static int menuScr = MN_MAIN, menuSel = 0;
static int optVol = 8, optMus = 8, optSfx = 8;      /* volumes 0-10 (guardados; ligar ao audsrv quando houver som) */
static int pwIdx[5] = {0, 0, 0, 0, 0}, pwPos = 0, pwErr = 0;   /* tela de senha */
static int optShake = 1, optVig = 1;                /* tremor de tela / vinheta ligados */
static int paused = 0, ready = 0, frame = 0, score = 0;
static int bossActive = 0, bossDeath = 0, shake = 0;
static int partsDone = 0;                    /* partes concluidas (0 = nenhuma) */
static int saveStatus = 0, menuMsg = 0;     /* saveStatus: 0 nada, 1 salvo, 2 falhou; menuMsg: aviso no menu */
static int hasDash = 0, dashMsg = 0;        /* dash: ganho ao derrotar o Capitao Lobo, permanente */
static int miniState = 0, wallTip = 0;      /* mini-chefe: 0 nao comecou, 1 lutando, 2 derrotado */
static float miniCam = 0;
static float cam = 0, cpx = 3 * TS;
static float bossX = 0, bossY = 0;
static int introT = 0;
static int cutT = 0, cutLine = 0, cutPhase = 0;   /* cena final */
static float cutBossY = 0;

/* spawns: tipo, coluna, linha (tatu/boss: linha do chao; batton: linha exata) */
static const int spawns[][3] = {
    {E_TATU, 18, 12}, {E_TATU, 23, 9},  {E_BATTON, 26, 5}, {E_TATU, 36, 12},
    {E_BATTON, 44, 4}, {E_TATU, 49, 12}, {E_BATTON, 64, 5}, {E_TATU, 66, 12},
    {E_TATU, 76, 8},  {E_TATU, 82, 12}, {E_BATTON, 90, 4}, {E_TATU, 93, 12},
    {E_TATU, 104, 12}, {E_BATTON, 110, 5}, {E_TATU, 116, 12}, {E_BATTON, 118, 4},
    {E_TATU, 122, 12}, {E_TATU, 128, 12}, {E_BATTON, 133, 4}, {E_TATU, 137, 12},
    {E_TATU, 144, 12}, {E_BATTON, 146, 5}, {E_TATU, 153, 8}, {E_BATTON, 157, 4},
    {E_TATU, 166, 12}, {E_BATTON, 172, 5}, {E_TATU, 181, 12}, {E_BATTON, 184, 4},
    {E_TATU, 189, 12}, {E_TATU, 196, 12}, {E_BATTON, 199, 5}, {E_TATU, 201, 12},
    {E_BATTON, 206, 4}, {E_TATU, 213, 12},
    {E_TATU, 222, 12}, {E_TATU, 236, 12}, {E_BATTON, 239, 4}, {E_TATU, 243, 12},
    {E_TATU, 284, 12}, {E_BATTON, 288, 4}, {E_TATU, 290, 12},
};
#define NSPAWN ((int)(sizeof(spawns) / sizeof(spawns[0])))

/* fonte 3x5 (A-Z, 0-9) */
static const unsigned char font[42][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
    {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},
    {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},
    {7,4,6,1,6},{3,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,6},
    {0,0,0,0,2},{0,0,0,2,4},{2,2,2,0,2},{6,1,2,0,2},{0,0,7,0,0},{0,2,0,2,0},   /* . , ! ? - : */
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
    if (shake > 0 && optShake) sx += ((frame & 1) ? 1.0f : -1.0f) * (shake > 6 ? 2.0f : 1.0f);
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
        else if (ch == '.') i = 36;
        else if (ch == ',') i = 37;
        else if (ch == '!') i = 38;
        else if (ch == '?') i = 39;
        else if (ch == '-') i = 40;
        else if (ch == ':') i = 41;
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

static unsigned hsh(unsigned n)                 /* hash barato para variar predios, janelas, tijolos */
{
    n *= 2654435761u; n ^= n >> 15; n *= 2246822519u; n ^= n >> 13;
    return n;
}

static void uiblob(float x, float y, float r, u64 c)   /* retangulo "arredondado" (coords de tela) */
{
    if (2 * r - 4 <= 0) return;
    ui(x - r, y - r + 2, 2 * r, 2 * r - 4, c);
    ui(x - r + 2, y - r, 2 * r - 4, 2, c);
    ui(x - r + 2, y + r - 2, 2 * r - 4, 2, c);
}

static void blob(float x, float y, float r, u64 c)     /* idem, no mundo */
{
    if (2 * r - 4 <= 0) return;
    put(x - r, y - r + 2, 2 * r, 2 * r - 4, c);
    put(x - r + 2, y - r, 2 * r - 4, 2, c);
    put(x - r + 2, y + r - 2, 2 * r - 4, 2, c);
}

static void glow(float x, float y, float r, int cr, int cg, int cb, int a)   /* halo de luz translucido */
{
    int k;
    for (k = 0; k < 5; k++)                                      /* aneis concentricos: degrade suave */
        blob(x, y, r * (1.0f - k * 0.18f), RGBA(cr, cg, cb, a));
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
        put(q->x - sz / 2.0f, q->y - sz / 2.0f, sz, sz, RGBA(q->r, q->g, q->b, 20 + (108 * f) / 256));
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
    fill(52, 11, 2, 1, 2);                  /* espinhos (2 tiles: pulo cobre ~4) */
    fill(62, 9, 3, 1, 1);

    fill(70, 11, 1, 1, 1);                  /* escada */
    fill(71, 10, 1, 2, 1);
    fill(72, 9, 1, 3, 1);
    fill(73, 8, 1, 4, 1);
    fill(74, 8, 5, 1, 1);

    fill(100, 9, 3, 1, 1);
    fill(105, 7, 3, 1, 1);
    fill(108, 11, 2, 1, 2);
    fill(112, 9, 1, 3, 1);

    /* ---- parte estendida (tile 120 em diante) ---- */
    fill(120, 10, 2, 2, 1);
    fill(124, 12, 3, 2, 0);
    fill(130, 11, 2, 1, 2);
    fill(135, 9, 3, 1, 1);
    fill(140, 12, 3, 2, 0);
    fill(148, 11, 1, 1, 1);                 /* escada para a plataforma alta */
    fill(149, 10, 1, 2, 1);
    fill(150, 9, 1, 3, 1);
    fill(151, 8, 6, 1, 1);
    fill(159, 12, 3, 2, 0);
    fill(164, 11, 2, 1, 2);
    fill(169, 12, 9, 2, 0);                 /* poco largo com plataformas */
    fill(170, 10, 2, 1, 1);
    fill(173, 9, 2, 1, 1);
    fill(176, 10, 2, 1, 1);
    fill(187, 10, 2, 2, 1);
    fill(192, 12, 3, 2, 0);
    fill(198, 11, 2, 1, 2);
    fill(203, 10, 3, 2, 1);
    fill(209, 12, 3, 2, 0);
    fill(215, 11, 2, 1, 2);
    /* ---- 220 em diante: aprendendo a pular na parede ---- */
    fill(226, 7, 2, 3, 1);                  /* poco A: parede esquerda (com porta embaixo) */
    fill(230, 7, 2, 5, 1);                  /* parede direita: so se passa escalando */
    fill(232, 9, 3, 1, 1);                  /* pouso depois do poco */
    fill(240, 11, 2, 1, 2);
    fill(245, 12, 3, 2, 0);
    fill(250, 3, 2, 7, 1);                  /* poco B: mais alto */
    fill(254, 3, 2, 9, 1);
    fill(256, 5, 3, 1, 1);                  /* escada de volta ao chao */
    fill(259, 8, 2, 1, 1);
    /* 261 a 280: arena do mini-chefe (portoes fecham na luta) */
    /* 292 em diante: arena plana da cena final */
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
        case E_TATU:    e->w = 14; e->h = 14; e->hp = 1;  e->y = spawns[i][2] * TS - e->h; break;
        case E_BATTON: e->w = 14; e->h = 10; e->hp = 1;  e->y = spawns[i][2] * TS; e->by = e->y; break;
        case E_BOSS:   e->w = 24; e->h = 28; e->hp = 28; e->y = spawns[i][2] * TS - e->h; break;
        }
    }
}

static void respawn_player(void)
{
    p.x = cpx; p.y = 10 * TS; p.vx = p.vy = 0;
    p.w = 12; p.h = 20; p.dir = 1; p.hp = MAXHP;
    p.inv = 0; p.stun = 0; p.ground = 0; p.charge = 0; p.coyote = 0; p.jbuf = 0;
    p.wall = p.kick = p.kdir = p.wcoy = p.wdir = 0;
    p.dash = p.dcool = p.tapdir = p.taptime = 0;
    memset(bl, 0, sizeof(bl));
    memset(it, 0, sizeof(it));
    memset(pt, 0, sizeof(pt));
    build_level();
    spawn_enemies();
    bossActive = 0; bossDeath = 0; shake = 0; miniState = 0;
    if (hasDash) {                                   /* ja derrotou o Capitao Lobo: nao repete a luta */
        miniState = 2;
        miniCam = cpx + 6 - VW / 2.0f;
        if (miniCam < 0) miniCam = 0;
        if (miniCam > MW * TS - VW) miniCam = MW * TS - VW;
    }
    ready = 90;
}

static void new_game(void)
{
    p.lives = 3; score = 0; cpx = 3 * TS; wallTip = 0; hasDash = 0; dashMsg = 0; partsDone = 0; saveStatus = 0;
    respawn_player();
    state = ST_INTRO; introT = 150; paused = 0;
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
    if (p.dcool > 0) p.dcool--;
    if (p.taptime > 0) p.taptime--;

    if (p.stun > 0) {
        p.stun--; p.wall = 0; p.kick = 0; p.dash = 0;
    } else {
        /* dash: aperte 2x rapido a direcao (qualquer momento, chao ou ar; pode estar segurando Quadrado) */
        if (hasDash && (pressed & (PAD_LEFT | PAD_RIGHT))) {
            int d = (pressed & PAD_LEFT) ? -1 : 1;
            if (p.tapdir == d && p.taptime > 0 && p.dash == 0 && p.dcool == 0) {
                p.dir = d; p.dash = DASHT; p.dcool = DASHCD; p.kick = 0; p.taptime = 0; p.tapdir = 0;
                burst(p.x + p.w / 2 - p.dir * 6, p.y + p.h / 2, 6, 140, 220, 255, 1.4f);
            } else { p.tapdir = d; p.taptime = 14; }
        }
        if (p.dash > 0) { p.dash--; p.vx = p.dir * DASHV; }
        else if (p.kick > 0) { p.kick--; p.vx = p.kdir * KICKV; p.dir = p.kdir; }   /* impulso do pulo na parede */
        else if (pad & PAD_LEFT) { p.vx = -RUN; p.dir = -1; }
        else if (pad & PAD_RIGHT) { p.vx = RUN; p.dir = 1; }
        else p.vx = 0;

        /* parede: no ar, encostado e segurando na direcao dela */
        p.wall = 0;
        if (!p.ground && p.kick == 0 && p.dash == 0) {
            if ((pad & PAD_LEFT) && hits(p.x - 1, p.y, p.w, p.h, 1)) p.wall = -1;
            else if ((pad & PAD_RIGHT) && hits(p.x + 1, p.y, p.w, p.h, 1)) p.wall = 1;
        }
        if (p.wall) { p.wcoy = 6; p.wdir = p.wall; p.dir = -p.wall; }     /* de costas pra parede: atira pro lado de fora */
        else if (p.wcoy > 0) p.wcoy--;
        if (p.ground) p.wcoy = 0;

        /* buffer de pulo + coyote time: perdoa apertos um pouco antes de pousar / depois de sair da borda */
        if (pressed & PAD_CROSS) p.jbuf = 6; else if (p.jbuf > 0) p.jbuf--;
        if (p.ground) p.coyote = 6; else if (p.coyote > 0) p.coyote--;
        if (p.jbuf > 0 && p.coyote > 0) { p.vy = -JUMP; p.ground = 0; p.coyote = 0; p.jbuf = 0; }
        else if (p.jbuf > 0 && !p.ground && p.wcoy > 0) {                 /* pulo na parede */
            p.vy = -WALLJUMP; p.kick = 8; p.kdir = -p.wdir; p.vx = p.kdir * KICKV; p.dir = p.kdir;
            p.jbuf = 0; p.wcoy = 0; p.wall = 0; wallTip = 1;
            burst(p.x + (p.wdir > 0 ? p.w : 0), p.y + p.h / 2, 5, 220, 220, 240, 1.2f);
        }
        if (!(pad & PAD_CROSS) && p.vy < -2.0f && p.kick == 0) p.vy = -2.0f;   /* pulo variavel */

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

    if (p.dash > 0 && p.vy >= 0) p.vy = 0;                            /* dash: sem gravidade (pular ainda funciona) */
    else p.vy += GRAV;
    if (p.vy > MAXFALL) p.vy = MAXFALL;
    if (p.wall && p.vy > SLIDE) p.vy = SLIDE;                         /* desliza devagar na parede */
    if (p.wall && p.vy > 0 && (frame % 5) == 0)
        burst(p.x + (p.wall > 0 ? p.w : 0), p.y + p.h - 3, 1, 210, 210, 225, 0.5f);
    was = p.ground;
    fall = p.vy;
    move_x(&p.x, p.y, p.w, p.h, &p.vx);
    p.ground = move_y(p.x, &p.y, p.w, p.h, &p.vy);
    /* o snap deixa o pe exatamente em cima do tile; sem a sonda abaixo, o flag de chao so
       ficava ligado ~1 a cada 3 quadros (a gravidade precisa acumular 1px para "bater") */
    if (!p.ground && p.vy >= 0 && hits(p.x, p.y + 1, p.w, p.h, 1)) p.ground = 1;
    if (p.ground && !was && fall > 2.5f)                    /* poeira ao pousar */
        burst(p.x + p.w / 2, p.y + p.h, 5, 200, 200, 215, 1.2f);

    if (p.ground && p.vx != 0 && (frame % 9) == 0)                  /* poeirinha ao correr */
        burst(p.x + p.w / 2 - p.dir * 5, p.y + p.h, 1, 190, 190, 210, 0.5f);

    if (hits(p.x + 2, p.y + 2, p.w - 4, p.h - 4, 2)) { p.hp = 0; kill_player(); return; }
    if (p.y > MH * TS + 20) { p.hp = 0; kill_player(); return; }

    if (p.x > 285 * TS) cpx = 285 * TS;
    else if (p.x > 248 * TS) cpx = 248 * TS;
    else if (p.x > 237 * TS) cpx = 237 * TS;
    else if (p.x > 222 * TS) cpx = 222 * TS;
    else if (p.x > 200 * TS) cpx = 200 * TS;
    else if (p.x > 180 * TS) cpx = 180 * TS;
    else if (p.x > 157 * TS) cpx = 157 * TS;
    else if (p.x > 118 * TS) cpx = 118 * TS;
    else if (p.x > 84 * TS) cpx = 84 * TS;

}

/* ---------- inimigos ---------- */
static void kill_enemy(Enemy *e)
{
    e->active = 0;
    if (e->type == E_MINI) {                                          /* Capitao Lobo derrotado */
        int ty, k;
        score += 1500; shake = 30; miniState = 2;
        if (!hasDash) { hasDash = 1; dashMsg = 260; }                 /* ganha o dash para sempre */
        burst(e->x + e->w / 2, e->y + e->h / 2, 30, 255, 200, 80, 3.0f);
        burst(e->x + e->w / 2, e->y + e->h / 2, 16, 140, 190, 255, 2.0f);
        memset(bl, 0, sizeof(bl));
        for (ty = 0; ty < 12; ty++) { map[ty][GATE_L] = 0; map[ty][GATE_R] = 0; }   /* abre os portoes */
        for (k = 0; k < 2; k++) {
            int i;
            for (i = 0; i < MAX_ITEMS; i++)
                if (!it[i].active) { it[i].active = 1; it[i].x = e->x + 6 + k * 12; it[i].y = e->y + 10; it[i].vy = -3; break; }
        }
        return;
    }
    if (e->type == E_BOSS) {
        score += 2000; bossDeath = 150; shake = 30;
        bossX = e->x; bossY = e->y;
        burst(e->x + e->w / 2, e->y + e->h / 2, 30, 255, 200, 80, 3.0f);
        memset(bl, 0, sizeof(bl));
        return;
    }
    burst(e->x + e->w / 2, e->y + e->h / 2, 12, 255, 170, 60, 2.0f);
    score += (e->type == E_TATU) ? 100 : 150;
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

    if (e->type == E_TATU) {
        if (fabsf(px - ex) > 240) return;
        if (e->st == 0) {                        /* anda */
            e->dir = (px < ex) ? -1 : 1;
            e->vx = 0.5f * e->dir;
            if (e->t > 100) { e->st = 1; e->t = 0; e->vx = 0; }
        } else if (e->st == 1) {                 /* enrolado na bola (invulneravel) */
            e->vx = 0;
            if (e->t > 50) { e->st = 2; e->t = 0; }
        } else {                                 /* desenrola e atira */
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
            if (fabsf(px - ex) < 120) { e->st = 1; e->t = 0; }
            return;
        }
        e->x += (px < ex ? -0.7f : 0.7f);                      /* mais lento */
        {   /* desce ate a altura do jogador: antes voava la em cima, fora do alcance do tiro */
            float target = p.y + 4;
            if (e->by < target - 0.6f) e->by += 0.6f;
            else if (e->by > target + 0.6f) e->by -= 0.6f;
        }
        e->y = e->by + sinf(e->t * 0.07f) * 8.0f;              /* onda menor e mais lenta */
        if (e->t > 500) e->active = 0;
    } else if (e->type == E_MINI) {              /* Capitao Lobo */
        if (miniState != 1) return;
        if (e->st == 5) {
            /* cai do alto */
        } else if (e->st == 0) {                 /* encara o jogador */
            e->dir = (px < ex) ? -1 : 1; e->vx = 0;
            if (e->t > 36) {
                e->a++;
                if (e->a % 3 == 0) { e->st = 3; e->t = 0; e->vy = -6.4f; e->vx = e->dir * 1.4f; }
                else { e->st = 1; e->t = 0; }
            }
        } else if (e->st == 1) {                 /* aviso: vai investir */
            e->vx = 0;
            if (e->t > 28) { e->st = 2; e->t = 0; e->vx = e->dir * 3.6f; }
        } else if (e->st == 2) {                 /* investida */
            if ((frame % 3) == 0) burst(e->x + e->w / 2 - e->dir * 8, e->y + e->h, 1, 210, 210, 230, 0.8f);
            if (e->t > 55) { e->st = 4; e->t = 0; e->vx = 0; }
        } else if (e->st == 3) {                 /* salta e uiva: leque de tiros */
            if (e->t == 16) {
                float dx = e->dir * 2.6f;
                fire(1, 2, ex, e->y + 12, dx, -0.9f);
                fire(1, 2, ex, e->y + 12, dx, 0.0f);
                fire(1, 2, ex, e->y + 12, dx, 0.9f);
            }
            if (e->t > 8 && e->ground) { e->st = 4; e->t = 0; e->vx = 0; }
        } else {                                 /* recupera o folego */
            e->vx = 0;
            if (e->t > 34) { e->st = 0; e->t = 0; }
        }
        e->vy += GRAV; if (e->vy > MAXFALL) e->vy = MAXFALL;
        if (move_x(&e->x, e->y, e->w, e->h, &e->vx) && e->st == 2) {
            e->st = 4; e->t = 0; shake = 10;                            /* bateu na parede */
            burst(e->x + e->w / 2 + e->dir * 10, e->y + e->h / 2, 8, 220, 220, 240, 1.6f);
        }
        e->ground = move_y(e->x, &e->y, e->w, e->h, &e->vy);
        if (e->st == 5 && e->ground && e->t > 3) {
            e->st = 0; e->t = 0; shake = 16;
            burst(e->x + e->w / 2, e->y + e->h, 10, 210, 210, 230, 1.6f);
        }
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
                if (e->type == E_MINI && e->st == 5) continue;
                if (!ov(b->x - 3, b->y - 3, 6, 6, e->x, e->y, e->w, e->h)) continue;
                b->active = 0;
                if (e->type == E_TATU && e->st == 1) {           /* tatu bola: sem dano */
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

/* ---------- cena final da Parte 1 ---------- */
typedef struct { int who; const char *a, *b; } Line;      /* who: 0 Max, 1 Rei Leao, 2 Filha */
static const Line story[] = {
    {0, "FILHA! SOLTE ELA AGORA!", ""},
    {1, "HA HA HA! EU SOU O REI LEAO.", "MEU EXERCITO DOMINA ESTE PLANETA."},
    {1, "SUA FILHA VAI COMIGO. QUER ELA DE", "VOLTA? ENFRENTE MEUS GENERAIS."},
    {2, "PAI! ME AJUDA!", ""},
    {0, "EU VOU TE SALVAR! EU PROMETO!", ""},
};
#define NSTORY ((int)(sizeof(story) / sizeof(story[0])))

static int clamp10_(int v) { return v < 0 ? 0 : (v > 10 ? 10 : v); }

/* ---------- save no memory card (slot 1) ---------- */
#ifndef MC_RD
#define MC_RD 0x0001
#define MC_WR 0x0002
#define MC_CREAT 0x0200
#define MC_TRUNC 0x0400
#endif
#define SAVE_MAGIC 0x544F4252u              /* "RBOT" */
typedef struct {
    unsigned magic;
    int ver, parts, dash, lives, score;
    int vol, mus, sfx, shake, vig;
    unsigned sum;
} SaveData;
static SaveData sd __attribute__((aligned(64)));   /* ultimo save conhecido (progresso + opcoes) */
static int mcReady = 0;

static unsigned sd_sum(const SaveData *d)
{
    const unsigned char *b = (const unsigned char *)d;
    unsigned a = 0x9E37u;
    unsigned i;
    for (i = 0; i < sizeof(SaveData) - sizeof(unsigned); i++) a = a * 31u + b[i];
    return a;
}

static void mc_start(void)
{
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    mcReady = (mcInit(MC_TYPE_MC) >= 0);
}

static int mc_card_ok(void)
{
    int type, free_, fmt, r, k;
    if (!mcReady) return 0;
    for (k = 0; k < 2; k++) {                   /* a 1a consulta so detecta o cartao */
        mcGetInfo(0, 0, &type, &free_, &fmt);
        mcSync(0, NULL, &r);
    }
    return r >= -1 && type == 2;               /* 2 = cartao de PS2 */
}

static int write_save(void)
{
    int r, fd, ok;
    if (!mc_card_ok()) return 0;
    mcMkDir(0, 0, "REISBOT"); mcSync(0, NULL, &r);          /* ja existir nao e erro */
    mcOpen(0, 0, "/REISBOT/save.bin", MC_WR | MC_CREAT | MC_TRUNC);
    mcSync(0, NULL, &fd);
    if (fd < 0) return 0;
    sd.magic = SAVE_MAGIC; sd.ver = 1;
    sd.vol = optVol; sd.mus = optMus; sd.sfx = optSfx; sd.shake = optShake; sd.vig = optVig;
    sd.sum = sd_sum(&sd);
    mcWrite(fd, &sd, sizeof(SaveData));
    mcSync(0, NULL, &r);
    ok = (r == (int)sizeof(SaveData));
    mcClose(fd); mcSync(0, NULL, &r);
    return ok;
}

static int read_save(void)                 /* 1 se leu um save valido (e aplica as opcoes) */
{
    static SaveData tmp __attribute__((aligned(64)));
    int r, fd;
    if (!mc_card_ok()) return 0;
    mcOpen(0, 0, "/REISBOT/save.bin", MC_RD);
    mcSync(0, NULL, &fd);
    if (fd < 0) return 0;
    mcRead(fd, &tmp, sizeof(SaveData));
    mcSync(0, NULL, &r);
    mcClose(fd); mcSync(0, NULL, &fd);
    if (r != (int)sizeof(SaveData) || tmp.magic != SAVE_MAGIC || tmp.ver != 1 || tmp.sum != sd_sum(&tmp)) return 0;
    sd = tmp;
    optVol = clamp10_(sd.vol); optMus = clamp10_(sd.mus); optSfx = clamp10_(sd.sfx);
    optShake = sd.shake ? 1 : 0; optVig = sd.vig ? 1 : 0;
    return 1;
}

/* fim de uma parte: mostra senha na tela de vitoria e salva o progresso no memory card */
static void finish_part(void)
{
    partsDone = 1;
    state = ST_WIN;
    sd.parts = partsDone; sd.dash = hasDash; sd.lives = (p.lives < 1) ? 3 : p.lives; sd.score = score;
    saveStatus = write_save() ? 1 : 2;
}

static void start_cut(void)
{
    state = ST_CUT; cutPhase = 0; cutT = 0; cutLine = 0; cutBossY = -70;
    p.dir = 1; p.charge = 0; p.vx = 0;
    memset(en, 0, sizeof(en));
    memset(bl, 0, sizeof(bl));
}

static void update_cut(unsigned pressed)
{
    p.charge = 0;
    update_player(0, 0);                         /* sem controle: so a fisica */
    if (state != ST_CUT) return;
    update_parts();
    cutT++;
    if (cutPhase == 0) {                         /* o chefao cai do ceu */
        cutBossY += 4.0f;
        if (cutBossY >= KING_Y) {
            cutBossY = KING_Y; cutPhase = 1; cutT = 0; shake = 24;
            burst(KING_X + 18, KING_Y + 56, 14, 200, 200, 215, 2.0f);
        }
    } else if (cutPhase == 1) {                  /* dialogo (X avanca) */
        if ((cutT > 25 && (pressed & PAD_CROSS)) || cutT > 300) {
            cutLine++; cutT = 0;
            if (cutLine >= NSTORY) cutPhase = 2;
        }
    } else {                                     /* foge levando a filha */
        cutBossY -= 1.0f + cutT * 0.08f;
        if (cutT % 6 == 0) burst(KING_X + 18, cutBossY + 56, 4, 255, 200, 120, 1.5f);
        if (cutT < 30) shake = 6;
        if (cutBossY < -80) finish_part();
    }
}

static void start_mini(void)
{
    int i, ty;
    miniState = 1; miniCam = cam;
    for (ty = 0; ty < 12; ty++) { map[ty][GATE_L] = 1; map[ty][GATE_R] = 1; }   /* fecha os portoes */
    memset(bl, 0, sizeof(bl));
    for (i = 0; i < MAX_ENEMIES; i++)
        if (!en[i].active) {
            Enemy *e = &en[i];
            memset(e, 0, sizeof(*e));
            e->active = 1; e->type = E_MINI; e->w = 24; e->h = 38; e->hp = 24; e->dir = -1;
            e->x = 274 * TS; e->y = -60; e->st = 5;
            shake = 12;
            break;
        }
}

/* ---------- senhas ---------- */
/* a senha guarda: checkpoint (0-9), se tem o dash e vidas (1-8) */
#define PW_LEN 5
static const char pwChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* 32 simbolos (sem I, O, 0, 1) */
static const int cpCols[10] = { 3, 84, 118, 157, 180, 200, 222, 237, 248, 285 };

static int cp_index(void)
{
    int i;
    for (i = 9; i > 0; i--) if ((int)cpx >= cpCols[i] * TS) return i;
    return 0;
}

static unsigned pw_hash(unsigned x) { return (x * 2654435761u + 0x1234567u) & 0x1FFFFFFu; }
static unsigned pw_chk(unsigned d) { return (d * 73u + 41u) & 0xFFu; }

static void pw_make(char *out, int cp, int dash, int lives, int parts)
{
    unsigned d, h;
    int i;
    if (lives < 1) lives = 3;
    if (lives > 8) lives = 8;
    if (parts < 0) parts = 0;
    if (parts > 7) parts = 7;
    d = (unsigned)cp | ((unsigned)(dash ? 1 : 0) << 4) | ((unsigned)(lives - 1) << 5) | ((unsigned)parts << 8);
    h = pw_hash((d << 8) | pw_chk(d));
    for (i = 0; i < PW_LEN; i++) out[PW_LEN - 1 - i] = pwChars[(h >> (5 * i)) & 31];
    out[PW_LEN] = 0;
}

static int pw_decode(const int *idx, int *cp, int *dash, int *lives, int *parts)
{
    unsigned h = 0, d;
    int i;
    for (i = 0; i < PW_LEN; i++) h = (h << 5) | (unsigned)idx[i];
    for (d = 0; d < 2048; d++) {
        if ((d & 15) > 9) continue;
        if (pw_hash((d << 8) | pw_chk(d)) == h) {
            *cp = (int)(d & 15); *dash = (int)((d >> 4) & 1); *lives = (int)((d >> 5) & 7) + 1; *parts = (int)(d >> 8);
            return 1;
        }
    }
    return 0;
}

static void current_pw(char *out)
{
    pw_make(out, (state == ST_WIN) ? 0 : cp_index(), hasDash, p.lives, partsDone);
}

static void load_password(int cp, int dash, int lives, int parts)
{
    new_game();
    hasDash = dash; dashMsg = 0;
    cpx = (float)(cpCols[cp] * TS);
    p.lives = lives;
    respawn_player();
    if (parts > 0) {                         /* parte concluida: vai para a tela de fim de parte */
        partsDone = parts; saveStatus = 0;
        state = ST_WIN;
    }
}

/* ---------- menu do titulo ---------- */
#define OPT_ITEMS 6

static void update_menu(unsigned pressed)
{
    int ok = (pressed & (PAD_CROSS | PAD_START)) != 0;
    int back = (pressed & PAD_CIRCLE) != 0;
    int n = (menuScr == MN_MAIN) ? 5 : OPT_ITEMS;

    if (pwErr > 0) pwErr--;
    if (menuMsg > 0) menuMsg--;
    if (menuScr == MN_CRED) {
        if (ok || back) { menuScr = MN_MAIN; menuSel = 4; }
        return;
    }
    if (menuScr == MN_PASS) {
        if (back) { menuScr = MN_MAIN; menuSel = 2; return; }
        if (pressed & PAD_LEFT) pwPos = (pwPos + PW_LEN - 1) % PW_LEN;
        if (pressed & PAD_RIGHT) pwPos = (pwPos + 1) % PW_LEN;
        if (pressed & PAD_UP) pwIdx[pwPos] = (pwIdx[pwPos] + 1) % 32;
        if (pressed & PAD_DOWN) pwIdx[pwPos] = (pwIdx[pwPos] + 31) % 32;
        if (ok) {
            int cp, dash, lives, parts;
            if (pw_decode(pwIdx, &cp, &dash, &lives, &parts)) load_password(cp, dash, lives, parts);
            else pwErr = 90;
        }
        return;
    }
    if (pressed & PAD_UP) menuSel = (menuSel + n - 1) % n;
    if (pressed & PAD_DOWN) menuSel = (menuSel + 1) % n;

    if (menuScr == MN_MAIN) {
        if (!ok) return;
        if (menuSel == 0) new_game();
        else if (menuSel == 1) {                                   /* continuar do save do memory card */
            if (sd.parts > 0) { load_password(0, sd.dash, sd.lives, sd.parts); score = sd.score; }
            else menuMsg = 90;
        }
        else if (menuSel == 2) { menuScr = MN_PASS; pwPos = 0; pwErr = 0; }
        else if (menuSel == 3) { menuScr = MN_OPT; menuSel = 0; }
        else { menuScr = MN_CRED; }
    } else {
        int d = ((pressed & PAD_RIGHT) ? 1 : 0) - ((pressed & PAD_LEFT) ? 1 : 0);
        if (back || (ok && menuSel == OPT_ITEMS - 1)) { menuScr = MN_MAIN; menuSel = 3; write_save(); return; }   /* grava as opcoes */
        if (menuSel == 0) optVol = clamp10_(optVol + d);
        else if (menuSel == 1) optMus = clamp10_(optMus + d);
        else if (menuSel == 2) optSfx = clamp10_(optSfx + d);
        else if (menuSel == 3 && (d || ok)) optShake = !optShake;
        else if (menuSel == 4 && (d || ok)) optVig = !optVig;
    }
}

/* ---------- loop de logica ---------- */
static void update(unsigned pad, unsigned pressed)
{
    int i;
    if (state == ST_INTRO) {
        if (--introT <= 0 || (pressed & PAD_START)) state = ST_PLAY;
        return;
    }
    if (state == ST_TITLE) { update_menu(pressed); return; }
    if (state == ST_OVER || state == ST_WIN) {
        if (pressed & PAD_START) new_game();
        return;
    }
    if (state == ST_CUT) { update_cut(pressed); goto camera; }
    if (pressed & PAD_START) paused = !paused;
    if (paused) return;
    if (shake > 0) shake--;
    if (ready > 0) { ready--; goto camera; }

    if (dashMsg > 0) dashMsg--;
    update_player(pad, pressed);
    if (state != ST_PLAY) return;
    if (p.x > GOAL_X) { start_cut(); return; }                      /* chegou na arena final */
    if (miniState == 0 && p.x > MINI_TRIGGER && p.x < (GATE_R - 2) * TS) start_mini();

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &en[i];
        if (!e->active) continue;
        update_enemy(e);
        if (!e->active) continue;
        if (e->type == E_BOSS && !bossActive) continue;
        if (e->type == E_MINI && e->st == 5) continue;
        if (ov(p.x, p.y, p.w, p.h, e->x + 1, e->y + 1, e->w - 2, e->h - 2))
            hurt_player(e->type == E_BOSS ? 4 : (e->type == E_BATTON ? 2 : 3));
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
        if (--bossDeath == 0) finish_part();
    }

camera:
    cam = p.x + p.w / 2 - VW / 2;
    if (cam < 0) cam = 0;
    if (cam > MW * TS - VW) cam = MW * TS - VW;
    if (bossActive && cam < 130 * TS) cam = 130 * TS;
    if (miniState == 1 || miniState == 2) {                         /* camera suave: trava na arena durante a luta */
        float d = ((miniState == 1) ? (float)ARENA_X : cam) - miniCam;
        if (d > 5) d = 5;
        if (d < -5) d = -5;
        miniCam += d; cam = miniCam;
    }
}

/* ---------- desenho ---------- */
static void shadow(float cx, float feet, float w)     /* sombra de contato na superficie abaixo */
{
    int tx = (int)floorf(cx / TS), ty = (int)floorf(feet / TS);
    float gy, h, sw;
    if (ty < 0) ty = 0;
    for (; ty < MH && tile_at(tx, ty) != 1; ty++) ;
    if (ty >= MH) return;
    gy = ty * TS; h = gy - feet;
    if (h < 0) h = 0;
    if (h > 48) return;
    sw = w * (1.0f - h / 72.0f);
    put(cx - sw / 2, gy - 1, sw, 2, RGBA(0, 0, 24, 80));
    put(cx - sw / 2 + 2, gy - 2, sw - 4, 1, RGBA(0, 0, 24, 40));
}

static void draw_background(float c)
{
    int i, k, j;
    float par = c * 0.4f, par2 = c * 0.15f;
    int base = (int)(par / 64), base2 = (int)(par2 / 56);

    for (i = 0; i < 28; i++) {                                   /* ceu em degrade */
        float t = i / 27.0f, t2 = t * t;
        ui(0, i * 8, VW, 8, RGB((int)(8 + 90 * t2), (int)(10 + 52 * t2), (int)(40 + 92 * t)));
    }

    for (i = 0; i < 40; i++) {                                   /* estrelas (algumas em cruz) */
        int sx = ((i * 47 + 13 - (int)(c * 0.03f)) % VW + VW) % VW;
        int sy = hsh(i + 7) % 120;
        int tw = ((frame / 18) + i) % 5;
        u64 col = (tw == 0) ? RGB(110, 110, 170) : RGB(235, 235, 255);
        ui(sx, sy, 1, 1, col);
        if (i % 7 == 0 && tw != 0) { ui(sx - 1, sy, 3, 1, col); ui(sx, sy - 1, 1, 3, col); }
    }
    {                                                            /* estrela cadente */
        int t = frame % 520;
        if (t < 22) {
            float sx = 210 - t * 7, sy = 18 + t * 2.4f;
            ui(sx, sy, 8, 1, RGB(255, 255, 255));
            ui(sx + 8, sy - 3, 12, 1, RGBA(200, 200, 255, 60));
            ui(sx + 20, sy - 7, 12, 1, RGBA(200, 200, 255, 24));
        }
    }

    for (k = 0; k < 7; k++) uiblob(258, 36, 40 - k * 3, RGBA(190, 190, 255, 3));   /* lua com halo */
    for (k = -13; k <= 13; k++) {
        int hw = (int)sqrtf((float)(14 * 14 - k * k));
        ui(258 - hw, 36 + k, 2 * hw, 1, RGB(238, 238, 205));
    }
    ui(251, 31, 4, 3, RGB(208, 208, 168)); ui(262, 40, 4, 3, RGB(208, 208, 168));
    ui(257, 27, 2, 2, RGB(208, 208, 168)); ui(264, 32, 2, 2, RGB(218, 218, 180));

    for (i = 0; i < 3; i++) {                                    /* nuvens a deriva */
        float cx = (float)((int)(i * 130 + 60 - c * 0.06f - frame * 0.12f) % (VW + 90));
        float cy = 44 + i * 26;
        if (cx < 0) cx += VW + 90;
        cx -= 50;
        ui(cx + 10, cy, 24, 3, RGBA(120, 110, 180, 44));
        ui(cx + 4, cy + 3, 38, 3, RGBA(120, 110, 180, 44));
        ui(cx, cy + 6, 46, 3, RGBA(120, 110, 180, 44));
        ui(cx + 6, cy + 9, 34, 2, RGBA(90, 84, 150, 40));
    }

    for (i = 0; i < 8; i++) {                                    /* predios distantes */
        int idx = base2 + i;
        unsigned h1 = hsh(idx * 3 + 1);
        float bx = idx * 56 - par2, bw = 34 + (h1 % 5) * 4, bh = 46 + ((h1 >> 4) % 6) * 10;
        ui(bx, 192 - bh, bw, bh, RGB(40, 42, 100));
        ui(bx, 192 - bh, bw, 1, RGB(70, 72, 140));
        for (j = 0; j < (int)((bh - 8) / 10); j++)
            for (k = 0; k < 2; k++)
                if (hsh(idx * 17 + j * 5 + k) % 5 == 0)
                    ui(bx + 8 + k * 14, 192 - bh + 6 + j * 10, 3, 3, RGB(130, 120, 165));
        if (h1 & 8) {
            ui(bx + bw / 2, 192 - bh - 8, 1, 8, RGB(40, 42, 100));
            if (((frame / 30) + idx) % 2) ui(bx + bw / 2 - 1, 192 - bh - 10, 3, 2, RGB(255, 60, 60));
        }
    }

    for (i = 0; i < 6; i++)                                      /* neblina no horizonte */
        ui(0, 150 + i * 7, VW, 7, RGBA(160, 105, 190, 5 + i * 4));

    for (i = 0; i < 7; i++) {                                    /* predios proximos */
        int idx = base + i;
        unsigned h = hsh(idx * 5 + 2);
        float bx = idx * 64 - par, bw = 44 + (h % 4) * 4, bh = 60 + ((h >> 3) % 5) * 10;
        int cols = (int)((bw - 8) / 10), rows = (int)((bh - 14) / 12);
        float top = 192 - bh;
        ui(bx, top, bw, bh, RGB(24, 30, 78));
        ui(bx, top, 2, bh, RGB(38, 46, 104));
        ui(bx + bw - 2, top, 2, bh, RGB(16, 20, 58));
        ui(bx, top, bw, 2, RGB(56, 70, 136));
        switch ((h >> 8) % 4) {
        case 0:                                                  /* caixa dagua */
            ui(bx + 8, top - 12, 14, 10, RGB(30, 36, 90)); ui(bx + 6, top - 14, 18, 3, RGB(44, 52, 112));
            ui(bx + 10, top - 2, 2, 2, RGB(20, 24, 66)); ui(bx + 18, top - 2, 2, 2, RGB(20, 24, 66));
            break;
        case 1:                                                  /* ar condicionado */
            ui(bx + 10, top - 6, 14, 6, RGB(34, 42, 100)); ui(bx + 12, top - 5, 10, 1, RGB(60, 72, 130));
            break;
        case 2:                                                  /* antena com luz */
            ui(bx + bw / 2, top - 14, 2, 14, RGB(30, 36, 90));
            if (((frame / 25) + idx) % 2) ui(bx + bw / 2 - 1, top - 17, 4, 3, RGB(255, 70, 70));
            break;
        default: break;
        }
        for (j = 0; j < rows; j++)
            for (k = 0; k < cols; k++) {
                unsigned v = hsh(idx * 131 + j * 7 + k) % 100;
                float wx = bx + 6 + k * 10, wy = top + 8 + j * 12;
                if (v < 45 && !(v == 3 && (frame / 45) % 2)) {
                    u64 wc = (v % 3 == 0) ? RGB(250, 230, 120) : (v % 3 == 1) ? RGB(255, 188, 96) : RGB(140, 215, 255);
                    ui(wx, wy, 6, 6, wc);
                    ui(wx, wy, 6, 1, RGB(255, 255, 220));
                } else {
                    ui(wx, wy, 6, 6, RGB(30, 38, 90));
                    ui(wx, wy, 6, 1, RGB(40, 50, 108));
                }
            }
    }

    {                                                            /* cerca viva atras do piso */
        float par3 = c * 0.8f;
        int b0 = (int)(par3 / 22);
        for (i = 0; i < 17; i++) {
            int idx = b0 + i;
            unsigned hh = hsh(idx * 9 + 5);
            float bx = idx * 22 - par3 + 11, rr = 8 + (hh % 4);
            for (k = 0; k < (int)rr; k += 2) {
                float dy = rr - k, hw = sqrtf(rr * rr - dy * dy);
                ui(bx - hw, 192 - rr + k, 2 * hw, 2, RGB(24, 74, 58));
                if (k >= 2) ui(bx - hw + 2, 192 - rr + k, hw * 1.1f, 2, RGB(40, 112, 82));
            }
            ui(bx - 5, 192 - rr + 1, 3, 1, RGB(92, 170, 116));
        }
    }
    {                                                            /* viga metalica no alto */
        float gp = c * 0.3f;
        int g0 = (int)(gp / 48);
        ui(0, 0, VW, 13, RGB(70, 48, 128));
        ui(0, 0, VW, 2, RGB(152, 120, 216));
        ui(0, 11, VW, 2, RGB(40, 26, 84));
        ui(0, 13, VW, 2, RGBA(10, 6, 30, 70));
        for (i = 0; i < 9; i++) {
            int idx = g0 + i;
            float gx = idx * 48 - gp;
            ui(gx, 2, 2, 9, RGB(40, 26, 84)); ui(gx + 2, 2, 1, 9, RGB(110, 84, 176));
            ui(gx + 10, 4, 6, 6, RGB(20, 60, 50)); ui(gx + 11, 5, 4, 4, RGB(80, 230, 140));
            ui(gx + 22, 4, 6, 6, RGB(20, 60, 50)); ui(gx + 23, 5, 4, 4, RGB(80, 230, 140));
            if (idx % 3 == 0)
                for (k = 0; k < 26; k++) {
                    ui(gx + k * 2, 13 + k, 6, 2, RGB(58, 40, 108));
                    ui(gx + k * 2, 13 + k, 6, 1, RGB(110, 84, 176));
                }
        }
    }

    ui(0, 192, VW, 12, RGB(14, 12, 38));                         /* abismo dos buracos */
    ui(0, 204, VW, 20, RGB(6, 6, 22));
}

/* rua: calcada em cima, asfalto embaixo; blocos acima do chao = tijolos com laje */
static void street_tile(float x, float y, int ty, unsigned h, int tx, int le, int re)
{
    int k;
    if (ty == 12) {                                              /* piso metalico: faixa xadrez em cima */
        for (k = 0; k < 2; k++) {
            u64 A = RGB(176, 232, 128), B = RGB(46, 110, 74);
            int odd = (tx * 2 + k) & 1;
            put(x + k * 8, y, 8, 2, odd ? A : B);
            put(x + k * 8, y + 2, 8, 2, odd ? B : A);
        }
        put(x, y + 4, TS, 1, RGB(214, 252, 176));
        put(x, y + 5, TS, 8, RGB(64, 146, 98));
        put(x, y + 5, TS, 1, RGB(112, 192, 134));
        put(x, y + 9, TS, 1, RGB(44, 108, 72));
        put(x, y + 13, TS, 3, RGB(34, 82, 62));
        put(x, y + 13, TS, 1, RGB(88, 168, 116));
        put(x, y + 5, 1, 11, RGB(40, 98, 68));
        put(x + 11, y + 6, 2, 2, RGB(172, 230, 178)); put(x + 12, y + 7, 1, 1, RGB(40, 98, 68));
        put(x + 11, y + 10, 2, 2, RGB(172, 230, 178)); put(x + 12, y + 11, 1, 1, RGB(40, 98, 68));
    } else {                                                     /* painel grande rebaixado (2 tiles) */
        int left = ((tx & 1) == 0);
        put(x, y, TS, TS, RGB(52, 122, 86));
        put(x, y, TS, 1, RGB(22, 58, 46));
        put(x, y + 1, TS, 1, RGB(92, 170, 122));
        put(left ? x + 2 : x, y + 4, TS - 2, 9, RGB(40, 100, 70));
        put(left ? x + 2 : x, y + 4, TS - 2, 1, RGB(26, 70, 52));
        put(left ? x + 2 : x, y + 12, TS - 2, 1, RGB(104, 180, 130));
        if (left) { put(x + 2, y + 4, 1, 9, RGB(26, 70, 52)); put(x + 1, y + 4, 1, 9, RGB(104, 180, 130)); }
        else      { put(x + TS - 3, y + 4, 1, 9, RGB(104, 180, 130)); put(x + TS - 2, y + 4, 1, 9, RGB(26, 70, 52)); }
        put(x, y + 14, TS, 2, RGB(30, 76, 58));
        if ((h & 3) == 0) put(x + 5, y + 7, 6, 2, RGB(52, 122, 86));
    }
    if (le) put(x, y, 2, TS, RGB(22, 54, 44));
    if (re) put(x + TS - 2, y, 2, TS, RGB(22, 54, 44));
}

static void draw_floor_strip(void)                               /* chao das telas de titulo/abertura */
{
    int i;
    float oc = cam; int os = shake;
    cam = 0; shake = 0;
    for (i = 0; i < VW / TS + 1; i++) {
        street_tile(i * TS, 192, 12, hsh(i * 31 + 84), i, 0, 0);
        street_tile(i * TS, 208, 13, hsh(i * 31 + 91), i, 0, 0);
    }
    cam = oc; shake = os;
}

static void draw_lamps(void)                                     /* postes de luz atras do cenario */
{
    int k, k0 = (int)(cam / TS / 11) - 1;
    for (k = k0; k <= k0 + 4; k++) {
        int col = 6 + k * 11, j;
        float x = col * TS + 8, hx = x + 12;
        if (col < 1 || col >= MW - 2) continue;
        if (tile_at(col, 12) != 1 || tile_at(col, 11) == 1 || tile_at(col + 1, 12) != 1) continue;
        for (j = 0; j < 6; j++)                                  /* cone de luz */
            put(hx - (4 + j * 4), 132 + j * 10, 8 + j * 8, 10, RGBA(255, 215, 130, 7));
        put(hx - 24, 189, 48, 3, RGBA(255, 225, 140, 14));
        put(x - 4, 186, 9, 6, RGB(34, 38, 68));                  /* base e poste */
        put(x - 1, 126, 3, 62, RGB(40, 44, 76)); put(x - 1, 126, 1, 62, RGB(76, 84, 128));
        put(x - 1, 124, 14, 3, RGB(40, 44, 76));                 /* braco */
        put(x + 8, 125, 9, 4, RGB(74, 80, 124)); put(x + 8, 125, 9, 1, RGB(120, 128, 176));
        put(x + 9, 129, 7, 2, RGB(255, 242, 180));               /* lampada */
        glow(hx, 132, 20, 255, 205, 110, 9);
    }
}

static void draw_tiles(void)
{
    int tx0 = (int)(cam / TS), tx, ty, k;
    for (tx = tx0; tx <= tx0 + VW / TS + 1 && tx < MW; tx++)
        for (ty = 0; ty < MH; ty++) {
            int t = map[ty][tx];
            float x = tx * TS, y = ty * TS;
            unsigned h = hsh(tx * 31 + ty * 7);
            if (t == 1 && ty >= 12) {
                street_tile(x, y, ty, h, tx, tile_at(tx - 1, ty) != 1, tile_at(tx + 1, ty) != 1);
            } else if (t == 1 && miniState == 1 && (tx == GATE_L || tx == GATE_R)) {   /* portao da arena */
                put(x, y, TS, TS, RGB(58, 46, 84));
                put(x, y, 2, TS, RGB(120, 104, 168)); put(x + TS - 2, y, 2, TS, RGB(30, 22, 54));
                for (k = 0; k < 4; k++) put(x + 2, y + k * 4 + 3, TS - 4, 1, RGB(30, 22, 54));
                put(x + 6, y, 4, TS, ((frame / 8) % 2) ? RGB(255, 80, 80) : RGB(170, 36, 50));
                put(x + 7, y, 2, TS, RGB(255, 190, 180));
            } else if (t == 1) {                                 /* caixote de metal rebitado */
                put(x, y, TS, TS, RGB(86, 88, 152));
                put(x, y, TS, 1, RGB(170, 174, 232)); put(x, y, 1, TS, RGB(170, 174, 232));
                put(x, y + TS - 1, TS, 1, RGB(36, 38, 86)); put(x + TS - 1, y, 1, TS, RGB(36, 38, 86));
                put(x + 1, y + 1, TS - 2, 1, RGB(124, 128, 198)); put(x + 1, y + 1, 1, TS - 2, RGB(124, 128, 198));
                put(x + 2, y + TS - 2, TS - 3, 1, RGB(58, 60, 118)); put(x + TS - 2, y + 2, 1, TS - 3, RGB(58, 60, 118));
                put(x + 4, y + 4, TS - 8, TS - 8, RGB(70, 72, 134));
                put(x + 4, y + 4, TS - 8, 1, RGB(46, 48, 98)); put(x + 4, y + 4, 1, TS - 8, RGB(46, 48, 98));
                put(x + 4, y + TS - 5, TS - 8, 1, RGB(118, 122, 192)); put(x + TS - 5, y + 4, 1, TS - 8, RGB(118, 122, 192));
                if ((h & 7) == 0)                                /* painel com listras de alerta */
                    for (k = 0; k < 4; k++) put(x + 5 + k * 2, y + 5, 1, TS - 10, RGB(240, 176, 40));
                put(x + 2, y + 2, 2, 2, RGB(210, 214, 252)); put(x + TS - 4, y + 2, 2, 2, RGB(210, 214, 252));
                put(x + 2, y + TS - 4, 2, 2, RGB(210, 214, 252)); put(x + TS - 4, y + TS - 4, 2, 2, RGB(210, 214, 252));
                if (tile_at(tx, ty - 1) != 1) {                  /* tampa no topo */
                    put(x, y, TS, 3, RGB(198, 202, 246));
                    put(x, y, TS, 1, RGB(244, 246, 255));
                    put(x, y + 3, TS, 1, RGB(58, 60, 120));
                }
            } else if (t == 2) {                                 /* espinhos metalicos */
                put(x, y + 12, TS, 4, RGB(74, 64, 90));
                put(x, y + 12, TS, 1, RGB(124, 112, 146));
                put(x + 2, y + 14, 2, 1, RGB(214, 60, 70)); put(x + 12, y + 14, 2, 1, RGB(214, 60, 70));
                for (k = 0; k < 4; k++) {
                    put(x + k * 4, y + 8, 4, 4, RGB(150, 158, 186));
                    put(x + k * 4, y + 8, 1, 4, RGB(104, 110, 140));
                    put(x + k * 4 + 1, y + 5, 2, 3, RGB(196, 204, 226));
                    put(x + k * 4 + 1, y + 2, 2, 3, RGB(240, 244, 255));
                    put(x + k * 4 + 1, y + 1, 2, 1, RGB(255, 110, 110));
                }
            }
        }
}

static void vignette(void)                                       /* escurece os cantos da tela */
{
    int k;
    if (!optVig) return;
    for (k = 0; k < 6; k++) {
        float w = 4 + k * 4;
        ui(0, 0, w, VH, RGBA(0, 0, 16, 7)); ui(VW - w, 0, w, VH, RGBA(0, 0, 16, 7));
        ui(0, 0, VW, w * 0.7f, RGBA(0, 0, 16, 6)); ui(0, VH - w * 0.7f, VW, w * 0.7f, RGBA(0, 0, 16, 6));
    }
}

static void draw_subtitle(void)
{
    const Line *l;
    static const char *const names[3] = { "MAX", "REI LEAO", "FILHA" };
    u64 col[3];
    col[0] = RGB(120, 230, 255); col[1] = RGB(255, 110, 110); col[2] = RGB(255, 150, 200);
    if (cutPhase != 1 || cutLine >= NSTORY) return;
    l = &story[cutLine];
    panel(20, 34, 280, 52);
    text_s(28, 39, names[l->who], 2, col[l->who]);
    text(28, 54, l->a, 2, RGB(255, 255, 255));
    text(28, 68, l->b, 2, RGB(255, 255, 255));
    if ((frame / 20) % 2) text(284, 74, "X", 2, RGB(250, 240, 130));
}

/* ---------- sprites em pixel art (gerados) ---------- */
typedef struct { int w, h; const char *const *rows; } Spr;

static const char *const spr_max_idle_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    "......KJJlJJjK......",
    "......KJJKKJjK......",
    "......KJJK.KjK......",
    "......KJJK.KjK......",
    ".....KWWWK.KWWK.....",
    ".....KWwwK.KwwK.....",
    ".....KkkkK.KkkkK....",
    "......KKK...KKK.....",
};
static const Spr spr_max_idle = { 20, 26, spr_max_idle_r };

static const char *const spr_max_run0_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    ".....KJJlJJjjK......",
    "....KJJJKKjjjK......",
    "...KJJJK..KjjK......",
    "..KWWWK...KjjK......",
    "..KWwwK...KWWWK.....",
    "..KkkK....KWwwK.....",
    "...KK.....KkkkK.....",
    "...........KKK......",
};
static const Spr spr_max_run0 = { 20, 26, spr_max_run0_r };

static const char *const spr_max_run1_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    "......KJJlJJjK......",
    ".....KJJKKJjjK......",
    ".....KJJK.KjjK......",
    ".....KJJK..KjjK.....",
    "....KWWWK..KWWK.....",
    "....KWwwK..KwwK.....",
    "....KkkkK..KkkK.....",
    ".....KKK....KK......",
};
static const Spr spr_max_run1 = { 20, 26, spr_max_run1_r };

static const char *const spr_max_run2_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    ".....KJJlJJjjK......",
    "......KJJKKjjjK.....",
    "......KJJK.KjjjK....",
    "......KJJK..KWWWK...",
    ".....KWWWK..KWwwK...",
    ".....KWwwK...KkkK...",
    ".....KkkkK....KK....",
    "......KKK...........",
};
static const Spr spr_max_run2 = { 20, 26, spr_max_run2_r };

static const char *const spr_max_jump_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    ".....KJJlJJjjK......",
    "....KJJJKKjjjjK.....",
    "...KJJJK..KjjjK.....",
    "..KWWWK....KWWK.....",
    "..KWwwK....KwwK.....",
    "..KkkkK....KkkK.....",
    "...KKK......KK......",
    "....................",
};
static const Spr spr_max_jump = { 20, 26, spr_max_jump_r };

static const char *const spr_tatu_walk0_r[16] = {
    "................",
    "................",
    "................",
    "................",
    "....KKKKKKK.....",
    "..KKZZVVZZVKK...",
    ".KZVVmVVmVVVmK..",
    "KZVVmVVmVVVmVK..",
    "KVVmVVmVVVmVVVK.",
    "KDDDDDDDDDDDKSSK",
    ".KsssssssssKSESK",
    ".KsssssssssKSSSK",
    "..KKKKKKKKKKKMKK",
    "..KVVK..KVVK....",
    "..KKKK..KKKK....",
    "................",
};
static const Spr spr_tatu_walk0 = { 16, 16, spr_tatu_walk0_r };

static const char *const spr_tatu_walk1_r[16] = {
    "................",
    "................",
    "................",
    "................",
    "....KKKKKKK.....",
    "..KKZZVVZZVKK...",
    ".KZVVmVVmVVVmK..",
    "KZVVmVVmVVVmVK..",
    "KVVmVVmVVVmVVVK.",
    "KDDDDDDDDDDDKSSK",
    ".KsssssssssKSESK",
    ".KsssssssssKSSSK",
    "..KKKKKKKKKKKMKK",
    ".KVVK...KVVK....",
    ".KKKK...KKKK....",
    "................",
};
static const Spr spr_tatu_walk1 = { 16, 16, spr_tatu_walk1_r };

static const char *const spr_tatu_hide_r[16] = {
    "................",
    "................",
    "................",
    "................",
    ".....KKKKKK.....",
    "...KKZZVVVVKK...",
    "..KZZVVmVVVmVK..",
    ".KZVVVmVVVVmVVK.",
    ".KVVDDDDDDDDVVK.",
    "KVVmVVVmVVVmVVVK",
    "KVVmVVVmVVVmVVVK",
    "KDDDDDDDDDDDDDDK",
    ".KVVmVVVmVVVmVK.",
    ".KVVVVmVVVVmVVK.",
    "..KKVVVVVVVVKK..",
    "....KKKKKKKK....",
};
static const Spr spr_tatu_hide = { 16, 16, spr_tatu_hide_r };

static const char *const spr_bat_up_r[10] = {
    "KK..............KK",
    "KpK....KKKK....KpK",
    "KpqK..KPPPPK..KqpK",
    "KpppKKPPqPPPKKpppK",
    ".KpnnKPTPPTPKnnpK.",
    "..KnnKPPPPPPKnnK..",
    "...KKKPWPPWPKKK...",
    ".....KPPWWPPK.....",
    "......KPPPPK......",
    ".......KKKK.......",
};
static const Spr spr_bat_up = { 18, 10, spr_bat_up_r };

static const char *const spr_bat_down_r[10] = {
    "..................",
    "......KKKKKK......",
    ".....KPPPPPPK.....",
    "KK...KPTPPTPK...KK",
    "KpK..KPPPPPPK..KpK",
    "KppKKKPWPPWPKKKppK",
    "KpnnpKKPWWPKKpnnpK",
    ".KnnnpKPPPPKpnnnK.",
    "..KnnnKKKKKKnnnK..",
    "...KKK......KKK...",
};
static const Spr spr_bat_down = { 18, 10, spr_bat_down_r };

static const char *const spr_shot_r[6] = {
    "..KKKK..",
    ".KzyyzK.",
    "KzyWWyzK",
    "KzyWWyzK",
    ".KzyyzK.",
    "..KKKK..",
};
static const Spr spr_shot = { 8, 6, spr_shot_r };

static const char *const spr_charge_r[16] = {
    "......cccc......",
    "....cccccccc....",
    "...ccCCCCCCcc...",
    "..cCCCCggCCCCc..",
    ".ccCCggggggCCcc.",
    ".cCCgggWWgggCCc.",
    "ccCCggWWWWggCCcc",
    "ccCggWWWWWWggCcc",
    "ccCggWWWWWWggCcc",
    "ccCCggWWWWggCCcc",
    ".cCCgggWWgggCCc.",
    ".ccCCggggggCCcc.",
    "..cCCCCggCCCCc..",
    "...ccCCCCCCcc...",
    "....cccccccc....",
    "......cccc......",
};
static const Spr spr_charge = { 16, 16, spr_charge_r };

static const char *const spr_orb_r[8] = {
    "..xxxx..",
    ".xzyyzx.",
    "xzyyyyzx",
    "xyyWWyyx",
    "xyyWWyyx",
    "xzyyyyzx",
    ".xzyyzx.",
    "..xxxx..",
};
static const Spr spr_orb = { 8, 8, spr_orb_r };

static const char *const spr_med_r[10] = {
    "..KKKKKK..",
    ".KWWWWWWwK",
    "KWWWRRWWwK",
    "KWWWRRWWwK",
    "KWRRRRRRwK",
    "KWRRRRRRwK",
    "KWWWRRWWwK",
    "KWWWRRWWwK",
    ".KwwwwwwwK",
    "..KKKKKK..",
};
static const Spr spr_med = { 10, 10, spr_med_r };

static u64 pal(char ch, int mode)     /* mode: 0 normal, 1/2 carregando, 3 piscada branca, 5 pulso */
{
    if (ch == '.') return 0;
    if (mode == 3) return RGB(255, 255, 255);
    if (mode == 1) { if (ch == 'O') return RGB(255, 226, 100); if (ch == 'Y') return RGB(255, 250, 200); if (ch == 'o') return RGB(222, 172, 52); }
    if (mode == 2) { if (ch == 'O') return RGB(255, 156, 88); if (ch == 'Y') return RGB(255, 208, 152); }
    if (mode == 5) { if (ch == 'g') return RGB(255, 255, 255); if (ch == 'C') return RGB(220, 252, 255); if (ch == 'c') return RGB(96, 224, 250); }
    switch (ch) {
    case 'K': return RGB(26, 14, 30);
    case 'R': return RGB(208, 44, 60);
    case 'r': return RGB(138, 24, 46);
    case 'h': return RGB(255, 120, 116);
    case 'S': return RGB(252, 208, 172);
    case 's': return RGB(214, 150, 122);
    case 'B': return RGB(98, 56, 36);
    case 'E': return RGB(22, 14, 38);
    case 'M': return RGB(188, 88, 84);
    case 'O': return RGB(240, 122, 46);
    case 'o': return RGB(176, 72, 30);
    case 'Y': return RGB(255, 178, 104);
    case 'W': return RGB(246, 246, 255);
    case 'w': return RGB(178, 184, 216);
    case 'C': return RGB(96, 224, 250);
    case 'c': return RGB(34, 140, 196);
    case 'g': return RGB(220, 252, 255);
    case 'J': return RGB(64, 94, 178);
    case 'j': return RGB(34, 56, 122);
    case 'l': return RGB(100, 132, 214);
    case 'k': return RGB(54, 46, 76);
    case 'a': return RGB(250, 204, 52);
    case 'b': return RGB(206, 142, 24);
    case 'd': return RGB(140, 84, 16);
    case 'e': return RGB(255, 244, 160);
    case 'f': return RGB(44, 30, 54);
    case 'P': return RGB(150, 72, 190);
    case 'q': return RGB(196, 120, 230);
    case 'p': return RGB(104, 40, 150);
    case 'n': return RGB(70, 24, 110);
    case 'T': return RGB(255, 70, 70);
    case 'y': return RGB(255, 232, 96);
    case 'z': return RGB(255, 150, 40);
    case 'x': return RGB(200, 60, 30);
    case 'G': return RGB(214, 160, 44);
    case 'H': return RGB(255, 232, 134);
    case 'D': return RGB(142, 92, 26);
    case 'A': return RGB(72, 56, 92);
    case 'I': return RGB(106, 86, 132);
    case 'U': return RGB(152, 130, 180);
    case 'N': return RGB(226, 112, 32);
    case 'L': return RGB(255, 168, 74);
    case 'm': return RGB(150, 64, 20);
    case 'F': return RGB(242, 190, 108);
    case 'V': return RGB(204, 140, 70);
    case 'Z': return RGB(255, 228, 172);
    case 'X': return RGB(150, 22, 44);
    case 'Q': return RGB(94, 12, 32);
    case 'i': return RGB(208, 52, 74);
    case 'v': return RGB(255, 150, 196);
    case 'u': return RGB(206, 96, 150);
    case 't': return RGB(180, 186, 208);
    case '1': return RGB(130, 136, 166);
    case '2': return RGB(82, 88, 120);
    case '3': return RGB(255, 226, 70);
    case '4': return RGB(218, 222, 236);
    case '5': return RGB(230, 60, 70);
    default: return RGB(255, 0, 255);
    }
}

static void spr(const Spr *s, float x, float y, int flip, int mode)   /* desenha sprite (coords do mundo) */
{
    int r, c, n;
    char ch;
    for (r = 0; r < s->h; r++) {
        const char *row = s->rows[r];
        for (c = 0; c < s->w; c += n) {
            ch = row[flip ? s->w - 1 - c : c];
            n = 1;
            if (ch == '.') continue;
            while (c + n < s->w && row[flip ? s->w - 1 - (c + n) : c + n] == ch) n++;
            put(x + c, y + r, n, 1, pal(ch, mode));
        }
    }
}


/* pose de parede do Max e Capitao Lobo (mini-chefe) */
static const char *const spr_max_wall_r[26] = {
    "......KKKKKK........",
    ".....KRRRhhRK.......",
    "....KRRRhRRRRRK.....",
    "....KRRRRRRRRRRKKK..",
    "....KrrrrrrrrrrRRRK.",
    "...KBBBsSSSSBBSKKKK.",
    "...KBBBsSSSSSSESK...",
    "...KBBBsSSSSSSESK...",
    "....KBBsSSSSSSMSK...",
    ".....KssSSSSSSSK....",
    "......KKYYYYYKKKKKK.",
    ".....KYYYYYYYYKKKKK.",
    ".....KYOOOOOOOOcCCgK",
    ".....KOOOYYOooocCCCK",
    ".....KOOOooOooocccKK",
    ".....KoOOOOOoooKKKK.",
    "......KooooooKK.....",
    "......KJJJJJJK......",
    ".....KJJlJJjjK......",
    "....KJJJKKjjK.......",
    "...KJJJK..KjjK......",
    "...KWWK....KjjK.....",
    "..KKWwK.....KWWK....",
    "...KkkK.....KwwK....",
    "....KK......KkkK....",
    "............KKK.....",
};
static const Spr spr_max_wall = { 20, 26, spr_max_wall_r };

static const char *const spr_wolf_r[47] = {
    ".......................K.............",
    "...........K..........K1K............",
    "..........K1K.........K1K............",
    "..........K11K.......K11K............",
    "..........K111K.....K1151K...........",
    "..........K1511K....K1551K...........",
    "...........K5511KKKKK1551K...........",
    "...........K55551ttt155551K..........",
    "...........K551tttttttt151K..........",
    "...........K5tttttttttttK1K..........",
    "...........KllllllllllKKllK..........",
    "..........KtlJJJJJJJKKJJJjK..........",
    "..........KtjjjjjjjjjjjjjjKKKKKKK....",
    "........K.Ktttttttttt333WttttttKKK...",
    ".......KlKKttttttttt13333ttttttKKK...",
    "......KlllKttttttt1111tttttttttKKK...",
    "......KlllKKttttt11111ttttttttt1K....",
    "......Klllllltt1111111ttt2222222K....",
    ".....KllllllJJ111111111111W111W1K....",
    ".....KllllJJJJJK11111llllJWJJJWK.....",
    ".....KllJJJJJJjllllllllJJJJJJjK.KKKK.",
    "....KlJJJJJJJjjjJJJJlJJJJJJJjjjK4444K",
    ".....KJJJJJjjjjJJJJJJJJtttttttttKKK4K",
    ".....KJJJjjjjjjj3333jJJt111111124444K",
    "......KJjjjjjjjj3333jjJt11111112KKK4K",
    "....KKKtjjjjjJjj3333jjjt111111124444K",
    ".KKK2ttt112lJJjjjjjjjjj222222222KKKK.",
    "K2222ttt112lJjjjjjjjjjjJjKKKKKKK.....",
    "K2222ttt112lJJJJJJJJJJJJjK...........",
    ".K22tttt112lJJJJJJJJJJJJjK...........",
    ".Kt2tttt112lJJJJJJJJJJJJjK...........",
    ".Kt2tt4444411111333311111K...........",
    "..Kttt4444212222333322222K...........",
    "..Kttt4444212222333322222K...........",
    "..Kttt4444222222333322222K...........",
    "..Kttt22222t1112KKKt1112K............",
    "...KtttKKKKt1112K.Kt1112K............",
    "...KtKK...Kt1112K.Kt1112K............",
    "....K.....Kt1112K.Kt1112K............",
    "..........Kt1112K.Kt1112K............",
    "..........Kt1112K.Kt1112KK...........",
    ".........KlllllllKllllllllK..........",
    ".........KlJJJJJjKlJJJJJJjKK.........",
    ".........KlJJJJJjKlJJJJJJ444K........",
    ".........KlJJJJJjKlJJJJJJ444K........",
    ".........KjjjjjjjKjjjjjjj444K........",
    "..........KKKKKKK.KKKKKKKKKK.........",
};
static const Spr spr_wolf = { 37, 47, spr_wolf_r };

/* Rei Leao (60x70) e a filha do Max (20x36), pixel art */
static const char *const spr_king_r[70] = {
    "............................................................",
    "...................K..K....K....K....K.....K................",
    "..................KNKKHK..KHK..KHK..KHK..KKNK...............",
    "..................KNNNHK..KHK..KHK..KHKKKNNK................",
    "...................KNHHHKKHHHKKHHHKKHHHNNNK.................",
    "...................KNHHHNKHHHKKHHHKKHHHNNNK......KK.........",
    "................KKKKHHHHHHHHHHHHHHHHHHHHNKKKK...KggK........",
    "...............KLLLKHHHHHHHHHHHHHHHHHHHHKKLLLK.KgggCK.......",
    "..............KLLLNNHGGGGGGGGTTGGGGGGGDNKLLLNNKgggCCCK......",
    "........K....KLLNNNNHGGGGGGGGTTGGGGGGGDNLLNNNNNggCCCCK......",
    ".......KNKKKKKLNNFNmDDDDDDDDDDDDDDDDDDDNLNNFNmmCCCCccNK.....",
    "........KNNNNNNNNNmmmLLLLLZZZZZZZNNNNNNNNNNNmmmCNNNNNK......",
    ".........KNNNNNNmmmmLLLLZZZZZZZZZZZNNNNNNNmmmmNNNNNNcK......",
    ".........KNNNNNNmmmLLLZZAAZZZZZZZAAFFNNNNNmmmNNNNNNNK.......",
    "..........KNNNNLLLLLLZAAZZZZZVVAAFFFFFNNNNNNNNKNNNNHHK......",
    "...........KNKLLLLLLZAZZZZZZZFAFFFFFFFFNNNNNLNmDDNDDDK......",
    "............KKLLLLLLZZZZZZZZFFFFFFFFFFFNNNNNmmmKKHDKK.......",
    "............KKLLLLLZZZgCCgFFFFFgCCgFFFFFNNmmmmmKKHDK........",
    "...........KHKLLLLLZZZCCCCFFFFFCCCCFFFVVmmmmmmmKKHDK........",
    "...........KHLLLLNNZZZFFFFFFFFFFFFFFVVVVmmmmmmmmKHDK........",
    "..........KHHHLNNNZZZZZZZFFFFFFFFFFVVVVVVmmmmmmKKHDK........",
    "..........KHHHNNNNZZZZZZZZZFFFFFFVVVVVVVmmmmmmNKKHDK........",
    "..........KHHNNNNKAAZZZZZZZZFFFVVVVVVVVVmmmmmmmNKHDK........",
    "..........KHNHNNNAAAZZZZZZZFFVVVVVVVVVVVmmmmmmmKKNDK........",
    ".........KHNHHHNZZZZZZZZZFFFFVVVVVVVVVVmmmmmmmHHHHNNK.......",
    ".......KKNHHHHHNNZZZZZZFFFFFVVVVVVVVVVVmmmLmmmHHGGGKNK......",
    "......KNNHHHHHHHNZZZZFFFFFFFVVVVVVVVVVmmmmmmmHGGGGGGKNKK....",
    ".....KNKKHHHHHGGGNZFAAAAAAAAVVVVVVVVVmmmmmmmGGGGGGGGKKKNK...",
    "......KKHHHGGGGGGGNNFWFFFWVVVVVVVVVmmmmmmmmGGGGGGGGDDK.K....",
    ".......KHGGGGGGGGGGmmWmmmWVVVVVVVmmmmmmmmmGGGGGGGDDDDK......",
    ".......KGGGGGGGGGDDDDmmmmmmmmmmmmmmmmmmmGGGGGGGDDDDDDK......",
    "........KGGDDDDDDDDDDDGmmmmmmmmmmmmmmmGGGDDDDDDDDDDDK.......",
    "........KGGUUUUUUDDDDDGGGGmmmmmmmmmGGGGGGGGDDDDDDDDDK.......",
    ".........KDUIIIIADDDDDDDDDDDDgggDDDDDDDDDDDDDDDDDDDK........",
    "....KKKKKKKUIIIIADDGGGGGGGDDgggCCDDGGGGGGGDDDDDDDADK........",
    "...KUUUUUUHHHHHHHHHHHGGGGGDggCCCCCDGGGGGGGDiUIIIIADK........",
    "...KUIIIIAGGGGGGGGGGDGGGGGDgCCCCccDGGGGGGGDiUIIIIADK........",
    "...KUAAAAAGGGGGGGGGGDGGGGGDCCCccccDGGGGGGGDiUIIIIADXK.......",
    "...KUIIIIAGGGGGGGGGGDDDDDDDDCccccDDDDDDDDDDiUIIIIADXK.......",
    "...KUIIIIAGGGGGGGGGGDGGGGGGDDcccDDGGGGGGGGDXUIIHHHHHHK......",
    "...KAAAAAADDDDDDDDDDDGGGGGGGDDDDDGGGGGGGGGDXUIIHGGGGDK......",
    "....KKKKKKKKKKKKKKHGGGGGGGGGGDDGGGGGGGGGGGDXUIIHGGGGDK......",
    ".................KHGGGGGGGGGGDDGGGGGGGGGGGDXAAAHGGGGDK......",
    ".................KHGGGGGGGGGGDDGGGGGGGGGGGDXXXXHGGGGDK......",
    ".................KHGGGGGGGGGGDDGGGGGGGGGGGDXXXXDDDDDDK......",
    ".................KIIIIIIIIIHHHHHHHIIIIIIIIIXQXXXXHDXK.......",
    ".................KIAAAAAAAAHGGGGGDAAAAAAAAAXXQXXXHDXK.......",
    ".................KIAAAAAAAAHGGCGGDAAAAAAAAAXXXXXXHDXK.......",
    ".................KIAAAAAAAAHGGGGGDAAAAAAAAAXXXXXXHDXXK......",
    ".................KAAAAAAAAADDDDDDDAAAAAAAAAXXXXXXHDXXK......",
    "..................KKUAAAAAAAAAAAAAAAAAAAiiiXXXXXXHDXXK......",
    "...................KUAAAAAAAAAAAAAAAAAAAiiiXXQXXXHDXXK......",
    "...................KUIIIIIIIAKKUIIIIIIIAiiiXXXQXXHDXXK......",
    "...................KUHHHHHHHAKKUHHHHHHHAiiiXXXXXXHDXXK......",
    "...................KUHGGGGGDAKKUHGGGGGDAiiiXXXXXXHDXXK......",
    "...................KUHGGGGGDAKKUHGGGGGDAiiXXXXXXXHDXXK......",
    "...................KUDDDDDDDAKKUDDDDDDDAiiXXXXXXXHDXXK......",
    "...................KUIIIIIIIAKKUIIIIIIIAiiXXXXQXXHDXXK......",
    "...................KUIIIIIIIAKKUIIIIIIIAiiXXXXXQXHDXXXK.....",
    "................KKKKUIIIIIIIAKKUIIIIIIIAiiXXXXXXXHDXXXK.....",
    "...............KIIIIIIIIIIIIIIIIIIIIIIIIIIXXXXXXXHDXXXK.....",
    "...............KIAAAAAAAAAAAAIAAAAAAAAAAAAQQQQQQQHDQQQK.....",
    "...............KIAAAAAAAAAAAAIAAAAAAAAAAAAQQQQQQQHDQQQK.....",
    "...............KKKKKKKKKKKKKKKKKKKKKKKKKKKQQQQQQQDDQQQK.....",
    "...............KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKQKKKKK......",
    "................KKKKKKKKKKKKKKKKKKKKKKKKKK......K...........",
    "............................................................",
    "............................................................",
    "............................................................",
    "............................................................",
};
static const Spr spr_king = { 60, 70, spr_king_r };

static const char *const spr_girl0_r[36] = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    ".....KKK.KKKK.KKK...",
    "....KRRRKBBBBKRRRK..",
    "....KRRRBBBBBBRRRK..",
    "....KBBBBBBBBBBBBK..",
    "....KBBBBBBBBBBBBK..",
    "....KBBBBBBBBBBBBK..",
    "...KBBBBBBBBBBBBBK..",
    "...KBBBBBBBBBBBBBK..",
    "...KBBBBWEBBBWEsK...",
    "...KBBBSEEBBBEEsKK..",
    "..KBBBSSEESssEEsBBK.",
    "..KBBBSSSSssssssBBK.",
    "..KBBBKScssssssBBBK.",
    "..KBBBKKcsMMssKBBBK.",
    "..KBBBKvvvMMuuuBBBK.",
    "..KBBBKSvvvuuSSBBBK.",
    "...KKSSSvvvuuSSKKK..",
    ".KKKSSSSSvvuuSSuK...",
    "KSSSSSvvvvvuuSSuK...",
    "KSSSKKvvvvvuuuuuK...",
    "KSSSKKvvvvvvuuuuK...",
    ".KKK.KvvvvvvuuuuK...",
    "....KvvvvvvvuuuuuK..",
    "....KuuuuuuuuuuuuK..",
    "....KuuuuuuuuuuuuK..",
    ".....KKSSKKKSSKKK...",
    "......KSSK.KSSK.....",
    "......KSSK.KSSK.....",
    "......KSSK.KSSK.....",
    "......KWWK.KSSK.....",
    "......KWWK.KWWK.....",
};
static const Spr spr_girl0 = { 20, 36, spr_girl0_r };

static const char *const spr_girl1_r[36] = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    ".....KKK.KKKK.KKK...",
    "....KRRRKBBBBKRRRK..",
    "....KRRRBBBBBBRRRK..",
    "....KBBBBBBBBBBBBK..",
    "....KBBBBBBBBBBBBK..",
    "....KBBBBBBBBBBBBK..",
    "...KBBBBBBBBBBBBBK..",
    ".KKKBBBBBBBBBBBBBK..",
    "KSSSBBBBWEBBBWEsKK..",
    "KSSSBBBSEEBBBEEsBBK.",
    "KSSSKKSSEESssEEsBBK.",
    ".KKSBBSSSSssssssBBK.",
    "..KBSBKScssssssBBBK.",
    "..KBBSKKcsMMssKBBBK.",
    "..KBBBSvvvMMuuuBBBK.",
    "..KBBBKSvvvuuSSKKK..",
    "..KBBBKvSvvuuSSK....",
    "...KKKvvvvvuuSSuK...",
    ".....KvvvvvuuSSuK...",
    ".....KvvvvvuuuuuK...",
    ".....KvvvvvvuuuuK...",
    ".....KvvvvvvuuuuK...",
    "....KvvvvvvvuuuuuK..",
    "....KuuuuuuuuuuuuK..",
    "....KuuuuuuuuuuuuK..",
    ".....KKSSKKKSSKKK...",
    "......KSSK.KSSK.....",
    "......KSSK.KSSK.....",
    "......KSSK.KSSK.....",
    "......KSSK.KWWK.....",
    "......KWWK.KWWK.....",
};
static const Spr spr_girl1 = { 20, 36, spr_girl1_r };

static void draw_king(float x, float y)                          /* chefao final + filha */
{
    float bob = (cutPhase != 0 && (frame / 24) % 2) ? 1.0f : 0.0f;       /* respiracao */
    float kx = x - 18, ky = y - 10 + bob;
    int gf = (frame / 8) % 2;                                            /* a filha se debate */
    shadow(kx + 30, y + 56, 50);
    spr(gf ? &spr_girl1 : &spr_girl0, kx - 9, ky + 15 + ((frame / 4) % 2 ? 1.0f : 0.0f), 0, 0);
    spr(&spr_king, kx, ky, 0, 0);
    glow(kx + 24, ky + 18, 4, 120, 240, 255, 7); glow(kx + 33.5f, ky + 18, 4, 120, 240, 255, 7);   /* olhos */
    glow(kx + 30, ky + 36, 6, 120, 240, 255, 6);                                                   /* nucleo */
    glow(kx + 49.5f, ky + 10, 9, 110, 230, 255, 8);                                                /* cristal do cetro */
}

static void draw_player(void)
{
    float x = p.x + p.w / 2.0f - 10, y = p.y + p.h - 26;       /* sprite 20x26, pe alinhado com a hitbox */
    int moving = (p.vx != 0 && p.ground);
    int mode = 0;
    const Spr *sp;

    if (p.inv > 0 && (frame / 3) % 2) return;
    shadow(p.x + p.w / 2.0f, p.y + p.h, 14);

    if (p.wall) { sp = &spr_max_wall; x += -p.wall * 2; }
    else if (!p.ground) sp = &spr_max_jump;
    else if (moving) {
        int k = (frame / 5) % 4;                                /* ciclo de corrida: 0,1,2,1 */
        sp = (k == 0) ? &spr_max_run0 : (k == 2) ? &spr_max_run2 : &spr_max_run1;
    } else sp = &spr_max_idle;

    if (p.charge >= CHARGE_FRAMES && (frame / 3) % 2) mode = 1;
    else if (p.charge > 20 && (frame / 6) % 2) mode = 2;

    if (p.dash > 0) {                                           /* rastro do dash */
        int k;
        for (k = 1; k <= 3; k++)
            put(p.x + (p.dir > 0 ? -k * 7.0f : p.w + k * 7.0f - 5), p.y + 3, 5, p.h - 6, RGBA(120, 210, 255, 110 - k * 28));
    }
    spr(sp, x, y, p.dir < 0, mode);

    if (p.charge > 20) {                                        /* energia na luva */
        int full = (p.charge >= CHARGE_FRAMES);
        int sz = full ? 6 : 2 + (p.charge - 20) / 12;
        float gx = (p.dir > 0) ? x + 20 + sz / 2.0f : x - sz / 2.0f;
        float gy = y + 12.5f;
        glow(gx, gy, full ? 14 : 8, 80, 205, 255, full ? 18 : 12);
        put(gx - sz / 2.0f, gy - sz / 2.0f, sz, sz, (full && (frame / 2) % 2) ? RGB(255, 255, 255) : RGB(96, 224, 250));
    }
}

#define EM(lx, ly, rw, rh, c) mput(x, e->w, d, (lx), y + (ly), (rw), (rh), (c))

static void draw_enemies(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &en[i];
        float x = e->x, y = e->y;
        int d = e->dir, f = e->flash;
        u64 white = RGB(255, 255, 255);
        (void)white;
        if (!e->active) continue;
        shadow(x + e->w / 2.0f, y + e->h, e->w + 2);

        if (e->type == E_TATU) {
            const Spr *ms = (e->st == 1) ? &spr_tatu_hide
                          : ((e->st == 0 && (frame / 8) % 2) ? &spr_tatu_walk1 : &spr_tatu_walk0);
            spr(ms, x + e->w / 2.0f - 8, y + e->h - 16, d < 0, f ? 3 : 0);
        } else if (e->type == E_BATTON) {
            spr(((frame / 6) % 2) ? &spr_bat_up : &spr_bat_down, x + e->w / 2.0f - 9, y, 0, f ? 3 : 0);
            glow(x + 7, y + 4, 6, 255, 60, 60, 12);
        } else if (e->type == E_MINI) {
            int tele = (e->st == 1 && (frame / 3) % 2);
            float sx = e->x + e->w / 2.0f - spr_wolf.w / 2.0f, sy = e->y + e->h - spr_wolf.h;
            if (e->st == 2) {                                               /* rastro de velocidade */
                int k;
                for (k = 0; k < 3; k++)
                    put(e->x - d * (8 + k * 9) + (d > 0 ? -6 : e->w), e->y + 6 + k * 10, 14, 2, RGBA(160, 200, 255, 70 - k * 18));
            }
            spr(&spr_wolf, sx, sy, d < 0, (f || tele) ? 3 : 0);
            if (e->st == 1) glow(e->x + e->w / 2.0f, e->y + 16, 16, 255, 80, 80, 8);
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
            glow(b->x, b->y, 10, 255, 200, 80, 16);
            put(b->x + s * 7 - 2, b->y - 1, 4, 2, RGBA(255, 200, 80, 70));
            spr(&spr_shot, b->x - 4, b->y - 3, 0, 0);
        } else if (b->own == 0) {
            glow(b->x, b->y, 18, 80, 200, 255, 16);
            put(b->x + s * 12 - 3, b->y - 3, 6, 6, RGBA(80, 200, 255, 70));
            put(b->x + s * 18 - 2, b->y - 2, 4, 4, RGBA(80, 200, 255, 40));
            spr(&spr_charge, b->x - 8, b->y - 8, 0, ((frame / 2) % 2) ? 5 : 0);
        } else {
            glow(b->x, b->y, 10, 255, 90, 40, 16);
            spr(&spr_orb, b->x - 4, b->y - 4, 0, 0);
        }
    }
    for (i = 0; i < MAX_ITEMS; i++)
        if (it[i].active) {
            float bob = ((frame / 10) % 2) ? 0.0f : -1.0f;
            glow(it[i].x + 4, it[i].y + 4 + bob, 10, 120, 255, 150, 14);
            spr(&spr_med, it[i].x - 1, it[i].y - 1 + bob, 0, 0);
        }
}

static void draw_bar(float x, int hp, u64 col, u64 hi, const char *emblem)
{
    int i, H = MAXHP * 3;
    ui(x - 3, 4, 12, H + 8, RGB(214, 220, 244));                 /* capsula metalica */
    ui(x - 4, 6, 14, H + 4, RGB(214, 220, 244));
    ui(x - 2, 5, 10, H + 6, RGB(20, 22, 44));
    ui(x - 1, 6, 8, H + 4, RGB(8, 8, 20));
    ui(x, 8, 6, H, RGB(32, 34, 58));
    if (hp > 0) {
        ui(x, 8 + (MAXHP - hp) * 3, 6, hp * 3, col);
        ui(x, 8 + (MAXHP - hp) * 3, 2, hp * 3, hi);
        ui(x + 5, 8 + (MAXHP - hp) * 3, 1, hp * 3, RGBA(0, 0, 0, 70));
    }
    for (i = 1; i < MAXHP; i++) ui(x, 8 + i * 3 - 1, 6, 1, RGB(8, 8, 20));
    ui(x - 4, 8 + H + 5, 14, 13, RGB(214, 220, 244));            /* emblema */
    ui(x - 3, 8 + H + 6, 12, 11, RGB(40, 72, 176));
    ui(x - 3, 8 + H + 6, 12, 2, RGB(96, 138, 236));
    text(x + 1, 8 + H + 9, emblem, 1, RGB(255, 255, 255));
}

static void draw_head_icon(float x, float y)                     /* carinha do Max para as vidas */
{
    ui(x, y + 1, 11, 9, RGB(40, 22, 16));
    ui(x + 1, y, 9, 4, RGB(210, 45, 55));
    ui(x + 6, y + 3, 5, 1, RGB(150, 25, 35));
    ui(x + 4, y + 4, 6, 5, RGB(245, 200, 160));
    ui(x + 1, y + 4, 3, 5, RGB(60, 36, 24));
    ui(x + 8, y + 5, 1, 2, RGB(40, 22, 16));
}

static void draw_hud(void)
{
    char buf[32];
    u64 col = RGB(250, 240, 130), hi = RGB(255, 255, 220);
    if (p.hp <= 7) { col = ((frame / 6) % 2) ? RGB(255, 70, 70) : RGB(190, 40, 50); hi = RGB(255, 170, 170); }
    else if (p.hp <= 14) { col = RGB(255, 160, 60); hi = RGB(255, 210, 150); }
    draw_bar(8, p.hp, col, hi, "M");
    if (bossActive || miniState == 1) {
        int i, hp = 0;
        for (i = 0; i < MAX_ENEMIES; i++) if (en[i].active && (en[i].type == E_BOSS || en[i].type == E_MINI)) hp = en[i].hp;
        draw_bar(VW - 14, hp, RGB(255, 90, 90), RGB(255, 170, 170), "L");
    }
    ui(24, 5, 120, 33, RGBA(0, 0, 24, 100));                      /* painel translucido */
    ui(24, 5, 120, 1, RGBA(150, 160, 205, 110)); ui(24, 37, 120, 1, RGBA(150, 160, 205, 110));
    ui(24, 5, 1, 33, RGBA(150, 160, 205, 110)); ui(143, 5, 1, 33, RGBA(150, 160, 205, 110));
    snprintf(buf, sizeof(buf), "SCORE %07d", score);
    text_s(30, 9, buf, 2, RGB(255, 255, 255));
    draw_head_icon(30, 24);
    snprintf(buf, sizeof(buf), "X %d", p.lives);
    text_s(46, 24, buf, 2, RGB(255, 255, 255));
}

static void logo(float y, const char *s, int sc)                 /* titulo com extrusao 3D */
{
    int k;
    float x = (VW - ((int)strlen(s) * 4 * sc - sc)) / 2.0f;
    for (k = 6; k >= 1; k--) text(x + k, y + k, s, sc, RGB(8 + k * 3, 18 + k * 6, 56 + k * 10));
    text(x, y, s, sc, RGB(120, 230, 255));
}

/* fundo espacial do menu: estrelas em paralaxe, planeta com anel, lua e estrela cadente */
static void space_disc(float cx, float cy, float r, int cr, int cg, int cb, int bands)
{
    int dy;
    for (dy = -(int)r; dy <= (int)r; dy++) {
        float w = sqrtf(r * r - (float)(dy * dy));
        int band = bands ? (((dy + (int)r) / 5) % 3) : 0;
        int sh = band * 14;
        ui(cx - w, cy + dy, w * 2, 1, RGB(cr - sh > 0 ? cr - sh : 0, cg - sh > 0 ? cg - sh : 0, cb - sh > 0 ? cb - sh : 0));
        ui(cx + w * 0.30f, cy + dy, w * 0.70f, 1, RGBA(0, 0, 24, 105));        /* lado na sombra */
    }
}

static void space_ring(float cx, float cy, int front)
{
    int k;
    for (k = 0; k < 120; k++) {
        float a = k * (6.2832f / 120.0f), sn = sinf(a), cs = cosf(a);
        if ((sn > 0) != (front != 0)) continue;
        ui(cx + cs * 62, cy + sn * 11 - cs * 9, 2, 2, RGB(215, 190, 150));
        ui(cx + cs * 54, cy + sn * 9.5f - cs * 8, 2, 1, RGB(160, 135, 110));
    }
}

static void draw_space_bg(void)
{
    int i;
    float oc = cam; int os = shake;
    cam = 0; shake = 0;

    for (i = 0; i < 28; i++) {                                   /* degrade: preto-azulado no topo, roxo no horizonte */
        float t = i / 27.0f;
        ui(0, i * 8, VW, 8, RGB((int)(4 + 34 * t * t), (int)(4 + 12 * t), (int)(22 + 52 * t)));
    }
    ui(0, 120, VW, 40, RGBA(120, 50, 170, 16));                  /* nebulosa */
    ui(0, 132, VW, 56, RGBA(60, 90, 200, 14));

    for (i = 0; i < 110; i++) {                                  /* estrelas: 3 camadas com velocidades diferentes */
        unsigned h = hsh(i * 7 + 3);
        int layer = i % 3;
        float speed = 0.08f + layer * 0.16f;
        int sx = (int)(((float)(h % VW) - frame * speed) );
        int sy = (int)((h >> 9) % 190);
        int tw = ((frame / (10 + layer * 6)) + i) % 7;
        u64 c;
        sx = ((sx % VW) + VW) % VW;
        c = (h & 0x300) == 0 ? RGB(190, 210, 255) : (h & 0x300) == 0x100 ? RGB(255, 245, 200) : RGB(255, 255, 255);
        if (tw == 0 && layer) continue;                          /* pisca */
        ui(sx, sy, layer == 2 ? 2 : 1, layer == 2 ? 2 : 1, c);
        if (layer == 2 && (h & 0xF000) == 0xA000) {              /* estrela brilhante em cruz */
            ui(sx - 2, sy, 6, 1, RGBA(255, 255, 255, 120));
            ui(sx, sy - 2, 1, 6, RGBA(255, 255, 255, 120));
        }
    }

    {   /* estrela cadente */
        int t = frame % 260;
        if (t < 22) {
            int k;
            float hx = 300 - t * 13.0f, hy = 18 + t * 4.0f;
            for (k = 0; k < 7; k++) ui(hx + k * 6, hy - k * 1.8f, 4, 1, RGBA(255, 255, 255, 190 - k * 26));
        }
    }

    space_disc(36, 58, 9, 175, 175, 190, 0);                     /* lua */
    ui(31, 55, 3, 2, RGB(140, 140, 156)); ui(38, 61, 3, 3, RGB(140, 140, 156)); ui(34, 63, 2, 2, RGB(140, 140, 156));

    space_ring(286, 118, 0);                                     /* anel atras do planeta */
    space_disc(286, 118, 38, 226, 150, 80, 1);                   /* planeta */
    space_ring(286, 118, 1);                                     /* anel na frente */

    cam = oc; shake = os;
}

static void draw_hero_title(float x)                             /* Max posando nas telas de titulo/abertura */
{
    Player sv = p;
    float oc = cam; int os = shake;
    cam = 0; shake = 0;
    p.x = x; p.y = 172; p.w = 12; p.h = 20; p.dir = 1; p.ground = 1;
    p.vx = 0; p.vy = 0; p.inv = 0; p.charge = 0;
    draw_player();
    p = sv; cam = oc; shake = os;
}

static void menu_item(float y, const char *str, int sc, int sel)
{
    u64 c = sel ? RGB(250, 240, 130) : RGB(255, 255, 255);
    float x = (VW - (int)strlen(str) * 4 * sc) / 2.0f - 12;
    text_center(y, str, sc, c);
    if (sel && (frame / 12) % 2 == 0) {                         /* seta */
        ui(x - 2, y + sc * 2 - 1, 2, 2, c); ui(x, y + sc * 2 - 3, 2, 6, c); ui(x + 2, y + sc * 2 - 5, 2, 10, c);
    }
}

static void draw_vol_bar(float x, float y, int v, int sel)
{
    int k;
    for (k = 0; k < 10; k++)
        ui(x + k * 7, y, 5, 8, k < v ? (sel ? RGB(250, 240, 130) : RGB(120, 230, 255)) : RGB(50, 54, 96));
}

static void draw_menu(void)
{
    static const char *const optName[OPT_ITEMS] = { "VOLUME", "MUSICA", "EFEITOS", "TREMOR", "VINHETA", "VOLTAR" };
    int i;
    if (menuScr == MN_MAIN) {
        menu_item(100, "INICIAR", 3, menuSel == 0);
        menu_item(118, "CONTINUAR", 3, menuSel == 1);
        menu_item(136, "SENHA", 3, menuSel == 2);
        menu_item(154, "OPCOES", 3, menuSel == 3);
        menu_item(172, "CREDITOS", 3, menuSel == 4);
    } else if (menuScr == MN_PASS) {
        panel(40, 50, 240, 130);
        text_center(58, "SENHA", 4, RGB(120, 230, 255));
        for (i = 0; i < PW_LEN; i++) {
            float x = 78 + i * 34;
            char ch[2];
            ch[0] = pwChars[pwIdx[i]]; ch[1] = 0;
            ui(x - 2, 96, 32, 34, (i == pwPos) ? RGB(250, 240, 130) : RGB(70, 76, 130));
            ui(x, 98, 28, 30, RGB(10, 12, 40));
            text(x + 8, 103, ch, 4, (i == pwPos) ? RGB(250, 240, 130) : RGB(255, 255, 255));
        }
        text_center(142, "CIMA BAIXO: LETRA  ESQ DIR: POSICAO", 1, RGB(200, 200, 220));
        text_center(152, "X OU START CONFIRMA", 2, RGB(255, 255, 255));
        if (pwErr > 0) text_center(166, "SENHA INVALIDA", 2, RGB(255, 90, 90));
    } else if (menuScr == MN_OPT) {
        panel(30, 46, 260, 150);
        text_center(54, "OPCOES", 3, RGB(120, 230, 255));
        for (i = 0; i < OPT_ITEMS; i++) {
            float y = 82 + i * 18;
            int sel = (menuSel == i);
            u64 c = sel ? RGB(250, 240, 130) : RGB(255, 255, 255);
            text(52, y, optName[i], 2, c);
            if (sel && (frame / 12) % 2 == 0) { ui(40, y + 3, 2, 4, c); ui(42, y + 1, 2, 8, c); }
            if (i == 0) draw_vol_bar(160, y + 1, optVol, sel);
            else if (i == 1) draw_vol_bar(160, y + 1, optMus, sel);
            else if (i == 2) draw_vol_bar(160, y + 1, optSfx, sel);
            else if (i == 3) text(160, y, optShake ? "LIGADO" : "DESLIGADO", 2, c);
            else if (i == 4) text(160, y, optVig ? "LIGADO" : "DESLIGADO", 2, c);
        }
    } else {
        panel(40, 56, 240, 116);
        text_center(66, "CREDITOS", 4, RGB(120, 230, 255));
        text_center(106, "CRIADO POR", 2, RGB(200, 200, 220));
        text_center(124, "DIEGO REIS", 4, RGB(250, 240, 130));
        if ((frame / 30) % 2) text_center(154, "X VOLTA", 2, RGB(255, 255, 255));
    }
}

static void draw(void)
{
    gsKit_clear(g, RGB(0, 0, 0));
    gsKit_set_primalpha(g, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);   /* mistura: Cs*As + Cd*(1-As) */

    if (state == ST_TITLE) {
        draw_space_bg();
        draw_floor_strip();
        draw_hero_title(40);
        logo(36, "REIS BOT", 6);
        if (menuScr == MN_MAIN) {
            if (menuMsg > 0) text_center(80, "SEM SAVE", 2, RGB(255, 90, 90));
            else text_center(80, "UMA AVENTURA PARA PS2", 2, RGB(255, 255, 255));
        }
        draw_menu();
        vignette();
    } else if (state == ST_INTRO) {
        draw_background(0);
        draw_floor_strip();
        draw_hero_title(40);
        logo(52, "PARTE 1", 6);
        text_center(100, "A RUA", 3, RGB(255, 255, 255));
        text_center(140, "CHEGUE AO FIM DA RUA", 2, RGB(200, 200, 220));
        vignette();
    } else if (state == ST_OVER) {
        draw_background(cam);
        panel(40, 56, 240, 148);
        text_center(76, "GAME OVER", 5, RGB(255, 90, 90));
        { char pw[PW_LEN + 1]; current_pw(pw); text_center(108, "SENHA", 2, RGB(200, 200, 220)); text_center(122, pw, 3, RGB(250, 240, 130)); }
        text_center(144, "SCORE", 2, RGB(255, 255, 255));
        { char b[16]; snprintf(b, sizeof(b), "%d", score); text_center(160, b, 3, RGB(250, 240, 130)); }
        if ((frame / 30) % 2) text_center(186, "START PARA TENTAR DE NOVO", 2, RGB(255, 255, 255));
    } else if (state == ST_WIN) {
        draw_background(cam);
        panel(40, 56, 240, 148);
        { char t[24]; snprintf(t, sizeof(t), "PARTE %d COMPLETA", partsDone); text_center(62, t, 3, RGB(120, 230, 255)); }
        text_center(90, "SCORE", 2, RGB(255, 255, 255));
        { char b[16]; snprintf(b, sizeof(b), "%d", score); text_center(102, b, 3, RGB(250, 240, 130)); }
        { char pw[PW_LEN + 1]; current_pw(pw); text_center(128, "SENHA", 2, RGB(200, 200, 220)); text_center(140, pw, 3, RGB(250, 240, 130)); }
        if (saveStatus == 1) text_center(160, "JOGO SALVO NO MEMORY CARD", 1, RGB(120, 255, 150));
        else if (saveStatus == 2) text_center(160, "NAO FOI POSSIVEL SALVAR", 1, RGB(255, 120, 120));
        text_center(172, "PARTE 2 EM BREVE", 2, RGB(120, 230, 255));
        if ((frame / 30) % 2) text_center(190, "START PARA JOGAR DE NOVO", 2, RGB(255, 255, 255));
    } else {
        draw_background(cam);
        draw_lamps();
        draw_tiles();
        draw_enemies();
        draw_bullets_items();
        if (state == ST_CUT) draw_king(KING_X, cutBossY);
        draw_player();
        draw_parts();
        vignette();
        if (state == ST_PLAY && !wallTip && p.x > 212 * TS && p.x < 232 * TS) {      /* dica da parede */
            panel(12, 56, 296, 40);
            text_center(63, "SEGURE PARA A PAREDE E APERTE X", 2, RGB(250, 240, 130));
            text_center(77, "PULE ENTRE AS PAREDES PARA SUBIR", 2, RGB(255, 255, 255));
        }
        if (state == ST_PLAY && dashMsg > 0 && (dashMsg > 30 || (frame / 3) % 2)) {   /* aviso do dash */
            panel(12, 56, 296, 40);
            text_center(63, "DASH CONQUISTADO!", 2, RGB(250, 240, 130));
            text_center(77, "APERTE 2X PARA FRENTE PARA USAR", 2, RGB(255, 255, 255));
        }
        if (p.inv > 84) ui(0, 0, VW, VH, RGBA(255, 40, 40, 36));       /* piscada de dano */
        if (state == ST_CUT) {                                          /* barras de cinema */
            ui(0, 0, VW, 12, RGB(0, 0, 0)); ui(0, VH - 12, VW, 12, RGB(0, 0, 0));
            draw_subtitle();
        } else draw_hud();
        if (ready > 0 && (frame / 8) % 2) text_center(100, "READY", 4, RGB(255, 255, 255));
        if (paused) {
            char pw[PW_LEN + 1];
            current_pw(pw);
            panel(80, 76, 160, 76);
            text_center(84, "PAUSA", 4, RGB(255, 255, 255));
            text_center(112, "SENHA", 2, RGB(200, 200, 220));
            text_center(126, pw, 3, RGB(250, 240, 130));
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
    mc_start();
    padInit(0);
    padPortOpen(0, 0, padBuf);
    do { s = padGetState(0, 0); tries++; }
    while (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1 && tries < 1000000);
}

static unsigned read_pad(void)
{
    static unsigned last = 0;     /* se a leitura falhar, repete o ultimo estado (nao solta botoes) */
    struct padButtonStatus b;
    int s = padGetState(0, 0);
    if (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1) return last;
    if (padRead(0, 0, &b) == 0) return last;
    last = 0xffff ^ b.btns;
    return last;
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
    g->PrimAlphaEnable = GS_SETTING_ON;                      /* permite brilhos/sombras translucidos */

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
    read_save();                 /* opcoes e progresso salvos, se houver */
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
