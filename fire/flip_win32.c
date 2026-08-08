#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define U_FIELD 0
#define V_FIELD 1
#define FLUID_CELL 0
#define AIR_CELL   1
#define SOLID_CELL 2

#define WIN_W 1100
#define WIN_H 760

typedef struct FlipFluid {
    float density;
    int fNumX, fNumY, fNumCells;
    float h, fInvSpacing;
    float *u, *v, *du, *dv, *prevU, *prevV, *p, *s;
    int *cellType;
    float *cellColor;

    int maxParticles, numParticles;
    float *particlePos, *particleVel, *particleColor, *particleDensity;
    float particleRestDensity;
    float particleRadius, pInvSpacing;
    int pNumX, pNumY, pNumCells;
    int *numCellParticles, *firstCellParticle, *cellParticleIds;
} FlipFluid;

typedef struct Scene {
    float gravity, dt, flipRatio, overRelaxation;
    int numPressureIters, numParticleIters;
    int frameNr;
    int compensateDrift, separateParticles;
    float obstacleX, obstacleY, obstacleRadius;
    float obstacleVelX, obstacleVelY;
    int paused, showParticles, showGrid, showObstacle;
    FlipFluid *fluid;
} Scene;

static Scene scene;
static int g_width = WIN_W, g_height = WIN_H;
static float simHeight = 3.0f, simWidth = 4.2f, cScale = 1.0f;
static int mouseDown = 0;
static uint32_t *pixels = NULL;
static BITMAPINFO bmi;

static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) { MessageBoxA(NULL, "Out of memory", "flip", MB_ICONERROR); ExitProcess(1); }
    return p;
}

static FlipFluid *fluid_create(float density, float width, float height, float spacing, float particleRadius, int maxParticles) {
    FlipFluid *f = (FlipFluid*)xcalloc(1, sizeof(*f));
    f->density = density;
    f->fNumX = (int)floorf(width / spacing) + 1;
    f->fNumY = (int)floorf(height / spacing) + 1;
    f->h = fmaxf(width / f->fNumX, height / f->fNumY);
    f->fInvSpacing = 1.0f / f->h;
    f->fNumCells = f->fNumX * f->fNumY;

    f->u = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->v = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->du = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->dv = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->prevU = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->prevV = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->p = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->s = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->cellType = (int*)xcalloc(f->fNumCells, sizeof(int));
    f->cellColor = (float*)xcalloc(3 * f->fNumCells, sizeof(float));

    f->maxParticles = maxParticles;
    f->particlePos = (float*)xcalloc(2 * maxParticles, sizeof(float));
    f->particleVel = (float*)xcalloc(2 * maxParticles, sizeof(float));
    f->particleColor = (float*)xcalloc(3 * maxParticles, sizeof(float));
    for (int i = 0; i < maxParticles; i++) f->particleColor[3 * i + 2] = 1.0f;
    f->particleDensity = (float*)xcalloc(f->fNumCells, sizeof(float));
    f->particleRestDensity = 0.0f;
    f->particleRadius = particleRadius;
    f->pInvSpacing = 1.0f / (2.2f * particleRadius);
    f->pNumX = (int)floorf(width * f->pInvSpacing) + 1;
    f->pNumY = (int)floorf(height * f->pInvSpacing) + 1;
    f->pNumCells = f->pNumX * f->pNumY;
    f->numCellParticles = (int*)xcalloc(f->pNumCells, sizeof(int));
    f->firstCellParticle = (int*)xcalloc(f->pNumCells + 1, sizeof(int));
    f->cellParticleIds = (int*)xcalloc(maxParticles, sizeof(int));
    return f;
}

static void fluid_destroy(FlipFluid *f) {
    if (!f) return;
    free(f->u); free(f->v); free(f->du); free(f->dv); free(f->prevU); free(f->prevV); free(f->p); free(f->s);
    free(f->cellType); free(f->cellColor); free(f->particlePos); free(f->particleVel); free(f->particleColor);
    free(f->particleDensity); free(f->numCellParticles); free(f->firstCellParticle); free(f->cellParticleIds); free(f);
}

