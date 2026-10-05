/*
 * MARIO KART - PSP EDITION
 * PSP icin sade kontrol ve performans ayarlari
 * Kontroller:
 *   Analog / D-pad : direksiyon
 *   X              : gaz
 *   Kare           : fren
 *   L / R          : drift (birakinca mini turbo)
 *   Daire          : eldeki ozel gucu kullan
 *   Ucgen          : kamera acisi degistir
 *   START          : duraklat / devam et
 *   SELECT         : bitis ekraninda yeniden baslat
 *
 *   Ozel gucler: Mantar, Turbo Yildizi, Kabuk, Muz, Yildirim, Kalkan

 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("MarioKartPSP", 0, 1, 2);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

/* ---------- HOME tusu ---------- */
static int exit_callback(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* ---------- Sabitler ---------- */
#define W 480
#define H 272
#define BUFW 512
#define PI 3.14159265f
#define DT (1.0f / 60.0f)
#define M 320            /* PSP icin yeterli pist ornek sayisi */
#define HW 11.0f         /* yol yarim genislik */
#define VMAX 52.0f
#define LAPS 3
#define NK 6             /* oyuncu + 5 bot */
#define MAXV 52000

static unsigned int __attribute__((aligned(16))) list[262144];

#define RGB(r,g,b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float wrapA(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static unsigned scol(unsigned c, float f) {
    int r = (int)((c & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)(((c >> 16) & 255) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}
static unsigned mixc(unsigned a, unsigned b, float t) {
    t = clampf(t, 0, 1);
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return RGB((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t), (int)(ab + (bb - ab) * t));
}
static unsigned rngState = 12345;
static float rnd(void) { rngState = rngState * 1664525u + 1013904223u; return ((rngState >> 8) & 0xFFFF) / 65536.0f; }

/* ---------- 2D cizim (HUD) ---------- */
typedef struct { unsigned int c; short x, y, z; } V2;
#define VF2 (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

static short cs(float v) {
    if (v != v) v = 0;
    if (v > 30000) v = 30000;
    if (v < -30000) v = -30000;
    return (short)v;
}
static void v2set(V2 *v, float x, float y, unsigned c) { v->c = c; v->x = cs(x); v->y = cs(y); v->z = 0; }
static void rect(float x, float y, float w, float h, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v2set(&v[0], x, y, c); v2set(&v[1], x + w, y + h, c);
    sceGuDrawArray(GU_SPRITES, VF2, 2, 0, v);
}
static void vgrad(float x, float y, float w, float h, unsigned top, unsigned bot) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], x, y, top); v2set(&v[1], x + w, y, top);
    v2set(&v[2], x, y + h, bot); v2set(&v[3], x + w, y + h, bot);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], xl1, y1, c); v2set(&v[1], xr1, y1, c);
    v2set(&v[2], xl2, y2, c); v2set(&v[3], xr2, y2, c);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void tri2(float x1, float y1, float x2, float y2, float x3, float y3, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(3 * sizeof(V2));
    v2set(&v[0], x1, y1, c); v2set(&v[1], x2, y2, c); v2set(&v[2], x3, y3, c);
    sceGuDrawArray(GU_TRIANGLES, VF2, 3, 0, v);
}

/* 7 segment yazi tipi */
static int maskFor(char ch) {
    switch (ch) {
    case '0': return 0x3F; case '1': return 0x06; case '2': return 0x5B; case '3': return 0x4F;
    case '4': return 0x66; case '5': return 0x6D; case '6': return 0x7D; case '7': return 0x07;
    case '8': return 0x7F; case '9': return 0x6F;
    case 'A': return 0x77; case 'C': return 0x39; case 'D': return 0x5E; case 'E': return 0x79;
    case 'F': return 0x71; case 'G': return 0x3D; case 'H': return 0x76; case 'I': return 0x06;
    case 'L': return 0x38; case 'N': return 0x54; case 'O': return 0x3F; case 'P': return 0x73;
    case 'R': return 0x50; case 'S': return 0x6D; case 'T': return 0x78; case 'U': return 0x3E;
    case 'Y': return 0x6E; case '-': return 0x40;
    }
    return 0;
}
static void glyph1(int x, int y, int w, int h, int t, int m, unsigned c) {
    if (m & 1)  rect(x, y, w, t, c);
    if (m & 2)  rect(x + w - t, y, t, h / 2 + t / 2, c);
    if (m & 4)  rect(x + w - t, y + h / 2 - t / 2, t, h - h / 2 + t / 2, c);
    if (m & 8)  rect(x, y + h - t, w, t, c);
    if (m & 16) rect(x, y + h / 2 - t / 2, t, h - h / 2 + t / 2, c);
    if (m & 32) rect(x, y, t, h / 2 + t / 2, c);
    if (m & 64) rect(x, y + h / 2 - t / 2, w, t, c);
}
static void text(int x, int y, int w, int h, int t, const char *s, unsigned c) {
    unsigned sh = RGBA(0, 0, 0, 200);
    for (; *s; s++) {
        char ch = *s;
        if (ch == ' ') { x += w; continue; }
        if (ch == '.') { rect(x + 1, y + h - t + 1, t, t, sh); rect(x, y + h - t, t, t, c); x += t + 3; continue; }
        if (ch == ':') {
            rect(x + 1, y + h / 3 + 1, t, t, sh); rect(x, y + h / 3, t, t, c);
            rect(x + 1, y + 2 * h / 3 - t + 1, t, t, sh); rect(x, y + 2 * h / 3 - t, t, t, c);
            x += t + 3; continue;
        }
        if (ch == '/') {
            trap(x + 1, x + 1 + t, y + h + 1, x + w - t + 1, x + w + 1, y + 1, sh);
            trap(x, x + t, y + h, x + w - t, x + w, y, c);
            x += w; continue;
        }
        int m = maskFor(ch);
        glyph1(x + 1, y + 1, w, h, t, m, sh);
        glyph1(x, y, w, h, t, m, c);
        x += w + t + 3;
    }
}
static int textWidth(const char *s, int w, int t) {
    int x = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '/') x += w;
        else if (*s == '.' || *s == ':') x += t + 3;
        else x += w + t + 3;
    }
    return x;
}

/* ---------- 3D mesh deposu ---------- */
typedef struct { unsigned c; float x, y, z; } Vtx;
#define VF3 (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
typedef struct { float x, y, z; } P3;

static Vtx __attribute__((aligned(16))) mesh[MAXV];
static int nv = 0;
static P3 mk(float x, float y, float z) { P3 p; p.x = x; p.y = y; p.z = z; return p; }
static void pv(P3 p, unsigned c) {
    if (nv < MAXV) { mesh[nv].c = c; mesh[nv].x = p.x; mesh[nv].y = p.y; mesh[nv].z = p.z; nv++; }
}
static void tri3(P3 a, P3 b, P3 c, unsigned col) {
    if (nv + 3 > MAXV) return;
    pv(a, col); pv(b, col); pv(c, col);
}
static void quad3(P3 a, P3 b, P3 c, P3 d, unsigned col) { tri3(a, b, c, col); tri3(a, c, d, col); }

