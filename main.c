#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("MarioKartPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)

#define NUM_KARTS 6
#define NUM_WAYPOINTS 16
#define MESH_MAX_VERTS 24576

#define RGB(r, g, b) ((unsigned int)(((b) << 16) | ((g) << 8) | (r) | 0xFF000000))

typedef struct {
    float x, y, z;
} Vector3;

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

typedef enum {
    ITEM_NONE = 0,
    ITEM_MUSHROOM,
    ITEM_STAR,
    ITEM_SHELL,
    ITEM_BANANA,
    ITEM_LIGHTNING,
    ITEM_SHIELD
} ItemType;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float rotY;
    float steerAngle;
    float engineSpeed;
    float driftTimer;
    int driftDir; // -1, 0, 1
    float boostTimer;
    float starTimer;
    float spinTimer;
    float shieldTimer;
    float sizeScale;
    int currentWaypoint;
    int lap;
    float lapProgress;
    int rank;
    ItemType currentItem;
    int isAI;
    unsigned int bodyColor;
} Kart;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    ItemType type;
    int active;
    int ownerIndex;
} Projectile;

typedef struct {
    Vector3 pos;
    int active;
    float respawnTimer;
} ItemBox;

static Vertex __attribute__((aligned(16))) g_meshVerts[MESH_MAX_VERTS];
static int g_meshCount = 0;

static Kart g_karts[NUM_KARTS];
static Vector3 g_waypoints[NUM_WAYPOINTS];
static ItemBox g_itemBoxes[8];
static Projectile g_projectiles[12];

static int g_cameraMode = 0; // 0: Normal, 1: Close, 2: Cinematic
static int g_gameState = 0;  // 0: Racing, 1: Paused, 2: Finished
static float g_raceTimer = 0.0f;

// -------------------------------------------------------------------------
// Mesh Oluşturma Yardımcı Fonksiyonları
// -------------------------------------------------------------------------
static void clearMesh(void) {
    g_meshCount = 0;
}

static void addQuad(Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4, unsigned int color) {
    if (g_meshCount + 6 > MESH_MAX_VERTS) return;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p1.x; g_meshVerts[g_meshCount].y = p1.y; g_meshVerts[g_meshCount].z = p1.z;
    g_meshCount++;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p2.x; g_meshVerts[g_meshCount].y = p2.y; g_meshVerts[g_meshCount].z = p2.z;
    g_meshCount++;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p3.x; g_meshVerts[g_meshCount].y = p3.y; g_meshVerts[g_meshCount].z = p3.z;
    g_meshCount++;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p1.x; g_meshVerts[g_meshCount].y = p1.y; g_meshVerts[g_meshCount].z = p1.z;
    g_meshCount++;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p3.x; g_meshVerts[g_meshCount].y = p3.y; g_meshVerts[g_meshCount].z = p3.z;
    g_meshCount++;

    g_meshVerts[g_meshCount].color = color;
    g_meshVerts[g_meshCount].x = p4.x; g_meshVerts[g_meshCount].y = p4.y; g_meshVerts[g_meshCount].z = p4.z;
    g_meshCount++;
}

static void box3(float cx, float cy, float cz, float hx, float hy, float hz, unsigned int c) {
    Vector3 v0 = {cx - hx, cy - hy, cz - hz};
    Vector3 v1 = {cx + hx, cy - hy, cz - hz};
    Vector3 v2 = {cx + hx, cy + hy, cz - hz};
    Vector3 v3 = {cx - hx, cy + hy, cz - hz};
    Vector3 v4 = {cx - hx, cy - hy, cz + hz};
    Vector3 v5 = {cx + hx, cy - hy, cz + hz};
    Vector3 v6 = {cx + hx, cy + hy, cz + hz};
    Vector3 v7 = {cx - hx, cy + hy, cz + hz};

    // Ön / Arka
    addQuad(v4, v5, v6, v7, c);
    addQuad(v1, v0, v3, v2, c);
    // Sol / Sağ
    addQuad(v0, v4, v7, v3, c);
    addQuad(v5, v1, v2, v6, c);
    // Üst / Alt
    addQuad(v3, v7, v6, v2, c);
    addQuad(v0, v1, v5, v4, c);
}