static void integrate_particles(FlipFluid *f, float dt, float gravity) {
    for (int i = 0; i < f->numParticles; i++) {
        f->particleVel[2*i + 1] += dt * gravity;
        f->particlePos[2*i] += f->particleVel[2*i] * dt;
        f->particlePos[2*i + 1] += f->particleVel[2*i + 1] * dt;
    }
}

static void push_particles_apart(FlipFluid *f, int numIters) {
    const float colorDiffusionCoeff = 0.001f;
    memset(f->numCellParticles, 0, f->pNumCells * sizeof(int));
    for (int i = 0; i < f->numParticles; i++) {
        float x = f->particlePos[2*i], y = f->particlePos[2*i + 1];
        int xi = clampi((int)floorf(x * f->pInvSpacing), 0, f->pNumX - 1);
        int yi = clampi((int)floorf(y * f->pInvSpacing), 0, f->pNumY - 1);
        f->numCellParticles[xi * f->pNumY + yi]++;
    }
    int first = 0;
    for (int i = 0; i < f->pNumCells; i++) { first += f->numCellParticles[i]; f->firstCellParticle[i] = first; }
    f->firstCellParticle[f->pNumCells] = first;
    for (int i = 0; i < f->numParticles; i++) {
        float x = f->particlePos[2*i], y = f->particlePos[2*i + 1];
        int xi = clampi((int)floorf(x * f->pInvSpacing), 0, f->pNumX - 1);
        int yi = clampi((int)floorf(y * f->pInvSpacing), 0, f->pNumY - 1);
        int cell = xi * f->pNumY + yi;
        f->firstCellParticle[cell]--;
        f->cellParticleIds[f->firstCellParticle[cell]] = i;
    }

    float minDist = 2.0f * f->particleRadius, minDist2 = minDist * minDist;
    for (int iter = 0; iter < numIters; iter++) {
        for (int i = 0; i < f->numParticles; i++) {
            float px = f->particlePos[2*i], py = f->particlePos[2*i + 1];
            int pxi = (int)floorf(px * f->pInvSpacing), pyi = (int)floorf(py * f->pInvSpacing);
            int x0 = pxi - 1 > 0 ? pxi - 1 : 0;
            int y0 = pyi - 1 > 0 ? pyi - 1 : 0;
            int x1 = pxi + 1 < f->pNumX - 1 ? pxi + 1 : f->pNumX - 1;
            int y1 = pyi + 1 < f->pNumY - 1 ? pyi + 1 : f->pNumY - 1;
            for (int xi = x0; xi <= x1; xi++) for (int yi = y0; yi <= y1; yi++) {
                int cell = xi * f->pNumY + yi;
                for (int j = f->firstCellParticle[cell]; j < f->firstCellParticle[cell + 1]; j++) {
                    int id = f->cellParticleIds[j];
                    if (id == i) continue;
                    float qx = f->particlePos[2*id], qy = f->particlePos[2*id + 1];
                    float dx = qx - px, dy = qy - py, d2 = dx*dx + dy*dy;
                    if (d2 > minDist2 || d2 == 0.0f) continue;
                    float d = sqrtf(d2), s = 0.5f * (minDist - d) / d;
                    dx *= s; dy *= s;
                    f->particlePos[2*i] -= dx; f->particlePos[2*i + 1] -= dy;
                    f->particlePos[2*id] += dx; f->particlePos[2*id + 1] += dy;
                    for (int k = 0; k < 3; k++) {
                        float c0 = f->particleColor[3*i+k], c1 = f->particleColor[3*id+k], c = 0.5f * (c0 + c1);
                        f->particleColor[3*i+k] = c0 + (c - c0) * colorDiffusionCoeff;
                        f->particleColor[3*id+k] = c1 + (c - c1) * colorDiffusionCoeff;
                    }
                }
            }
        }
    }
}