/* yone gore donmus kutu (f = ileri yonu xz) */
static void boxO(float cx, float cy, float cz, float fx, float fz, float hx, float hy, float hz, unsigned c) {
    float rx = -fz, rz = fx;
#define BP(sx, sy, sz) mk(cx + fx * hx * (sx) + rx * hz * (sz), cy + hy * (sy), cz + fz * hx * (sx) + rz * hz * (sz))
    quad3(BP(-1, 1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 1.00f));
    quad3(BP(1, -1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(1, -1, 1), scol(c, 0.82f));
    quad3(BP(-1, -1, -1), BP(-1, 1, -1), BP(-1, 1, 1), BP(-1, -1, 1), scol(c, 0.60f));
    quad3(BP(-1, -1, 1), BP(1, -1, 1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 0.90f));
    quad3(BP(-1, -1, -1), BP(1, -1, -1), BP(1, 1, -1), BP(-1, 1, -1), scol(c, 0.70f));
#undef BP
}
static void box3(float cx, float cy, float cz, float hx, float hy, float hz, unsigned c) {
    boxO(cx, cy, cz, 1, 0, hx, hy, hz, c);
}
static void cone3(float cx, float cy, float cz, float r, float h, int sides, float rot, unsigned c) {
    P3 apex = mk(cx, cy + h, cz);
    for (int i = 0; i < sides; i++) {
        float a0 = rot + i * 2 * PI / sides, a1 = rot + (i + 1) * 2 * PI / sides;
        float am = (a0 + a1) * 0.5f;
        float sh = 0.62f + 0.38f * (0.5f + 0.5f * cosf(am - 0.8f));
        tri3(mk(cx + cosf(a0) * r, cy, cz + sinf(a0) * r), mk(cx + cosf(a1) * r, cy, cz + sinf(a1) * r), apex, scol(c, sh));
    }
}

/* ---------- Pist ---------- */
static float tcx[M], tcz[M], tfx[M], tfz[M], tsd[M], tth[M];
static float TL = 0;
static float mapMinX, mapMaxX, mapMinZ, mapMaxZ;

#define NP 12
static const float ctrl[NP][2] = {
    {0, 0}, {160, 0}, {300, 60}, {360, 180}, {300, 300}, {160, 330},
    {60, 260}, {-40, 330}, {-180, 300}, {-260, 180}, {-200, 60}, {-100, 40} };

static void catmull(float p0, float p1, float p2, float p3, float t, float *o) {
    float t2 = t * t, t3 = t2 * t;
    *o = 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

#define DENSE_PER 40
#define KD (NP * DENSE_PER)
static void buildTrackPath(void) {
    static float dx[KD], dz[KD], cum[KD + 1];
    const float SC = 1.3f;
    int k = 0;
    for (int i = 0; i < NP; i++) {
        const float *p0 = ctrl[(i + NP - 1) % NP], *p1 = ctrl[i], *p2 = ctrl[(i + 1) % NP], *p3 = ctrl[(i + 2) % NP];
        for (int j = 0; j < DENSE_PER; j++) {
            float t = (float)j / DENSE_PER, x, z;
            catmull(p0[0], p1[0], p2[0], p3[0], t, &x);
            catmull(p0[1], p1[1], p2[1], p3[1], t, &z);
            dx[k] = x * SC; dz[k] = z * SC; k++;
        }
    }
    cum[0] = 0;
    for (int i = 0; i < KD; i++) {
        int j = (i + 1) % KD;
        cum[i + 1] = cum[i] + sqrtf((dx[j] - dx[i]) * (dx[j] - dx[i]) + (dz[j] - dz[i]) * (dz[j] - dz[i]));
    }
    TL = cum[KD];
    int ptr = 0;
    for (int m = 0; m < M; m++) {
        float target = TL * m / M;
        while (ptr < KD - 1 && cum[ptr + 1] < target) ptr++;
        int j = (ptr + 1) % KD;
        float seglen = cum[ptr + 1] - cum[ptr];
        float u = seglen > 0.0001f ? (target - cum[ptr]) / seglen : 0;
        tcx[m] = dx[ptr] + (dx[j] - dx[ptr]) * u;
        tcz[m] = dz[ptr] + (dz[j] - dz[ptr]) * u;
        tsd[m] = target;
    }
    mapMinX = mapMinZ = 1e9f; mapMaxX = mapMaxZ = -1e9f;
    for (int m = 0; m < M; m++) {
        int a = (m + M - 1) % M, b = (m + 1) % M;
        float fx = tcx[b] - tcx[a], fz = tcz[b] - tcz[a];
        float l = sqrtf(fx * fx + fz * fz);
        if (l < 0.0001f) l = 1;
        tfx[m] = fx / l; tfz[m] = fz / l; tth[m] = atan2f(tfz[m], tfx[m]);
        if (tcx[m] < mapMinX) mapMinX = tcx[m];
        if (tcx[m] > mapMaxX) mapMaxX = tcx[m];
        if (tcz[m] < mapMinZ) mapMinZ = tcz[m];
        if (tcz[m] > mapMaxZ) mapMaxZ = tcz[m];
    }
}

static int nFlat = 0, nScene = 0;
static int kartStart[NK], kartCount = 0;
static int boxStart, boxCount, coneStart, coneCount, flameStart, flameCount, shadowStart, shadowCount;
static int sparkStart, sparkCount;
static unsigned char padMark[M];

static const unsigned kBody[NK] = { RGB(225, 35, 35), RGB(40, 90, 235), RGB(30, 175, 70), RGB(245, 205, 25), RGB(160, 70, 205), RGB(245, 135, 25) };
static const unsigned kTrim[NK] = { RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(40, 40, 40), RGB(255, 255, 255), RGB(40, 40, 40) };


/* Low-poly cylinder with its axis on Z. 10 sides keeps the PSP fill-rate friendly. */
static void cylZ(float cx, float cy, float cz, float r, float hz, int sides, unsigned c) {
    int n = sides < 6 ? 6 : (sides > 12 ? 12 : sides);
    for (int i = 0; i < n; i++) {
        float a0 = i * 2 * PI / n, a1 = (i + 1) * 2 * PI / n;
        float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r;
        float x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
        /* tyre sidewall */
        tri3(mk(x0, y0, cz - hz), mk(x1, y1, cz - hz), mk(x1, y1, cz + hz), scol(c, 0.78f));
        tri3(mk(x0, y0, cz - hz), mk(x1, y1, cz + hz), mk(x0, y0, cz + hz), scol(c, 0.94f));
        /* hub faces */
        tri3(mk(cx, cy, cz + hz), mk(x1, y1, cz + hz), mk(x0, y0, cz + hz), scol(c, 1.08f));
        tri3(mk(cx, cy, cz - hz), mk(x0, y0, cz - hz), mk(x1, y1, cz - hz), scol(c, 0.62f));
    }
}

/* A slightly more sculpted kart: layered body, proper tyres, spoiler and cockpit. */
static void buildKartMesh(unsigned body, unsigned trim) {
    unsigned dark = RGB(18, 20, 25), tyre = RGB(22, 23, 28);
    unsigned metal = RGB(165, 172, 185), glass = RGB(55, 100, 135);
    unsigned skin = RGB(255, 208, 165), glow = RGB(255, 235, 110);

    /* Main chassis and layered nose. */
    box3(0.0f, 0.58f, 0, 1.65f, 0.30f, 0.78f, body);
    boxO(0.95f, 0.82f, 0, 1, 0, 0.72f, 0.18f, 0.70f, scol(body, 1.08f));
    boxO(1.72f, 0.60f, 0, 1, 0, 0.48f, 0.18f, 0.62f, body);
    boxO(2.25f, 0.43f, 0, 1, 0, 0.28f, 0.10f, 0.92f, trim);

    /* Side pods / intake blocks. */
    box3(0.15f, 0.52f, 1.00f, 0.95f, 0.22f, 0.18f, trim);
    box3(0.15f, 0.52f, -1.00f, 0.95f, 0.22f, 0.18f, trim);
    box3(-0.55f, 0.78f, 0.78f, 0.45f, 0.18f, 0.12f, dark);
    box3(-0.55f, 0.78f, -0.78f, 0.45f, 0.18f, 0.12f, dark);

    /* Four chunky tyres with metallic hubs. */
    for (int i = 0; i < 2; i++) {
        float x = i ? 1.12f : -1.15f;
        cylZ(x, 0.43f, 1.06f, 0.48f, 0.20f, 10, tyre);
        cylZ(x, 0.43f, -1.06f, 0.48f, 0.20f, 10, tyre);
        cylZ(x, 0.43f, 1.29f, 0.23f, 0.035f, 10, metal);
        cylZ(x, 0.43f, -1.29f, 0.23f, 0.035f, 10, metal);
    }

    /* Rear diffuser, front splitter and exhausts. */
    box3(-1.72f, 0.38f, 0, 0.38f, 0.10f, 0.86f, dark);
    box3(2.42f, 0.30f, 0, 0.12f, 0.06f, 1.05f, metal);
    box3(-1.85f, 0.64f, 0.40f, 0.18f, 0.12f, 0.10f, glow);
    box3(-1.85f, 0.64f, -0.40f, 0.18f, 0.12f, 0.10f, glow);

    /* Cockpit, windscreen and driver. */
    box3(-0.18f, 1.02f, 0, 0.42f, 0.34f, 0.42f, trim);
    boxO(-0.25f, 1.30f, 0, 1, 0, 0.34f, 0.22f, 0.38f, glass);
    box3(-0.22f, 1.62f, 0, 0.27f, 0.25f, 0.27f, skin);
    box3(-0.22f, 1.86f, 0, 0.32f, 0.18f, 0.32f, body);
    box3(0.08f, 1.62f, 0, 0.045f, 0.11f, 0.23f, dark);
    box3(0.22f, 1.05f, 0.40f, 0.26f, 0.07f, 0.07f, skin);
    box3(0.22f, 1.05f, -0.40f, 0.26f, 0.07f, 0.07f, skin);

    /* Large rear wing and supports. */
    box3(-1.55f, 1.45f, 0, 0.15f, 0.07f, 1.12f, trim);
    box3(-1.55f, 1.18f, 0.72f, 0.08f, 0.28f, 0.08f, dark);
    box3(-1.55f, 1.18f, -0.72f, 0.08f, 0.28f, 0.08f, dark);
    box3(-1.55f, 1.67f, 0, 0.20f, 0.08f, 1.22f, body);
}

static void buildSparkMesh(void) {
    sparkStart = nv;
    P3 a = mk(0, 0.9f, 0);
    P3 b = mk(-0.42f, -0.25f, 0);
    P3 c = mk(0.42f, -0.25f, 0);
    P3 d = mk(0, 0, 0.55f);
    P3 e = mk(0, 0, -0.55f);
    unsigned q = RGB(255, 220, 70);
    tri3(a, b, d, q); tri3(a, d, c, q);
    tri3(a, c, e, q); tri3(a, e, b, q);
    sparkCount = nv - sparkStart;
}

static void buildMesh(void) {
    nv = 0;
    unsigned G1 = RGB(34, 150, 48), G2 = RGB(28, 138, 42);
    /* --- duz katmanlar (derinlik testsiz, sirayla) --- */
    /* zemin */
    const int GN = 24; const float GS = 125.0f;
    for (int i = 0; i < GN; i++) for (int j = 0; j < GN; j++) {
        float x0 = -1500 + i * GS, z0 = -1500 + j * GS;
        quad3(mk(x0, 0, z0), mk(x0 + GS, 0, z0), mk(x0 + GS, 0, z0 + GS), mk(x0, 0, z0 + GS), ((i + j) & 1) ? G1 : G2);
    }
    /* cim seritleri (yolun yani) */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 4) & 1) ? RGB(54, 180, 66) : RGB(44, 166, 58);
        float a = HW + 1.8f, b = HW + 14.0f;
        quad3(mk(tcx[k] + rx0 * a, 0.0f, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0.0f, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0.0f, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0.0f, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0.0f, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0.0f, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0.0f, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0.0f, tcz[k2] - rz1 * a), c);
    }
    /* rumble */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 2) & 1) ? RGB(225, 40, 40) : RGB(245, 245, 245);
        float a = HW, b = HW + 1.8f;
        quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0, tcz[k2] - rz1 * a), c);
    }
    /* yol */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 3) & 1) ? RGB(96, 96, 102) : RGB(106, 106, 112);
        quad3(mk(tcx[k] - rx0 * HW, 0, tcz[k] - rz0 * HW), mk(tcx[k] + rx0 * HW, 0, tcz[k] + rz0 * HW),
              mk(tcx[k2] + rx1 * HW, 0, tcz[k2] + rz1 * HW), mk(tcx[k2] - rx1 * HW, 0, tcz[k2] - rz1 * HW), c);
    }
    /* turbo seritleri */
    memset(padMark, 0, sizeof(padMark));
    for (int q = 1; q < 10; q++) {
        int b = M * q / 10 + 7;
        for (int j = 0; j < 7; j++) {
            int k = (b + j) % M, k2 = (k + 1) % M;
            padMark[k] = 1;
            float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
            float w = HW * 0.4f;
            unsigned c = (j & 1) ? RGB(255, 215, 30) : RGB(255, 120, 20);
            quad3(mk(tcx[k] - rx0 * w, 0, tcz[k] - rz0 * w), mk(tcx[k] + rx0 * w, 0, tcz[k] + rz0 * w),
                  mk(tcx[k2] + rx1 * w, 0, tcz[k2] + rz1 * w), mk(tcx[k2] - rx1 * w, 0, tcz[k2] - rz1 * w), c);
        }
    }
    /* seritler */
    for (int k = 0; k < M; k++) {
        if ((k % 4) > 1) continue;
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int ln = -1; ln <= 1; ln += 2) {
            float o = ln * HW / 3.0f, w = 0.22f;
            quad3(mk(tcx[k] + rx0 * (o - w), 0, tcz[k] + rz0 * (o - w)), mk(tcx[k] + rx0 * (o + w), 0, tcz[k] + rz0 * (o + w)),
                  mk(tcx[k2] + rx1 * (o + w), 0, tcz[k2] + rz1 * (o + w)), mk(tcx[k2] + rx1 * (o - w), 0, tcz[k2] + rz1 * (o - w)), RGB(240, 240, 240));
        }
    }
    /* baslangic cizgisi */
    for (int row = 0; row < 2; row++) {
        int k = row, k2 = row + 1;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int i = 0; i < 8; i++) {
            float a = -HW + i * (2 * HW / 8.0f), b = a + 2 * HW / 8.0f;
            quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
                  mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a),
                  ((i + row) & 1) ? RGB(250, 250, 250) : RGB(20, 20, 20));
        }
    }
    nFlat = nv;

    /* --- sahne objeleri --- */
    /* kapi */
    {
        float fx = tfx[0], fz = tfz[0], rx = -fz, rz = fx;
        float px0 = tcx[0], pz0 = tcz[0];
        float off = HW + 2.0f;
        boxO(px0 + rx * off, 4.0f, pz0 + rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0 - rx * off, 4.0f, pz0 - rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0, 8.4f, pz0, fx, fz, 0.9f, 0.9f, off + 0.6f, RGB(220, 40, 40));
        boxO(px0, 8.4f, pz0, fx, fz, 0.95f, 0.35f, off * 0.7f, RGB(250, 250, 250));
    }
    /* agaclar */
    for (int k = 0; k < M; k += 2) {
        for (int side = -1; side <= 1; side += 2) {
            if (rnd() < 0.3f) continue;
            float lat = side * (HW + 15.0f + rnd() * 18.0f);
            float x = tcx[k] - tfz[k] * lat, z = tcz[k] + tfx[k] * lat;
            float s = 0.8f + rnd() * 0.9f;
            int pine = rnd() < 0.7f;
            box3(x, 1.2f * s, z, 0.35f * s, 1.2f * s, 0.35f * s, RGB(110, 72, 40));
            unsigned leaf = pine ? RGB(20, 110 + (int)(rnd() * 40), 45) : RGB(70, 160 + (int)(rnd() * 40), 50);
            cone3(x, 2.0f * s, z, 2.2f * s, 4.2f * s, 6, rnd() * 3, leaf);
            cone3(x, 3.8f * s, z, 1.6f * s, 3.6f * s, 6, rnd() * 3, scol(leaf, 1.1f));
        }
    }
    /* daglar */
    {
        float mx = (mapMinX + mapMaxX) * 0.5f, mz = (mapMinZ + mapMaxZ) * 0.5f;
        for (int i = 0; i < 22; i++) {
            float a = i * 2 * PI / 22 + rnd() * 0.1f;
            float rad = 950.0f + rnd() * 120.0f;
            float h = 160.0f + rnd() * 160.0f, r = h * (0.9f + rnd() * 0.5f);
            float x = mx + cosf(a) * rad, z = mz + sinf(a) * rad;
            cone3(x, 0, z, r, h, 5, rnd() * 3, RGB(98, 108, 170));
            cone3(x, h * 0.72f, z, r * 0.29f, h * 0.28f, 5, 0, RGB(240, 245, 255));
        }
    }

    /* --- premium roadside set dressing: lamps, rocks and banners --- */
    for (int k = 5; k < M; k += 10) {
        float fx = tfx[k], fz = tfz[k], rx = -fz, rz = fx;
        for (int side = -1; side <= 1; side += 2) {
            float lat = side * (HW + 6.5f + rnd() * 2.5f);
            float x = tcx[k] + rx * lat, z = tcz[k] + rz * lat;
            /* lamp post */
            box3(x, 3.0f, z, 0.10f, 3.0f, 0.10f, RGB(55, 58, 68));
            box3(x, 6.0f, z, 0.42f, 0.10f, 0.16f, RGB(80, 82, 92));
            box3(x + rx * 0.18f, 5.72f, z + rz * 0.18f, 0.12f, 0.10f, RGB(255, 224, 125));
        }
    }

    /* Hero arches around the lap: chunky supports + colored top beam. */
    for (int aidx = 0; aidx < 3; aidx++) {
        int k = (aidx * M / 3 + 18) % M;
        float fx = tfx[k], fz = tfz[k], rx = -fz, rz = fx;
        float px = tcx[k], pz = tcz[k], off = HW + 2.8f;
        boxO(px + rx * off, 4.2f, pz + rz * off, fx, fz, 0.34f, 4.2f, 0.34f, RGB(42, 48, 60));
        boxO(px - rx * off, 4.2f, pz - rz * off, fx, fz, 0.34f, 4.2f, 0.34f, RGB(42, 48, 60));
        boxO(px, 8.2f, pz, fx, fz, 0.40f, 0.42f, off + 0.5f, (aidx == 0) ? RGB(210, 38, 50) : RGB(38, 92, 210));
        boxO(px, 7.45f, pz, fx, fz, 0.52f, 0.12f, off + 0.1f, RGB(245, 245, 250));
    }

    /* Low-poly boulders give the track a denser, more cinematic silhouette. */
    for (int k = 3; k < M; k += 5) {
        int side = (k & 1) ? 1 : -1;
        float lat = side * (HW + 18.0f + rnd() * 16.0f);
        float x = tcx[k] - tfz[k] * lat, z = tcz[k] + tfx[k] * lat;
        float s = 0.7f + rnd() * 1.6f;
        unsigned rc = (rnd() < 0.5f) ? RGB(92, 96, 108) : RGB(116, 105, 96);
        box3(x, 0.55f * s, z, 0.75f * s, 0.55f * s, 0.60f * s, rc);
        cone3(x, 0.45f * s, z, 1.15f * s, 1.15f * s, 6, rnd() * 3.0f, scol(rc, 1.08f));
    }

    nScene = nv;

    /* --- arac meshleri --- */
    kartCount = 0;
    for (int i = 0; i < NK; i++) {
        kartStart[i] = nv;
        buildKartMesh(kBody[i], kTrim[i]);
        kartCount = nv - kartStart[i];
    }
    boxStart = nv;
    box3(0, 0, 0, 0.8f, 0.8f, 0.8f, RGB(255, 200, 40));
    box3(0, 0.82f, 0, 0.82f, 0.02f, 0.82f, RGB(255, 245, 160));
    box3(0, 0, 0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0, 0, -0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    box3(-0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    boxCount = nv - boxStart;
    coneStart = nv;
    cone3(0, 0, 0, 0.9f, 2.2f, 8, 0, RGB(250, 120, 20));
    cone3(0, 0.7f, 0, 0.64f, 1.0f, 8, 0, RGB(255, 255, 255));
    box3(0, 0.1f, 0, 1.0f, 0.1f, 1.0f, RGB(60, 60, 66));
    coneCount = nv - coneStart;
    flameStart = nv;
    box3(-0.6f, 0, 0.45f, 0.6f, 0.13f, 0.13f, RGB(255, 140, 20));
    box3(-0.6f, 0, -0.45f, 0.6f, 0.13f, 0.13f, RGB(255, 140, 20));
    box3(-0.45f, 0, 0.45f, 0.45f, 0.08f, 0.08f, RGB(255, 235, 120));
    box3(-0.45f, 0, -0.45f, 0.45f, 0.08f, 0.08f, RGB(255, 235, 120));
    flameCount = nv - flameStart;
    shadowStart = nv;
    quad3(mk(-2.0f, 0.0f, -1.3f), mk(2.4f, 0.0f, -1.3f), mk(2.4f, 0.0f, 1.3f), mk(-2.0f, 0.0f, 1.3f), RGBA(0, 0, 0, 110));
    shadowCount = nv - shadowStart;
    buildSparkMesh();
    sceKernelDcacheWritebackAll();
}

/* ---------- Oyun nesneleri ---------- */
typedef struct {
    float x, z, h, vh, spd;
    int idx, lapc;
    float prog, lat;
    float boostT, driftT, spinT, spinA;
    float slowT, starT;
    int shield, driftDir;
    int item;
    float skill, lane, steerVis;
    float bobA, lean, pitch, accelVis, fxTimer;
} Kart;
static Kart K[NK];

typedef struct { float x, z; int type; int active; float resp; } Thing;

/* ---------- Animasyon / parcacik efektleri ---------- */
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life, maxLife, size;
    int active, kind;
} FxPart;
#define MAXFX 64
static FxPart fx[MAXFX];
static int fxHead = 0;