static void drawCylinder(float cx, float cy, float cz, float radius, float height, int sides, unsigned int color) {
    float halfH = height * 0.5f;
    for (int i = 0; i < sides; i++) {
        float a1 = ((float)i / sides) * 2.0f * M_PI;
        float a2 = ((float)(i + 1) / sides) * 2.0f * M_PI;

        Vector3 p1 = {cx + cosf(a1) * radius, cy - halfH, cz + sinf(a1) * radius};
        Vector3 p2 = {cx + cosf(a2) * radius, cy - halfH, cz + sinf(a2) * radius};
        Vector3 p3 = {cx + cosf(a2) * radius, cy + halfH, cz + sinf(a2) * radius};
        Vector3 p4 = {cx + cosf(a1) * radius, cy + halfH, cz + sinf(a1) * radius};

        addQuad(p1, p2, p3, p4, color);
    }
}

static void drawMushroom(float x, float y, float z, float scale) {
    drawCylinder(x, y + 0.2f * scale, z, 0.15f * scale, 0.4f * scale, 8, RGB(240, 240, 220));
    box3(x, y + 0.5f * scale, z, 0.35f * scale, 0.2f * scale, 0.35f * scale, RGB(220, 40, 40));
}

// -------------------------------------------------------------------------
// Pist & Nesne Oluşturma
// -------------------------------------------------------------------------
static void initWaypoints(void) {
    float radius = 45.0f;
    for (int i = 0; i < NUM_WAYPOINTS; i++) {
        float angle = ((float)i / NUM_WAYPOINTS) * 2.0f * M_PI;
        g_waypoints[i].x = cosf(angle) * radius + sinf(angle * 3.0f) * 5.0f;
        g_waypoints[i].y = 0.0f;
        g_waypoints[i].z = sinf(angle) * radius + cosf(angle * 2.0f) * 5.0f;
    }
}

static void buildMesh(void) {
    clearMesh();

    // Çim Taban
    box3(0.0f, -0.5f, 0.0f, 120.0f, 0.5f, 120.0f, RGB(34, 139, 34));

    // Pist Yolu
    for (int i = 0; i < NUM_WAYPOINTS; i++) {
        Vector3 p1 = g_waypoints[i];
        Vector3 p2 = g_waypoints[(i + 1) % NUM_WAYPOINTS];

        float dx = p2.x - p1.x;
        float dz = p2.z - p1.z;
        float len = sqrtf(dx * dx + dz * dz);
        float nx = -dz / len;
        float nz = dx / len;

        float trackWidth = 7.0f;

        Vector3 w1 = {p1.x + nx * trackWidth, 0.02f, p1.z + nz * trackWidth};
        Vector3 w2 = {p1.x - nx * trackWidth, 0.02f, p1.z - nz * trackWidth};
        Vector3 w3 = {p2.x - nx * trackWidth, 0.02f, p2.z - nz * trackWidth};
        Vector3 w4 = {p2.x + nx * trackWidth, 0.02f, p2.z + nz * trackWidth};

        addQuad(w1, w2, w3, w4, (i % 2 == 0) ? RGB(60, 60, 65) : RGB(70, 70, 75));
    }

    // Dekoratif Ağaçlar ve Kutu Objeleri
    for (int i = 0; i < NUM_WAYPOINTS; i++) {
        float x = g_waypoints[i].x;
        float z = g_waypoints[i].z;

        float rx = cosf(i * 1.5f);
        float rz = sinf(i * 1.5f);

        // Ağaç Gövdesi ve Yapraklar
        drawCylinder(x + rx * 14.0f, 1.5f, z + rz * 14.0f, 0.4f, 3.0f, 6, RGB(101, 67, 33));
        box3(x + rx * 14.0f, 3.5f, z + rz * 14.0f, 1.2f, 1.2f, 1.2f, RGB(20, 100, 20));

        // DÜZELTİLEN SATIR (484): hz için 0.10f parametresi eklendi
        box3(x + rx * 0.18f, 5.72f, z + rz * 0.18f, 0.12f, 0.10f, 0.10f, RGB(255, 224, 125));
    }
}