static void handle_particle_collisions(FlipFluid *f, float ox, float oy, float obstacleRadius) {
    float h = 1.0f / f->fInvSpacing, r = f->particleRadius;
    float minDist = obstacleRadius + r, minDist2 = minDist * minDist;
    float minX = h + r, maxX = (f->fNumX - 1) * h - r;
    float minY = h + r, maxY = (f->fNumY - 1) * h - r;
    for (int i = 0; i < f->numParticles; i++) {
        float x = f->particlePos[2*i], y = f->particlePos[2*i + 1];
        float dx = x - ox, dy = y - oy;
        if (dx*dx + dy*dy < minDist2) {
            f->particleVel[2*i] = scene.obstacleVelX;
            f->particleVel[2*i + 1] = scene.obstacleVelY;
        }
        if (x < minX) { x = minX; f->particleVel[2*i] = 0.0f; }
        if (x > maxX) { x = maxX; f->particleVel[2*i] = 0.0f; }
        if (y < minY) { y = minY; f->particleVel[2*i + 1] = 0.0f; }
        if (y > maxY) { y = maxY; f->particleVel[2*i + 1] = 0.0f; }
        f->particlePos[2*i] = x; f->particlePos[2*i + 1] = y;
    }
}

static void update_particle_density(FlipFluid *f) {
    int n = f->fNumY;
    float h = f->h, h1 = f->fInvSpacing, h2 = 0.5f * h;
    memset(f->particleDensity, 0, f->fNumCells * sizeof(float));
    for (int i = 0; i < f->numParticles; i++) {
        float x = clampf(f->particlePos[2*i], h, (f->fNumX - 1) * h);
        float y = clampf(f->particlePos[2*i + 1], h, (f->fNumY - 1) * h);
        int x0 = (int)floorf((x - h2) * h1);
        float tx = ((x - h2) - x0 * h) * h1;
        int x1 = x0 + 1 < f->fNumX - 2 ? x0 + 1 : f->fNumX - 2;
        int y0 = (int)floorf((y - h2) * h1);
        float ty = ((y - h2) - y0 * h) * h1;
        int y1 = y0 + 1 < f->fNumY - 2 ? y0 + 1 : f->fNumY - 2;
        float sx = 1.0f - tx, sy = 1.0f - ty;
        if (x0 >= 0 && x0 < f->fNumX && y0 >= 0 && y0 < f->fNumY) f->particleDensity[x0*n+y0] += sx*sy;
        if (x1 >= 0 && x1 < f->fNumX && y0 >= 0 && y0 < f->fNumY) f->particleDensity[x1*n+y0] += tx*sy;
        if (x1 >= 0 && x1 < f->fNumX && y1 >= 0 && y1 < f->fNumY) f->particleDensity[x1*n+y1] += tx*ty;
        if (x0 >= 0 && x0 < f->fNumX && y1 >= 0 && y1 < f->fNumY) f->particleDensity[x0*n+y1] += sx*ty;
    }
    if (f->particleRestDensity == 0.0f) {
        float sum = 0.0f; int num = 0;
        for (int i = 0; i < f->fNumCells; i++) if (f->cellType[i] == FLUID_CELL) { sum += f->particleDensity[i]; num++; }
        if (num > 0) f->particleRestDensity = sum / num;
    }
}