static void spawnFx(float x, float y, float z, float vx, float vy, float vz,
                    float life, float size, int kind) {
    int id = fxHead++ % MAXFX;
    fx[id].x = x; fx[id].y = y; fx[id].z = z;
    fx[id].vx = vx; fx[id].vy = vy; fx[id].vz = vz;
    fx[id].life = life; fx[id].maxLife = life; fx[id].size = size;
    fx[id].active = 1; fx[id].kind = kind;
}

static void spawnKartFx(int who, int kind, int count) {
    if (who < 0 || who >= NK) return;
    Kart *k = &K[who];
    float ca = cosf(k->h), sa = sinf(k->h);
    for (int n = 0; n < count; n++) {
        float a = k->h + PI + (rnd() - 0.5f) * 0.8f;
        float sp = 4.0f + rnd() * 9.0f + k->spd * 0.06f;
        float side = (rnd() - 0.5f) * 1.2f;
        float dist = 1.8f + rnd() * 0.9f;
        float px = k->x - ca * dist - sa * side;
        float pz = k->z - sa * dist + ca * side;
        float py = 0.18f + rnd() * 0.7f;
        spawnFx(px, py, pz,
                cosf(a) * sp + (rnd() - 0.5f) * 2.0f,
                1.8f + rnd() * 4.5f,
                sinf(a) * sp + (rnd() - 0.5f) * 2.0f,
                0.22f + rnd() * 0.30f,
                0.16f + rnd() * 0.16f,
                kind);
    }
}