// -------------------------------------------------------------------------
// Oyun Mantığı
// -------------------------------------------------------------------------
static void initGame(void) {
    initWaypoints();
    buildMesh();

    unsigned int colors[NUM_KARTS] = {
        RGB(230, 30, 30),  // Mario (Kırmızı)
        RGB(30, 200, 30),  // Luigi (Yeşil)
        RGB(240, 200, 20), // Peach/Yellow
        RGB(30, 100, 230), // Toad/Blue
        RGB(200, 30, 200), // Wario/Purple
        RGB(230, 120, 30)  // Bowser/Orange
    };

    for (int i = 0; i < NUM_KARTS; i++) {
        g_karts[i].pos = g_waypoints[0];
        g_karts[i].pos.x += (i % 2 == 0 ? 2.0f : -2.0f);
        g_karts[i].pos.z -= (i * 2.5f);
        g_karts[i].vel = (Vector3){0, 0, 0};
        g_karts[i].rotY = 0.0f;
        g_karts[i].steerAngle = 0.0f;
        g_karts[i].engineSpeed = 0.0f;
        g_karts[i].driftTimer = 0.0f;
        g_karts[i].driftDir = 0;
        g_karts[i].boostTimer = 0.0f;
        g_karts[i].starTimer = 0.0f;
        g_karts[i].spinTimer = 0.0f;
        g_karts[i].shieldTimer = 0.0f;
        g_karts[i].sizeScale = 1.0f;
        g_karts[i].currentWaypoint = 1;
        g_karts[i].lap = 1;
        g_karts[i].lapProgress = 0.0f;
        g_karts[i].rank = i + 1;
        g_karts[i].currentItem = ITEM_NONE;
        g_karts[i].isAI = (i == 0) ? 0 : 1;
        g_karts[i].bodyColor = colors[i];
    }

    for (int i = 0; i < 8; i++) {
        int wpIdx = (i * 2) % NUM_WAYPOINTS;
        g_itemBoxes[i].pos = g_waypoints[wpIdx];
        g_itemBoxes[i].pos.y = 0.8f;
        g_itemBoxes[i].active = 1;
        g_itemBoxes[i].respawnTimer = 0.0f;
    }

    for (int i = 0; i < 12; i++) {
        g_projectiles[i].active = 0;
    }
}