static void transfer_velocities(FlipFluid *f, int toGrid, float flipRatio) {
    int n = f->fNumY;
    float h = f->h, h1 = f->fInvSpacing, h2 = 0.5f * h;
    if (toGrid) {
        memcpy(f->prevU, f->u, f->fNumCells * sizeof(float));
        memcpy(f->prevV, f->v, f->fNumCells * sizeof(float));
        memset(f->du, 0, f->fNumCells * sizeof(float)); memset(f->dv, 0, f->fNumCells * sizeof(float));
        memset(f->u, 0, f->fNumCells * sizeof(float)); memset(f->v, 0, f->fNumCells * sizeof(float));
        for (int i = 0; i < f->fNumCells; i++) f->cellType[i] = (f->s[i] == 0.0f) ? SOLID_CELL : AIR_CELL;
        for (int i = 0; i < f->numParticles; i++) {
            int xi = clampi((int)floorf(f->particlePos[2*i] * h1), 0, f->fNumX - 1);
            int yi = clampi((int)floorf(f->particlePos[2*i + 1] * h1), 0, f->fNumY - 1);
            int cell = xi * n + yi;
            if (f->cellType[cell] == AIR_CELL) f->cellType[cell] = FLUID_CELL;
        }
    }
    for (int comp = 0; comp < 2; comp++) {
        float dx = comp == 0 ? 0.0f : h2, dy = comp == 0 ? h2 : 0.0f;
        float *field = comp == 0 ? f->u : f->v;
        float *prev = comp == 0 ? f->prevU : f->prevV;
        float *w = comp == 0 ? f->du : f->dv;
        for (int i = 0; i < f->numParticles; i++) {
            float x = clampf(f->particlePos[2*i], h, (f->fNumX - 1) * h);
            float y = clampf(f->particlePos[2*i + 1], h, (f->fNumY - 1) * h);
            int x0 = (int)floorf((x - dx) * h1); if (x0 > f->fNumX - 2) x0 = f->fNumX - 2; if (x0 < 0) x0 = 0;
            float tx = ((x - dx) - x0 * h) * h1;
            int x1 = x0 + 1 < f->fNumX - 2 ? x0 + 1 : f->fNumX - 2;
            int y0 = (int)floorf((y - dy) * h1); if (y0 > f->fNumY - 2) y0 = f->fNumY - 2; if (y0 < 0) y0 = 0;
            float ty = ((y - dy) - y0 * h) * h1;
            int y1 = y0 + 1 < f->fNumY - 2 ? y0 + 1 : f->fNumY - 2;
            float sx = 1.0f - tx, sy = 1.0f - ty;
            float d0=sx*sy, d1=tx*sy, d2=tx*ty, d3=sx*ty;
            int nr0=x0*n+y0, nr1=x1*n+y0, nr2=x1*n+y1, nr3=x0*n+y1;
            if (toGrid) {
                float pv = f->particleVel[2*i + comp];
                field[nr0] += pv*d0; w[nr0] += d0; field[nr1] += pv*d1; w[nr1] += d1;
                field[nr2] += pv*d2; w[nr2] += d2; field[nr3] += pv*d3; w[nr3] += d3;
            } else {
                int off = comp == 0 ? n : 1;
                float valid0 = (nr0 - off >= 0 && (f->cellType[nr0] != AIR_CELL || f->cellType[nr0-off] != AIR_CELL)) ? 1.0f : 0.0f;
                float valid1 = (nr1 - off >= 0 && (f->cellType[nr1] != AIR_CELL || f->cellType[nr1-off] != AIR_CELL)) ? 1.0f : 0.0f;
                float valid2 = (nr2 - off >= 0 && (f->cellType[nr2] != AIR_CELL || f->cellType[nr2-off] != AIR_CELL)) ? 1.0f : 0.0f;
                float valid3 = (nr3 - off >= 0 && (f->cellType[nr3] != AIR_CELL || f->cellType[nr3-off] != AIR_CELL)) ? 1.0f : 0.0f;
                float denom = valid0*d0 + valid1*d1 + valid2*d2 + valid3*d3;
                if (denom > 0.0f) {
                    float picV = (valid0*d0*field[nr0] + valid1*d1*field[nr1] + valid2*d2*field[nr2] + valid3*d3*field[nr3]) / denom;
                    float corr = (valid0*d0*(field[nr0]-prev[nr0]) + valid1*d1*(field[nr1]-prev[nr1]) + valid2*d2*(field[nr2]-prev[nr2]) + valid3*d3*(field[nr3]-prev[nr3])) / denom;
                    float flipV = f->particleVel[2*i + comp] + corr;
                    f->particleVel[2*i + comp] = (1.0f - flipRatio) * picV + flipRatio * flipV;
                }
            }
        }
        if (toGrid) {
            for (int i = 0; i < f->fNumCells; i++) if (w[i] > 0.0f) field[i] /= w[i];
            for (int i = 0; i < f->fNumX; i++) for (int j = 0; j < f->fNumY; j++) {
                int c = i*n+j;
                int solid = f->cellType[c] == SOLID_CELL;
                if (solid || (i > 0 && f->cellType[(i-1)*n+j] == SOLID_CELL)) f->u[c] = f->prevU[c];
                if (solid || (j > 0 && f->cellType[i*n+j-1] == SOLID_CELL)) f->v[c] = f->prevV[c];
            }
        }
    }
}