static void updateFx(void) {
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        fx[i].x += fx[i].vx * DT;
        fx[i].y += fx[i].vy * DT;
        fx[i].z += fx[i].vz * DT;
        fx[i].vy -= 10.0f * DT;
        fx[i].life -= DT;
        if (fx[i].life <= 0 || fx[i].y < 0.03f) fx[i].active = 0;
    }
}


/* type: 0 = esya kutusu, 1 = trafik konisi, 2 = muz tuzagi */
#define TH_ITEM   0
#define TH_CONE   1
#define TH_BANANA 2
#define MAXTHING 64
static Thing things[MAXTHING];
static int nthings = 0;

static int state, lap, finalRank;
#define STATE_MENU  (-1)
#define STATE_PAUSE (3)
static int cameraMode = 0;
static float cd, raceTime, finalTime, shake, lapFlash, tAnim, camH, fovCur;
static unsigned prevB = 0;

/* ---------- Ozel gucler ---------- */
#define ITEM_NONE    0
#define ITEM_MUSH    1
#define ITEM_SHELL   2
#define ITEM_BANANA  3
#define ITEM_LIGHT   4
#define ITEM_SHIELD  5
#define ITEM_STAR    6

typedef struct {
    float x, z, speed;
    int active;
    int target;
} ItemShot;

static ItemShot shellShots[NK];
static int itemCntG = 0;

static const char *itemName(int item) {
    switch (item) {
    case ITEM_MUSH:   return "MUSH";
    case ITEM_SHELL:  return "SHELL";
    case ITEM_BANANA: return "BANANA";
    case ITEM_LIGHT:  return "LIGHT";
    case ITEM_SHIELD: return "SHIELD";
    case ITEM_STAR:   return "STAR";
    }
    return "NONE";
}

static unsigned itemColor(int item) {
    switch (item) {
    case ITEM_MUSH:   return RGB(235, 50, 50);
    case ITEM_SHELL:  return RGB(60, 150, 255);
    case ITEM_BANANA: return RGB(255, 210, 40);
    case ITEM_LIGHT:  return RGB(255, 245, 70);
    case ITEM_SHIELD: return RGB(80, 230, 255);
    case ITEM_STAR:   return RGB(255, 150, 40);
    }
    return RGB(180, 180, 180);
}

static int calcRank(void);
static int kartRank(int who) {
    int r = 1;
    for (int i = 0; i < NK; i++) if (i != who && K[i].prog > K[who].prog) r++;
    return r;
}
static void initRace(void);

static int randomItem(void) {
    int rank = calcRank();
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 25) return ITEM_STAR;
        if (r < 45) return ITEM_LIGHT;
        if (r < 65) return ITEM_SHELL;
        if (r < 80) return ITEM_MUSH;
        if (r < 90) return ITEM_SHIELD;
        return ITEM_BANANA;
    }
    if (r < 30) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 70) return ITEM_SHELL;
    if (r < 82) return ITEM_SHIELD;
    if (r < 93) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void giveItem(Kart *p) {
    p->item = randomItem();
    if (p == &K[0]) itemCntG = 1;
}

static int botRandomItem(int who) {
    int rank = kartRank(who);
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 24) return ITEM_STAR;
        if (r < 44) return ITEM_LIGHT;
        if (r < 68) return ITEM_SHELL;
        if (r < 82) return ITEM_MUSH;
        if (r < 92) return ITEM_BANANA;
        return ITEM_SHIELD;
    }
    if (r < 28) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 73) return ITEM_SHELL;
    if (r < 86) return ITEM_SHIELD;
    if (r < 95) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void botGiveItem(int who) {
    if (who <= 0 || K[who].item != ITEM_NONE) return;
    K[who].item = botRandomItem(who);
}