static void updatePlayer(SceCtrlData *pad, float dt) {
    Kart *p = &g_karts[0];

    if (p->spinTimer > 0.0f) {
        p->spinTimer -= dt;
        p->rotY += 15.0f * dt;
        return;
    }

    float accel = 0.0f;
    if (pad->Buttons & PSP_CTRL_CROSS) accel = 12.0f;
    if (pad->Buttons & PSP_CTRL_SQUARE) accel = -6.0f;

    float steer = 0.0f;
    if (pad->Lx < 80 || (pad->Buttons & PSP_CTRL_LEFT)) steer = 1.0f;
    if (pad->Lx > 175 || (pad->Buttons & PSP_CTRL_RIGHT)) steer = -1.0f;

    // Drift
    if ((pad->Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && fabsf(steer) > 0.1f) {
        p->driftDir = (steer > 0) ? 1 : -1;
        p->driftTimer += dt;
    } else {
        if (p->driftTimer > 1.2f) {
            p->boostTimer = 1.5f; // Mini Turbo
        }
        p->driftTimer = 0.0f;
        p->driftDir = 0;
    }

    if (p->boostTimer > 0.0f) {
        accel += 10.0f;
        p->boostTimer -= dt;
    }

    p->engineSpeed += accel * dt;
    p->engineSpeed *= 0.96f; // Sürtünme

    p->rotY += steer * 2.2f * dt * (p->engineSpeed > 0 ? 1.0f : -1.0f);

    p->pos.x += sinf(p->rotY) * p->engineSpeed * dt;
    p->pos.z += cosf(p->rotY) * p->engineSpeed * dt;

    // Item Kullanımı
    if ((pad->Buttons & PSP_CTRL_CIRCLE) && p->currentItem != ITEM_NONE) {
        if (p->currentItem == ITEM_MUSHROOM) {
            p->boostTimer = 2.0f;
        } else if (p->currentItem == ITEM_STAR) {
            p->starTimer = 5.0f;
        }
        p->currentItem = ITEM_NONE;
    }
}

static void updateAI(Kart *k, float dt) {
    Vector3 target = g_waypoints[k->currentWaypoint];
    float dx = target.x - k->pos.x;
    float dz = target.z - k->pos.z;
    float dist = sqrtf(dx * dx + dz * dz);

    if (dist < 4.0f) {
        k->currentWaypoint = (k->currentWaypoint + 1) % NUM_WAYPOINTS;
    }

    float targetAngle = atan2f(dx, dz);
    float angleDiff = targetAngle - k->rotY;

    while (angleDiff > M_PI) angleDiff -= 2.0f * M_PI;
    while (angleDiff < -M_PI) angleDiff += 2.0f * M_PI;

    k->rotY += angleDiff * 3.0f * dt;
    k->engineSpeed = 9.5f;

    k->pos.x += sinf(k->rotY) * k->engineSpeed * dt;
    k->pos.z += cosf(k->rotY) * k->engineSpeed * dt;
}

static void updateGame(float dt) {
    for (int i = 0; i < NUM_KARTS; i++) {
        if (g_karts[i].isAI) {
            updateAI(&g_karts[i], dt);
        }
    }

    // Item Kutusu Çarpışma Kontrolü
    for (int k = 0; k < NUM_KARTS; k++) {
        for (int b = 0; b < 8; b++) {
            if (!g_itemBoxes[b].active) {
                g_itemBoxes[b].respawnTimer -= dt;
                if (g_itemBoxes[b].respawnTimer <= 0) g_itemBoxes[b].active = 1;
                continue;
            }

            float dx = g_karts[k].pos.x - g_itemBoxes[b].pos.x;
            float dz = g_karts[k].pos.z - g_itemBoxes[b].pos.z;
            if (sqrtf(dx * dx + dz * dz) < 1.8f) {
                g_itemBoxes[b].active = 0;
                g_itemBoxes[b].respawnTimer = 4.0f;
                if (g_karts[k].currentItem == ITEM_NONE) {
                    g_karts[k].currentItem = (rand() % 2 == 0) ? ITEM_MUSHROOM : ITEM_STAR;
                }
            }
        }
    }
}

// -------------------------------------------------------------------------
// Çizim & Render
// -------------------------------------------------------------------------
static void renderKart(Kart *k) {
    sceGumPushMatrix();
    ScePspFVector3 p = {k->pos.x, k->pos.y + 0.3f, k->pos.z};
    sceGumTranslate(&p);
    sceGumRotateY(k->rotY);

    // Kart Gövdesi
    box3(0.0f, 0.2f, 0.0f, 0.6f, 0.2f, 0.9f, k->bodyColor);
    // Tekerlekler
    box3(-0.65f, 0.1f, 0.5f, 0.15f, 0.15f, 0.15f, RGB(20, 20, 20));
    box3(0.65f, 0.1f, 0.5f, 0.15f, 0.15f, 0.15f, RGB(20, 20, 20));
    box3(-0.65f, 0.1f, -0.5f, 0.15f, 0.15f, 0.15f, RGB(20, 20, 20));
    box3(0.65f, 0.1f, -0.5f, 0.15f, 0.15f, 0.15f, RGB(20, 20, 20));

    sceGumPopMatrix();
}

static void renderScene(void) {
    sceGuStart(GU_DIRECT, g_meshVerts);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    // Kamera Ayarları
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(75.0f, 480.0f / 272.0f, 0.5f, 200.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    Kart *p = &g_karts[0];
    float camDist = (g_cameraMode == 1) ? 4.0f : 7.0f;
    float camHeight = (g_cameraMode == 1) ? 1.8f : 3.2f;

    ScePspFVector3 eye = {
        p->pos.x - sinf(p->rotY) * camDist,
        p->pos.y + camHeight,
        p->pos.z - cosf(p->rotY) * camDist
    };
    ScePspFVector3 center = {p->pos.x, p->pos.y + 0.8f, p->pos.z};
    ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    // Pist Çizimi
    sceGuColor(0xFFFFFFFF);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, g_meshCount, 0, g_meshVerts);

    // Kartlar
    for (int i = 0; i < NUM_KARTS; i++) {
        renderKart(&g_karts[i]);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

// -------------------------------------------------------------------------
// Ana Giriş Noktası (Main)
// -------------------------------------------------------------------------
int main(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    // GU Başlatma
    sceGuInit();
    sceGuStart(GU_DIRECT, g_meshVerts);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CCW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);

    initGame();

    SceCtrlData pad;
    int running = 1;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons & PSP_CTRL_TRIANGLE) {
            g_cameraMode = (g_cameraMode + 1) % 2;
            sceKernelDelayThread(150000);
        }

        updatePlayer(&pad, 0.016f);
        updateGame(0.016f);
        renderScene();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