static void solve_incompressibility(FlipFluid *f, int numIters, float dt, float overRelaxation, int compensateDrift) {
    memset(f->p, 0, f->fNumCells * sizeof(float));
    memcpy(f->prevU, f->u, f->fNumCells * sizeof(float));
    memcpy(f->prevV, f->v, f->fNumCells * sizeof(float));
    int n = f->fNumY;
    float cp = f->density * f->h / dt;
    for (int iter = 0; iter < numIters; iter++) for (int i = 1; i < f->fNumX - 1; i++) for (int j = 1; j < f->fNumY - 1; j++) {
        int center = i*n+j;
        if (f->cellType[center] != FLUID_CELL) continue;
        int left=(i-1)*n+j, right=(i+1)*n+j, bottom=i*n+j-1, top=i*n+j+1;
        float sx0=f->s[left], sx1=f->s[right], sy0=f->s[bottom], sy1=f->s[top];
        float ss = sx0 + sx1 + sy0 + sy1;
        if (ss == 0.0f) continue;
        float div = f->u[right] - f->u[center] + f->v[top] - f->v[center];
        if (f->particleRestDensity > 0.0f && compensateDrift) {
            float compression = f->particleDensity[center] - f->particleRestDensity;
            if (compression > 0.0f) div -= compression;
        }
        float pp = -div / ss * overRelaxation;
        f->p[center] += cp * pp;
        f->u[center] -= sx0 * pp; f->u[right] += sx1 * pp;
        f->v[center] -= sy0 * pp; f->v[top] += sy1 * pp;
    }
}

static void set_sci_color(FlipFluid *f, int cell, float val, float minVal, float maxVal) {
    val = clampf(val, minVal, maxVal - 0.0001f);
    float d = maxVal - minVal;
    val = d == 0.0f ? 0.5f : (val - minVal) / d;
    float m = 0.25f;
    int num = (int)floorf(val / m);
    float s = (val - num * m) / m;
    float r=0,g=0,b=0;
    switch (num) { case 0: r=0; g=s; b=1; break; case 1: r=0; g=1; b=1-s; break; case 2: r=s; g=1; b=0; break; default: r=1; g=1-s; b=0; break; }
    f->cellColor[3*cell]=r; f->cellColor[3*cell+1]=g; f->cellColor[3*cell+2]=b;
}

static void update_particle_colors(FlipFluid *f) {
    float h1 = f->fInvSpacing;
    for (int i = 0; i < f->numParticles; i++) {
        float s = 0.01f;
        f->particleColor[3*i] = clampf(f->particleColor[3*i] - s, 0.0f, 1.0f);
        f->particleColor[3*i + 1] = clampf(f->particleColor[3*i + 1] - s, 0.0f, 1.0f);
        f->particleColor[3*i + 2] = clampf(f->particleColor[3*i + 2] + s, 0.0f, 1.0f);
        int xi = clampi((int)floorf(f->particlePos[2*i] * h1), 1, f->fNumX - 1);
        int yi = clampi((int)floorf(f->particlePos[2*i + 1] * h1), 1, f->fNumY - 1);
        int cell = xi * f->fNumY + yi;
        if (f->particleRestDensity > 0.0f && f->particleDensity[cell] / f->particleRestDensity < 0.7f) {
            f->particleColor[3*i] = 0.8f; f->particleColor[3*i + 1] = 0.8f; f->particleColor[3*i + 2] = 1.0f;
        }
    }
}

static void update_cell_colors(FlipFluid *f) {
    memset(f->cellColor, 0, 3 * f->fNumCells * sizeof(float));
    for (int i = 0; i < f->fNumCells; i++) {
        if (f->cellType[i] == SOLID_CELL) { f->cellColor[3*i]=0.5f; f->cellColor[3*i+1]=0.5f; f->cellColor[3*i+2]=0.5f; }
        else if (f->cellType[i] == FLUID_CELL) {
            float d = f->particleDensity[i]; if (f->particleRestDensity > 0.0f) d /= f->particleRestDensity;
            set_sci_color(f, i, d, 0.0f, 2.0f);
        }
    }
}

static void fluid_simulate(FlipFluid *f, float dt, float gravity, float flipRatio, int pressureIters, int particleIters, float overRelaxation, int compensateDrift, int separateParticles, float ox, float oy, float obstacleRadius) {
    integrate_particles(f, dt, gravity);
    if (separateParticles) push_particles_apart(f, particleIters);
    handle_particle_collisions(f, ox, oy, obstacleRadius);
    transfer_velocities(f, 1, flipRatio);
    update_particle_density(f);
    solve_incompressibility(f, pressureIters, dt, overRelaxation, compensateDrift);
    transfer_velocities(f, 0, flipRatio);
    update_particle_colors(f);
    update_cell_colors(f);
}