static int botTargetAhead(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[i].prog - K[who].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) {
        for (int i = 0; i < NK; i++) if (i != who) {
            float d = fabsf(K[i].prog - K[who].prog);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

static int botTargetBehind(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[who].prog - K[i].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void botFireShell(int who, int target) {
    if (target < 0 || target >= NK || who == target) return;
    shellShots[who].x = K[who].x + cosf(K[who].h) * 2.5f;
    shellShots[who].z = K[who].z + sinf(K[who].h) * 2.5f;
    shellShots[who].speed = 72.0f;
    shellShots[who].target = target;
    shellShots[who].active = 1;
}

static int addBanana(float x, float z);

static void botUseItem(int who) {
    if (who <= 0 || state != 1) return;
    Kart *b = &K[who];
    int item = b->item;
    if (item == ITEM_NONE) return;
    int ahead = botTargetAhead(who);
    int behind = botTargetBehind(who);

    if (item == ITEM_MUSH) {
        if (b->spd < VMAX * 0.82f || b->prog < K[0].prog - 40.0f) {
            b->boostT = 2.0f; b->spd += VMAX * 0.16f; b->item = ITEM_NONE;
        }
    } else if (item == ITEM_SHELL) {
        if (ahead >= 0 && fabsf(K[ahead].prog - b->prog) < 420.0f) {
            botFireShell(who, ahead); b->item = ITEM_NONE;
        }
    } else if (item == ITEM_BANANA) {
        if (behind >= 0 && K[who].prog - K[behind].prog < 120.0f) {
            addBanana(b->x - cosf(b->h) * 2.0f, b->z - sinf(b->h) * 2.0f); b->item = ITEM_NONE;
        }
    } else if (item == ITEM_LIGHT) {
        if (ahead >= 0 || behind >= 0) {
            for (int i = 0; i < NK; i++) if (i != who && K[i].starT <= 0) {
                if (K[i].shield) K[i].shield = 0;
                else { K[i].spd *= 0.45f; K[i].slowT = 2.8f; K[i].spinT = 0.45f; }
            }
            b->item = ITEM_NONE; shake = 0.45f;
        }
    } else if (item == ITEM_SHIELD) {
        if (behind >= 0 || b->prog < K[0].prog + 100.0f) { b->shield = 1; b->item = ITEM_NONE; }
    } else if (item == ITEM_STAR) {
        if (b->prog < K[0].prog + 180.0f || b->spd < VMAX * 0.7f) {
            b->starT = 5.0f; b->boostT = 5.0f; b->spd += VMAX * 0.18f; b->item = ITEM_NONE;
        }
    }
}

static void hitKart(Kart *k, float slow, float spin) {
    int who = (int)(k - K);
    if (k->starT > 0 || k->shield) {
        if (k->shield) {
            k->shield = 0;
            spawnKartFx(who, 2, 8);
        }
        return;
    }
    spawnKartFx(who, 2, 10);
    k->spd *= slow;
    k->boostT = 0;
    k->spinT = spin;
    k->driftDir = 0;
    k->slowT = 0.0f;
}

static int addBanana(float x, float z) {
    for (int i = 0; i < MAXTHING; i++) {
        if (things[i].active == 0 && things[i].type == TH_BANANA) {
            things[i].x = x; things[i].z = z;
            things[i].type = TH_BANANA; things[i].active = 1; things[i].resp = 0.6f;
            if (i >= nthings) nthings = i + 1;
            return 1;
        }
    }
    if (nthings >= MAXTHING) return 0;
    things[nthings].x = x; things[nthings].z = z;
    things[nthings].type = TH_BANANA; things[nthings].active = 1; things[nthings].resp = 0.6f;
    nthings++;
    return 1;
}

static void useItem(void) {
    Kart *p = &K[0];
    int item = p->item;
    if (item == ITEM_NONE || itemCntG <= 0 || state != 1) return;

    itemCntG = 0;
    p->item = ITEM_NONE;

    if (item == ITEM_MUSH) {
        p->boostT = 2.0f;
        p->spd += VMAX * 0.18f;
    } else if (item == ITEM_SHELL) {
        int best = botTargetAhead(0);
        if (best >= 0) botFireShell(0, best);
    } else if (item == ITEM_BANANA) {
        addBanana(p->x - cosf(p->h) * 2.0f, p->z - sinf(p->h) * 2.0f);
    } else if (item == ITEM_LIGHT) {
        for (int i = 1; i < NK; i++) {
            if (K[i].starT <= 0 && K[i].shield) {
                K[i].shield = 0;
            } else if (K[i].starT <= 0) {
                K[i].spd *= 0.45f;
                K[i].slowT = 2.8f;
                K[i].spinT = 0.45f;
            }
        }
        shake = 0.65f;
    } else if (item == ITEM_SHIELD) {
        p->shield = 1;
    } else if (item == ITEM_STAR) {
        p->starT = 5.0f;
        p->boostT = 5.0f;
        p->spd += VMAX * 0.22f;
    }
}

static void updateShell(void) {
    for (int owner = 0; owner < NK; owner++) {
        ItemShot *shot = &shellShots[owner];
        if (!shot->active) continue;
        if (shot->target < 0 || shot->target >= NK || shot->target == owner) { shot->active = 0; continue; }
        Kart *t = &K[shot->target];
        float dx = t->x - shot->x, dz = t->z - shot->z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d < 2.2f) {
            hitKart(t, 0.35f, 1.2f);
            shot->active = 0;
            if (shot->target == 0) shake = 0.35f;
            continue;
        }
        if (d < 0.001f) d = 0.001f;
        shot->x += dx / d * shot->speed * DT;
        shot->z += dz / d * shot->speed * DT;
    }
}

static void updateItemTimers(void) {
    for (int i = 0; i < NK; i++) {
        if (K[i].starT > 0) K[i].starT -= DT;
        if (K[i].slowT > 0) K[i].slowT -= DT;
    }
}

static void buildThings(void) {
    nthings = 0;
    for (int q = 0; q < 9; q++) {
        int s = (M * q / 9 + 10) % M;
        for (int j = -1; j <= 1; j++) {
            float lat = j * HW * 0.42f;
            things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
            things[nthings].type = TH_ITEM; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
        }
    }
    for (int i = 0; i < 20; i++) {
        int s = (M * i / 20 + 17) % M;
        float lat = (((i * 37) % 9) - 4) * 0.2f * HW * 0.75f;
        things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
        things[nthings].type = TH_CONE; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
    }
}

static void trackUpdate(Kart *k) {
    int best = k->idx; float bd = 1e18f;
    for (int j = -8; j <= 12; j++) {
        int i = (k->idx + j + M * 4) % M;
        float dx = k->x - tcx[i], dz = k->z - tcz[i];
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    int old = k->idx;
    if (best < old - M / 2) k->lapc++;
    else if (best > old + M / 2) k->lapc--;
    k->idx = best;
    float dx = k->x - tcx[best], dz = k->z - tcz[best];
    k->lat = dx * (-tfz[best]) + dz * tfx[best];
    float along = dx * tfx[best] + dz * tfz[best];
    k->prog = k->lapc * TL + tsd[best] + along;
    /* gorunmez duvar */
    float lim = HW + 12.0f;
    if (fabsf(k->lat) > lim) {
        float sgn = k->lat > 0 ? 1.0f : -1.0f, over = fabsf(k->lat) - lim;
        k->x -= (-tfz[best]) * over * sgn; k->z -= tfx[best] * over * sgn;
        k->lat = sgn * lim; k->spd *= 0.96f;
    }
}

static void stepKart(Kart *k, float steer, int gas, int brake, int driftBtn) {
    float oldSpd = k->spd;
    if (k->spinT > 0) { k->spinT -= DT; k->spinA += 14.0f * DT; steer = 0; gas = 0; brake = 0; driftBtn = 0; if (k->spinT <= 0) k->spinA = 0; }
    int boosting = k->boostT > 0;
    if (boosting) k->boostT -= DT;
    int off = fabsf(k->lat) > HW + 1.8f;
    float slowMul = k->slowT > 0 ? 0.48f : 1.0f;
    float lim = (boosting ? VMAX * (off ? 0.8f : 1.35f) : (off ? VMAX * 0.45f : VMAX)) * slowMul;
    if (boosting) k->spd += 90.0f * DT;
    else if (brake) k->spd -= 60.0f * DT;
    else if (gas) k->spd += 30.0f * (1.0f - 0.55f * k->spd / VMAX) * DT;
    else k->spd -= 10.0f * DT;
    if (k->spd > lim) k->spd -= (off ? 80.0f : 40.0f) * DT;
    if (k->spd < 0) k->spd = 0;

    if (driftBtn && !k->driftDir && k->spd > VMAX * 0.45f && fabsf(steer) > 0.3f) { k->driftDir = steer > 0 ? 1 : -1; k->driftT = 0; }
    if (k->driftDir) {
        if (!driftBtn || k->spd < VMAX * 0.3f) {
            if (k->driftT > 1.8f) k->boostT = 1.5f;
            else if (k->driftT > 0.8f) k->boostT = 0.9f;
            k->driftDir = 0; k->driftT = 0;
        } else k->driftT += DT;
    }
    float sp = k->spd;
    float rate = 2.0f * clampf(sp / 14.0f, 0, 1) * (1.0f - 0.25f * sp / VMAX);
    float turn = k->driftDir ? (k->driftDir * 0.85f + steer * 0.75f) * rate * 1.15f : steer * rate;
    k->h = wrapA(k->h + turn * DT);
    float grip = k->driftDir ? 2.4f : 14.0f;
    k->vh = wrapA(k->vh + wrapA(k->h - k->vh) * clampf(grip * DT, 0, 1));
    k->x += cosf(k->vh) * k->spd * DT;
    k->z += sinf(k->vh) * k->spd * DT;
    k->steerVis += (steer - k->steerVis) * 0.2f;

    /* Hareket animasyonlari: suspansiyon, govde yatmasi, ivmelenme */
    k->bobA += DT * (3.0f + k->spd * 0.12f);
    k->accelVis += ((k->spd - oldSpd) - k->accelVis) * 0.22f;
    {
        float targetLean = -k->steerVis * 0.15f - (k->driftDir ? k->driftDir * 0.08f : 0.0f);
        float targetPitch = clampf(-k->accelVis * 0.018f, -0.10f, 0.10f);
        k->lean += (targetLean - k->lean) * 0.20f;
        k->pitch += (targetPitch - k->pitch) * 0.20f;
    }

    /* Boost ve drift sirasinda hareketli parcacik efekti */
    k->fxTimer -= DT;
    if (k->boostT > 0 && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 0, 2);
        k->fxTimer = 0.06f;
    } else if (k->driftDir && k->spd > VMAX * 0.45f && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 1, 1);
        k->fxTimer = 0.10f;
    }
}

static void aiDrive(Kart *k, int n) {
    int la = (k->idx + 6 + (int)(k->spd / 8.0f)) % M;
    float lane = (k->lane + 0.35f * sinf(k->prog * 0.01f + n)) * HW * 0.6f;
    float tx = tcx[la] - tfz[la] * lane, tz = tcz[la] + tfx[la] * lane;
    float want = atan2f(tz - k->z, tx - k->x);
    float steer = clampf(wrapA(want - k->h) * 2.2f, -1, 1);
    int j = (k->idx + 14) % M;
    float turn = fabsf(wrapA(tth[j] - tth[k->idx]));
    float target = VMAX * k->skill * (1.0f - clampf(turn * 0.8f, 0, 0.35f));
    float pd = k->prog - K[0].prog;
    if (n != 0) { if (pd > 250) target *= 0.93f; else if (pd < -250) target *= 1.07f; }

    /* Botlar da esya kutularini toplar. */
    for (int i = 0; i < nthings; i++) {
        Thing *t = &things[i];
        if (!t->active || t->type != TH_ITEM || k->item != ITEM_NONE) continue;
        float dx = t->x - k->x, dz = t->z - k->z;
        if (dx * dx + dz * dz < 2.3f * 2.3f) {
            t->active = 0; t->resp = 8.0f;
            botGiveItem(n);
            break;
        }
    }

    /* Zorluk ve pozisyona gore akilli esya kullanimi. */
    if (k->item != ITEM_NONE && (rnd() < 0.10f || k->item == ITEM_LIGHT || k->item == ITEM_SHELL))
        botUseItem(n);

    /* Daha iyi botlar virajlarda hafif fren ve ara sira drift yapar. */
    int drift = (fabsf(turn) > 0.32f && k->spd > VMAX * 0.55f && ((int)(tAnim * 10.0f) + n) % 3 == 0);
    stepKart(k, steer, k->spd < target, k->spd > target + 6.0f, drift);
}

static int calcRank(void) {
    int r = 1;
    for (int i = 1; i < NK; i++) if (K[i].prog > K[0].prog) r++;
    return r;
}

static void initRace(void) {
    state = 0; lap = 1; cd = 4.0f; raceTime = 0; finalRank = 0; finalTime = 0; shake = 0; lapFlash = 0;
    itemCntG = 0; fxHead = 0; memset(fx, 0, sizeof(fx));
    for (int i = 0; i < NK; i++) shellShots[i].active = 0;
    static const float skills[NK] = { 0.85f, 0.95f, 0.92f, 0.90f, 0.88f, 0.86f };
    for (int i = 0; i < NK; i++) {
        int ii; float lat;
        if (i == 0) { ii = M - 9; lat = 0; }
        else { int r = (i - 1) / 2; ii = M - 3 - r * 2; lat = ((i - 1) % 2 ? 0.45f : -0.45f) * HW; }
        Kart *k = &K[i];
        memset(k, 0, sizeof(Kart));
        k->x = tcx[ii] - tfz[ii] * lat; k->z = tcz[ii] + tfx[ii] * lat;
        k->h = tth[ii]; k->vh = k->h; k->idx = ii; k->lapc = -1;
        k->skill = skills[i]; k->lane = -0.8f + (i - 1) * 0.4f;
        trackUpdate(k);
    }
    for (int i = 0; i < nthings; i++) { things[i].active = 1; things[i].resp = 0; }
    camH = K[0].h; fovCur = 62.0f;
    cameraMode = 0;
}

static float readSteer(const SceCtrlData *pad) {
    /* PSP analog stick: dead-zone + normalized response. */
    int lx = (int)pad->Lx - 128;
    float steer = 0.0f;
    const int dead = 18;
    const int full = 110;

    if (pad->Buttons & PSP_CTRL_LEFT) steer -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) steer += 1.0f;

    if (abs(lx) > dead) {
        float a = (float)(abs(lx) - dead) / (float)(full - dead);
        if (a > 1.0f) a = 1.0f;
        float analog = (lx < 0) ? -a : a;
        /* Analog is the fine steering source; D-pad remains a digital fallback. */
        if (steer == 0.0f) steer = analog;
        else steer = clampf(steer + analog * 0.25f, -1.0f, 1.0f);
    }
    return clampf(steer, -1.0f, 1.0f);
}