static void set_obstacle(float x, float y, int reset) {
    FlipFluid *f = scene.fluid;
    if (!f) return;
    float vx = 0.0f, vy = 0.0f;
    if (!reset) { vx = (x - scene.obstacleX) / scene.dt; vy = (y - scene.obstacleY) / scene.dt; }
    scene.obstacleX = x; scene.obstacleY = y;
    int n = f->fNumY;
    for (int i = 1; i < f->fNumX - 2; i++) for (int j = 1; j < f->fNumY - 2; j++) {
        int c = i*n+j;
        f->s[c] = 1.0f;
        float dx = (i + 0.5f) * f->h - x, dy = (j + 0.5f) * f->h - y;
        if (dx*dx + dy*dy < scene.obstacleRadius * scene.obstacleRadius) {
            f->s[c] = 0.0f;
            f->u[c] = vx; f->u[(i+1)*n+j] = vx; f->v[c] = vy; f->v[i*n+j+1] = vy;
        }
    }
    scene.obstacleVelX = vx; scene.obstacleVelY = vy; scene.showObstacle = 1;
}

static void setup_scene(void) {
    if (scene.fluid) fluid_destroy(scene.fluid);
    memset(&scene, 0, sizeof(scene));
    scene.gravity = -9.81f; scene.dt = 1.0f / 60.0f; scene.flipRatio = 0.9f;
    scene.numPressureIters = 50; scene.numParticleIters = 2; scene.overRelaxation = 1.9f;
    scene.compensateDrift = 1; scene.separateParticles = 1; scene.paused = 0; scene.showParticles = 1; scene.showGrid = 0;
    scene.obstacleRadius = 0.15f;

    int res = 100;
    float tankHeight = simHeight, tankWidth = simWidth;
    float h = tankHeight / res, density = 1000.0f;
    float relWaterHeight = 0.8f, relWaterWidth = 0.6f;
    float r = 0.3f * h, dx = 2.0f * r, dy = sqrtf(3.0f) * 0.5f * dx;
    int numX = (int)floorf((relWaterWidth * tankWidth - 2.0f*h - 2.0f*r) / dx);
    int numY = (int)floorf((relWaterHeight * tankHeight - 2.0f*h - 2.0f*r) / dy);
    int maxParticles = numX * numY;
    FlipFluid *f = scene.fluid = fluid_create(density, tankWidth, tankHeight, h, r, maxParticles);
    f->numParticles = maxParticles;
    int p = 0;
    for (int i = 0; i < numX; i++) for (int j = 0; j < numY; j++) {
        f->particlePos[p++] = h + r + dx * i + (j % 2 == 0 ? 0.0f : r);
        f->particlePos[p++] = h + r + dy * j;
    }
    int n = f->fNumY;
    for (int i = 0; i < f->fNumX; i++) for (int j = 0; j < f->fNumY; j++) f->s[i*n+j] = (i == 0 || i == f->fNumX - 1 || j == 0) ? 0.0f : 1.0f;
    set_obstacle(0.5f * simWidth, 0.67f * simHeight, 1);
}

static void resize_backbuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    g_width = w; g_height = h;
    free(pixels);
    pixels = (uint32_t*)xcalloc((size_t)w * h, sizeof(uint32_t));
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    cScale = (float)g_height / simHeight;
    simWidth = (float)g_width / cScale;
    setup_scene();
}

static uint32_t rgbf(float r, float g, float b) {
    int R = clampi((int)(r * 255.0f), 0, 255), G = clampi((int)(g * 255.0f), 0, 255), B = clampi((int)(b * 255.0f), 0, 255);
    return (uint32_t)(R | (G << 8) | (B << 16));
}
static void put_pixel(int x, int y, uint32_t c) { if ((unsigned)x < (unsigned)g_width && (unsigned)y < (unsigned)g_height) pixels[y * g_width + x] = c; }
static int sx(float x) { return (int)(x * cScale); }
static int sy(float y) { return g_height - 1 - (int)(y * cScale); }