static void update(SceCtrlData *pad) {
    tAnim += DT;
    updateFx();

    unsigned pressed = pad->Buttons & ~prevB;

    if (state == STATE_MENU) {
        prevB = pad->Buttons;
        if (pressed & PSP_CTRL_CROSS) initRace();
        return;
    }

    /* START is the pause button during a race. */
    if (state == 1 && (pressed & PSP_CTRL_START)) {
        state = STATE_PAUSE;
        prevB = pad->Buttons;
        return;
    }

    if (state == STATE_PAUSE) {
        if (pressed & PSP_CTRL_START) state = 1;
        else if (pressed & PSP_CTRL_SELECT) initRace();
        prevB = pad->Buttons;
        return;
    }

    /* Triangle cycles three lightweight chase-camera presets. */
    if (state == 1 && (pressed & PSP_CTRL_TRIANGLE))
        cameraMode = (cameraMode + 1) % 3;

    /* SELECT provides a quick restart after the finish. */
    if (state == 2 && (pressed & PSP_CTRL_SELECT)) { initRace(); return; }
    if (state == 2 && (pressed & PSP_CTRL_START)) { initRace(); return; }

    prevB = pad->Buttons;

    float steer = readSteer(pad);
    int gas = (pad->Buttons & PSP_CTRL_CROSS) != 0;
    int brake = (pad->Buttons & PSP_CTRL_SQUARE) != 0;
    int drift = (pad->Buttons & (PSP_CTRL_RTRIGGER | PSP_CTRL_LTRIGGER)) != 0;

    if (shake > 0) shake -= DT;
    if (lapFlash > 0) lapFlash -= DT;

    if (state == 0) {
        cd -= DT;
        if (cd <= 1.0f) state = 1;
    } else if (state == 1) {
        raceTime += DT;
        stepKart(&K[0], steer, gas, brake, drift);
        for (int i = 1; i < NK; i++) aiDrive(&K[i], i);

        /* kart-kart carpisma */
        for (int i = 0; i < NK; i++) for (int j = i + 1; j < NK; j++) {
            float dx = K[j].x - K[i].x, dz = K[j].z - K[i].z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 2.7f * 2.7f && d2 > 0.0001f) {
                float d = sqrtf(d2), ov = (2.7f - d) * 0.5f;
                float nx = dx / d, nz = dz / d;
                K[i].x -= nx * ov; K[i].z -= nz * ov; K[j].x += nx * ov; K[j].z += nz * ov;
                K[i].spd *= 0.985f; K[j].spd *= 0.985f;
                if (i == 0 || j == 0) shake = 0.15f;
            }
        }
        for (int i = 0; i < NK; i++) trackUpdate(&K[i]);

        Kart *p = &K[0];
        updateItemTimers();
        updateShell();

        if (padMark[p->idx] && fabsf(p->lat) < HW * 0.45f && p->boostT < 0.8f)
            p->boostT = 1.2f;

        /* Player item boxes. Bots are handled in aiDrive(). */
        for (int i = 0; i < nthings; i++) {
            Thing *t = &things[i];
            if (!t->active) {
                if ((t->type == TH_ITEM || t->type == TH_BANANA) && t->resp > 0) {
                    t->resp -= DT;
                    if (t->resp <= 0 && t->type == TH_ITEM) t->active = 1;
                }
                continue;
            }

            float dx = t->x - p->x, dz = t->z - p->z;
            if (t->type == TH_ITEM) {
                if (dx * dx + dz * dz < 2.3f * 2.3f) {
                    t->active = 0;
                    t->resp = 8.0f;
                    if (itemCntG == 0) giveItem(p);
                }
            } else if (t->type == TH_CONE) {
                if (dx * dx + dz * dz < 1.8f * 1.8f) {
                    t->active = 0;
                    hitKart(p, 0.35f, 0.9f);
                    shake = 0.5f;
                }
            }
        }

        /* Banana traps can hit player or any bot. */
        for (int ti = 0; ti < nthings; ti++) {
            Thing *t = &things[ti];
            if (!t->active || t->type != TH_BANANA) continue;
            for (int ki = 0; ki < NK; ki++) {
                float dx = t->x - K[ki].x, dz = t->z - K[ki].z;
                if (dx * dx + dz * dz < 2.0f * 2.0f) {
                    hitKart(&K[ki], 0.32f, 1.0f);
                    t->active = 0;
                    t->resp = 0;
                    if (ki == 0) shake = 0.45f;
                    break;
                }
            }
        }

        if (pressed & PSP_CTRL_CIRCLE) useItem();

        /* tur */
        {
            int lp = (int)floorf(p->prog / TL) + 1;
            if (lp > lap && lp <= LAPS) {
                lap = lp; lapFlash = 2.0f;
                for (int i = 0; i < nthings; i++) if (things[i].type == TH_CONE) things[i].active = 1;
            }
            if (p->prog >= LAPS * TL) { state = 2; finalRank = calcRank(); finalTime = raceTime; }
        }
    }
}
/* ---------- Cizim ---------- */
static void modelAt(float x, float y, float z, float yaw) {
    ScePspFVector3 t; t.x = x; t.y = y; t.z = z;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t);
    sceGumRotateY(-yaw);
}

static const unsigned HAZE = RGB(175, 218, 255);

static void render3D(void) {
    Kart *p = &K[0];
    float tgt = p->vh + wrapA(p->h - p->vh) * 0.5f;
    camH = wrapA(camH + wrapA(tgt - camH) * clampf(5.0f * DT, 0, 1));
    float boostF = (p->boostT > 0) ? 1.0f : 0.0f;

    /* Three PSP-friendly chase cameras: normal, close, wide. */
    float baseFov = 62.0f, back = 9.5f, eyeY = 4.3f, targetY = 1.4f, targetD = 7.0f;
    if (cameraMode == 1) {
        baseFov = 58.0f; back = 7.2f; eyeY = 3.5f; targetD = 7.8f;
    } else if (cameraMode == 2) {
        baseFov = 68.0f; back = 13.5f; eyeY = 5.8f; targetD = 8.5f;
    }
    back += 1.2f * boostF;
    fovCur += ((baseFov + 12.0f * boostF) - fovCur) * 0.10f;

    float sh = (shake > 0) ? sinf(tAnim * 90.0f) * 0.25f : 0.0f;
    ScePspFVector3 eye, ctr, up;
    eye.x = p->x - cosf(camH) * back; eye.y = eyeY + sh; eye.z = p->z - sinf(camH) * back;
    ctr.x = p->x + cosf(camH) * targetD; ctr.y = targetY; ctr.z = p->z + sinf(camH) * targetD;
    up.x = 0; up.y = 1; up.z = 0;

    /* gokyuzu */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    vgrad(0, 0, W, 150, RGB(30, 100, 225), HAZE);
    rect(0, 150, W, H - 150, HAZE);
    /* stylized sun bloom */
    rect(356, 48, 32, 32, RGBA(255, 244, 190, 45));
    rect(362, 54, 20, 20, RGBA(255, 250, 215, 90));

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovCur, (float)W / (float)H, 1.5f, 1800.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &ctr, &up);
    /* Cinematic chase-camera roll follows steering/boost without hurting control precision. */
    sceGumRotateZ(-p->lean * 0.10f);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuEnable(GU_FOG);
    /* zemin, yol (derinlik testsiz) */
    sceGumDrawArray(GU_TRIANGLES, VF3, nFlat, 0, mesh);
    /* golgeler */
    for (int i = 0; i < NK; i++) {
        modelAt(K[i].x, 0.08f, K[i].z, K[i].h);
        sceGumDrawArray(GU_TRIANGLES, VF3, shadowCount, 0, &mesh[shadowStart]);
    }
    /* nesneler (derinlik testli) */
    sceGuEnable(GU_DEPTH_TEST);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, VF3, nScene - nFlat, 0, &mesh[nFlat]);

    for (int i = 0; i < NK; i++) {
        Kart *k = &K[i];
        float bob = 0.05f * sinf(k->bobA) * clampf(k->spd / VMAX, 0, 1);
        if (k->spinT > 0) bob += 0.05f * sinf(tAnim * 36.0f);
        float yaw = k->h + k->driftDir * 0.3f + k->spinA;
        modelAt(k->x, bob, k->z, yaw);
        sceGumRotateX(k->pitch);
        sceGumRotateZ(k->lean);
        sceGumDrawArray(GU_TRIANGLES, VF3, kartCount, 0, &mesh[kartStart[i]]);
        if (k->boostT > 0) {
            ScePspFVector3 t, sc;
            t.x = -1.9f; t.y = 0.7f + 0.05f * sinf(tAnim * 32.0f + i); t.z = 0;
            sc.x = 0.75f + 0.55f * (0.5f + 0.5f * sinf(tAnim * 46.0f + i)); sc.y = 1; sc.z = 1;
            sceGumTranslate(&t);
            sceGumScale(&sc);
            sceGumDrawArray(GU_TRIANGLES, VF3, flameCount, 0, &mesh[flameStart]);
        }
        if (k->starT > 0) {
            for (int q = 0; q < 4; q++) {
                float a = tAnim * 7.0f + q * (PI * 0.5f);
                ScePspFVector3 t;
                t.x = cosf(a) * 2.2f; t.y = 1.1f + 0.55f * sinf(a * 1.7f); t.z = sinf(a) * 2.2f;
                sceGumTranslate(&t);
                sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
            }
        }
    }
    for (int i = 0; i < nthings; i++) {
        Thing *t = &things[i];
        if (!t->active) continue;
        if (t->type == TH_ITEM) {
            modelAt(t->x, 1.5f + 0.2f * sinf(tAnim * 3.0f + i), t->z, tAnim * 2.0f);
            sceGumDrawArray(GU_TRIANGLES, VF3, boxCount, 0, &mesh[boxStart]);
        } else if (t->type == TH_CONE) {
            modelAt(t->x, 0, t->z, 0);
            sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
        } else if (t->type == TH_BANANA) {
            modelAt(t->x, 0.35f, t->z, tAnim * 2.5f);
            sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
        }
    }

    for (int owner = 0; owner < NK; owner++) if (shellShots[owner].active) {
        modelAt(shellShots[owner].x, 1.0f + 0.18f * sinf(tAnim * 16.0f + owner), shellShots[owner].z, tAnim * 8.0f);
        sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
    }
    /* Parcaciklari 3D sahnede canli sekilde goster */
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        float fade = clampf(fx[i].life / fx[i].maxLife, 0, 1);
        float s = fx[i].size * (0.55f + 0.85f * fade);
        modelAt(fx[i].x, fx[i].y, fx[i].z, tAnim * (fx[i].kind == 1 ? 12.0f : 18.0f));
        {
            ScePspFVector3 sc; sc.x = s; sc.y = s; sc.z = s;
            sceGumScale(&sc);
        }
        sceGumDrawArray(GU_TRIANGLES, VF3, sparkCount, 0, &mesh[sparkStart]);
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
}

static void drawMush(float x, float y, float s) {
    tri2(x, y + s * 1.1f, x + s * 2, y + s * 1.1f, x + s, y - s * 0.2f, RGB(235, 40, 40));
    rect(x + s * 0.55f, y + s * 1.1f, s * 0.9f, s * 0.9f, RGB(250, 240, 215));
    rect(x + s * 0.8f, y + s * 0.5f, s * 0.4f, s * 0.35f, RGB(255, 255, 255));
}