static void fill_circle(int cx, int cy, int r, uint32_t color) {
    int r2 = r*r;
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x*x + y*y <= r2) put_pixel(cx+x, cy+y, color);
}
static void fill_rect(int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 > x1) { int t=x0; x0=x1; x1=t; } if (y0 > y1) { int t=y0; y0=y1; y1=t; }
    x0 = clampi(x0, 0, g_width-1); x1 = clampi(x1, 0, g_width-1); y0 = clampi(y0, 0, g_height-1); y1 = clampi(y1, 0, g_height-1);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) pixels[y*g_width+x] = c;
}

static void draw_scene(HDC hdc) {
    memset(pixels, 0, (size_t)g_width * g_height * sizeof(uint32_t));
    FlipFluid *f = scene.fluid;
    if (!f) return;
    if (scene.showGrid) {
        int cellPix = (int)(0.9f * f->h * cScale);
        if (cellPix < 1) cellPix = 1;
        for (int i = 0; i < f->fNumX; i++) for (int j = 0; j < f->fNumY; j++) {
            int c = i * f->fNumY + j;
            uint32_t col = rgbf(f->cellColor[3*c], f->cellColor[3*c+1], f->cellColor[3*c+2]);
            int x = sx((i + 0.5f) * f->h), y = sy((j + 0.5f) * f->h);
            fill_rect(x - cellPix/2, y - cellPix/2, x + cellPix/2, y + cellPix/2, col);
        }
    }
    if (scene.showParticles) {
        int pr = (int)(f->particleRadius * cScale);
        if (pr < 2) pr = 2;
        for (int i = 0; i < f->numParticles; i++) {
            uint32_t col = rgbf(f->particleColor[3*i], f->particleColor[3*i+1], f->particleColor[3*i+2]);
            fill_circle(sx(f->particlePos[2*i]), sy(f->particlePos[2*i+1]), pr, col);
        }
    }
    fill_circle(sx(scene.obstacleX), sy(scene.obstacleY), (int)((scene.obstacleRadius + f->particleRadius) * cScale), 0x000000FFu);
    StretchDIBits(hdc, 0, 0, g_width, g_height, 0, 0, g_width, g_height, pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static void screen_to_world(int mx, int my, float *x, float *y) { *x = (float)mx / cScale; *y = (float)(g_height - my) / cScale; }

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: SetTimer(hwnd, 1, 16, NULL); return 0;
    case WM_SIZE: resize_backbuffer(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_TIMER:
        if (!scene.paused && scene.fluid) {
            fluid_simulate(scene.fluid, scene.dt, scene.gravity, scene.flipRatio, scene.numPressureIters, scene.numParticleIters, scene.overRelaxation, scene.compensateDrift, scene.separateParticles, scene.obstacleX, scene.obstacleY, scene.obstacleRadius);
            scene.frameNr++;
        }
        InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd); mouseDown = 1; float x,y; screen_to_world(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &x, &y); set_obstacle(x,y,1); scene.paused = 0; return 0;
    }
    case WM_MOUSEMOVE:
        if (mouseDown) { float x,y; screen_to_world(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &x, &y); set_obstacle(x,y,0); }
        return 0;
    case WM_LBUTTONUP:
        mouseDown = 0; ReleaseCapture(); scene.obstacleVelX = scene.obstacleVelY = 0.0f; return 0;
    case WM_KEYDOWN:
        if (wp == 'P' || wp == VK_SPACE) scene.paused = !scene.paused;
        else if (wp == 'M') { if (scene.fluid) fluid_simulate(scene.fluid, scene.dt, scene.gravity, scene.flipRatio, scene.numPressureIters, scene.numParticleIters, scene.overRelaxation, scene.compensateDrift, scene.separateParticles, scene.obstacleX, scene.obstacleY, scene.obstacleRadius); }
        else if (wp == 'G') scene.showGrid = !scene.showGrid;
        else if (wp == 'R') setup_scene();
        else if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); draw_scene(hdc); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1); fluid_destroy(scene.fluid); scene.fluid = NULL; free(pixels); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wnd_proc; wc.hInstance = hInst; wc.lpszClassName = "PureCFlipFluidWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Pure C FLIP Fluid - P/Space pause, G grid, R reset, mouse drag obstacle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H, NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