static void drawHUD(void) {
    char buf[40];
    Kart *p = &K[0];

    /* Ana menu: tek kisilik yaris, diger araclar BOT */
    if (state == STATE_MENU) {
        rect(0, 0, W, H, RGBA(0, 0, 0, 135));
        float mp = 1.0f + 0.05f * sinf(tAnim * 3.0f);
        int mw = (int)(30 * mp), mh = (int)(50 * mp);
        text(W / 2 - textWidth("MARIO KART PSP", mw, 8) / 2, 38 - (mh - 50) / 2, mw, mh, 8, "MARIO KART PSP", RGB(255, 220, 50));
        rect(W / 2 - 115, 112, 230, 48, RGBA(20, 70, 150, 220));
        text(W / 2 - textWidth("TEK KISILIK", 18, 5) / 2, 123, 18, 30, 5, "TEK KISILIK", RGB(255, 255, 255));
        text(W / 2 - textWidth("X: BASLAT", 11, 3) / 2, 178, 11, 18, 3, "X: BASLAT", RGB(120, 240, 255));
        text(W / 2 - textWidth("5 BOT YARISMACI", 10, 3) / 2, 203, 10, 16, 3, "5 BOT YARISMACI", RGB(230, 230, 230));
        text(W / 2 - textWidth("CINEMATIC PSP RACING EDITION", 8, 2) / 2, 235, 8, 14, 2, "CINEMATIC PSP RACING EDITION", RGB(180, 200, 220));
        return;
    }
    int rank = (state == 2) ? finalRank : calcRank();
    snprintf(buf, sizeof(buf), "POS %d/%d", rank, NK);
    text(8, 8, 11, 18, 3, buf, rank == 1 ? RGB(255, 215, 40) : RGB(255, 255, 255));
    text(8, 28, 8, 14, 2, "5 BOT", RGB(120, 220, 255));
    snprintf(buf, sizeof(buf), "LAP %d/%d", lap, LAPS);
    text(W - 8 - textWidth(buf, 11, 3), 8, 11, 18, 3, buf, RGB(255, 255, 255));

    float t = (state == 2) ? finalTime : raceTime;
    int m = (int)(t / 60), s = (int)t % 60, d = (int)(t * 10) % 10;
    snprintf(buf, sizeof(buf), "%d:%02d.%d", m, s, d);
    text(W / 2 - textWidth(buf, 9, 3) / 2, 10, 9, 15, 3, buf, RGB(255, 255, 255));

    /* harita */
    {
        float sx = 92.0f / (mapMaxX - mapMinX), sz = 66.0f / (mapMaxZ - mapMinZ);
        float sc = sx < sz ? sx : sz;
        float ox = W - 104 + 4, oy = 36 + 3;
        rect(W - 104, 34, 98, 74, RGBA(0, 0, 0, 120));
        for (int i = 0; i < M; i += 4) rect(ox + (tcx[i] - mapMinX) * sc, oy + (tcz[i] - mapMinZ) * sc, 3, 3, RGB(210, 210, 215));
        rect(ox + (tcx[0] - mapMinX) * sc - 1, oy + (tcz[0] - mapMinZ) * sc - 1, 5, 5, RGB(255, 255, 255));
        for (int i = NK - 1; i >= 0; i--) {
            float mx = ox + (K[i].x - mapMinX) * sc, mz = oy + (K[i].z - mapMinZ) * sc;
            if (i == 0) rect(mx - 3, mz - 3, 7, 7, RGB(255, 255, 255));
            rect(mx - 2, mz - 2, 5, 5, kBody[i]);
        }
    }

    if (itemCntG > 0 && p->item != ITEM_NONE) {
        rect(8, H - 58, 82, 24, RGBA(0, 0, 0, 170));
        float ip = 0.85f + 0.15f * (0.5f + 0.5f * sinf(tAnim * 8.0f));
        rect(10, H - 56, 18, 20, scol(itemColor(p->item), ip));
        snprintf(buf, sizeof(buf), "%s", itemName(p->item));
        text(34, H - 54, 8, 14, 3, buf, RGB(255, 255, 255));
        text(12, H - 34, 7, 12, 2, "O:USE", RGB(255, 230, 70));
    }
    if (p->shield) text(108, H - 35, 8, 14, 3, "SHIELD", RGB(80, 230, 255));
    if (p->starT > 0) text(108, H - 55, 8, 14, 3, "STAR", RGB(255, 180, 40));

    /* PSP kontrol ipucu: kalabalik HUD yerine alt satirda kisa tut. */
    if (state == 1 && (((int)(tAnim * 1.5f)) & 3) == 0)
        text(8, H - 20, 7, 12, 2, "O:ITEM  T:CAM  L/R:DRIFT", RGB(205, 220, 235));

    /* hiz */
    float lim = VMAX * 1.35f;
    for (int i = 0; i < 20; i++) {
        int on = (p->spd / lim * 20.0f) > i;
        unsigned c = on ? mixc(RGB(60, 220, 60), RGB(240, 50, 40), i / 19.0f) : RGBA(0, 0, 0, 130);
        float hh = 5 + i * 0.6f;
        rect(W - 130 + i * 6, H - 12 - hh, 4, hh, c);
    }
    snprintf(buf, sizeof(buf), "%d", (int)(p->spd / VMAX * 180.0f));
    text(W - 130, H - 42, 11, 18, 3, buf, p->boostT > 0 ? RGB(255, 190, 40) : RGB(255, 255, 255));

    /* drift gostergesi */
    if (p->driftDir) {
        float f = clampf(p->driftT / 1.8f, 0, 1);
        unsigned c = p->driftT > 1.8f ? RGB(255, 150, 20) : (p->driftT > 0.8f ? RGB(60, 150, 255) : RGB(230, 230, 230));
        rect(W / 2 - 42, H - 26, 84, 10, RGBA(0, 0, 0, 160));
        rect(W / 2 - 40, H - 24, 80 * f, 6, c);
    }

    /* turbo cizgileri */
    if (p->boostT > 0 && state != 0) {
        for (int i = 0; i < 14; i++) {
            int side = i & 1;
            int y = (i * 47 + (int)(tAnim * 700)) % H;
            rect(side ? W - 70 : 0, y, 70, 2, RGBA(255, 255, 255, 90));
        }
    }

    /* geri sayim */
    if (state == 0) {
        int lit = (cd < 3.0f) ? ((cd < 2.0f) ? 3 : 2) : 1;
        for (int i = 0; i < 3; i++) {
            rect(W / 2 - 55 + i * 38, 50, 28, 28, RGB(20, 20, 20));
            rect(W / 2 - 52 + i * 38, 53, 22, 22, (i < lit) ? RGB(240, 40, 40) : RGB(80, 20, 20));
        }
        int dg = (int)ceilf(cd) - 1;
        if (dg >= 1 && dg <= 3) {
            snprintf(buf, sizeof(buf), "%d", dg);
            float pulse = 1.0f + 0.08f * sinf(tAnim * 14.0f);
            int nw = (int)(48 * pulse), nh = (int)(80 * pulse);
            text(W / 2 - nw / 2, 96 - (nh - 80) / 2, nw, nh, 12, buf, RGB(255, 230, 60));
        }
    } else if (state == 1 && raceTime < 1.2f) {
        for (int i = 0; i < 3; i++) {
            rect(W / 2 - 55 + i * 38, 50, 28, 28, RGB(20, 20, 20));
            rect(W / 2 - 52 + i * 38, 53, 22, 22, RGB(50, 235, 70));
        }
        text(W / 2 - textWidth("GO", 44, 11) / 2, 96, 44, 76, 11, "GO", RGB(60, 240, 80));
    }
    if (lapFlash > 0 && state == 1) {
        char b2[20];
        if (lap == LAPS) snprintf(b2, sizeof(b2), "FINAL LAP"); else snprintf(b2, sizeof(b2), "LAP %d", lap);
        text(W / 2 - textWidth(b2, 18, 5) / 2, 70, 18, 30, 5, b2, RGB(255, 230, 60));
    }


    /* Cinematic letterbox + speed vignette. Kept subtle so gameplay remains readable. */
    if (state == 1) {
        float speed = clampf(fabsf(p->spd) / VMAX, 0, 1.35f);
        if (speed > 0.55f) {
            int bands = (int)(4 + speed * 4);
            for (int i = 0; i < bands; i++) {
                int yy = (i * 31 + (int)(tAnim * (90 + speed * 130))) % H;
                int side = i & 1;
                if (side) rect(W - 95 - (int)(speed * 35), yy, 70 + speed * 25, 1, RGBA(210, 235, 255, 38));
                else rect(20, yy, 70 + speed * 25, 1, RGBA(210, 235, 255, 38));
            }
        }
        /* top/bottom cinematic bars pulse slightly during boost */
        if (p->boostT > 0) {
            rect(0, 0, W, 5, RGBA(0, 0, 0, 135));
            rect(0, H - 5, W, 5, RGBA(0, 0, 0, 135));
        }
    }

    /* duraklatma ekrani */
    if (state == STATE_PAUSE) {
        rect(0, 72, W, 128, RGBA(0, 0, 0, 175));
        text(W / 2 - textWidth("PAUSE", 30, 8) / 2, 84, 30, 50, 8, "PAUSE", RGB(255, 230, 70));
        text(W / 2 - textWidth("START:DEVAM", 10, 3) / 2, 148, 10, 16, 3, "START:DEVAM", RGB(255, 255, 255));
        text(W / 2 - textWidth("SELECT:YENI", 10, 3) / 2, 172, 10, 16, 3, "SELECT:YENI", RGB(180, 220, 255));
    }

    /* bitis ekrani */
    if (state == 2) {
        rect(0, 70, W, 150, RGBA(0, 0, 0, 150));
        text(W / 2 - textWidth("FINISH", 30, 8) / 2, 80, 30, 50, 8, "FINISH", RGB(255, 255, 255));
        unsigned medal = finalRank == 1 ? RGB(255, 215, 40) : (finalRank == 2 ? RGB(210, 215, 225) : (finalRank == 3 ? RGB(215, 140, 70) : RGB(255, 255, 255)));
        snprintf(buf, sizeof(buf), "PLACE %d", finalRank);
        text(W / 2 - textWidth(buf, 24, 6) / 2, 142, 24, 40, 6, buf, medal);
        if (((int)(tAnim * 2)) & 1)
            text(W / 2 - textWidth("PRESS START", 11, 3) / 2, 194, 11, 18, 3, "PRESS START", RGB(255, 255, 255));
    }
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    render3D();
    drawHUD();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void initGraphics(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUFW);
    sceGuDispBuffer(W, H, (void *)0x88000, BUFW);
    sceGuDepthBuffer((void *)0x110000, BUFW);
    sceGuOffset(2048 - (W / 2), 2048 - (H / 2));
    sceGuViewport(2048, 2048, W, H);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, W, H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(220.0f, 1250.0f, HAZE);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_callbacks();
    initGraphics();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    buildTrackPath();
    buildMesh();
    buildThings();
    initRace();
    state = STATE_MENU;
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        update(&pad);
        render();
    }
    return 0;
}