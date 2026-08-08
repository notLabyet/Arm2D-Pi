#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include "flip_rp2040_q16.h"

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define FLIP_MEM_ALIGN4(p)      ((uint8_t *)((((uintptr_t)(p)) + 3u) & ~(uintptr_t)3u))
#define FLIP_Q16_0_3            ((q16_t)19661)
#define FLIP_Q16_0_6            ((q16_t)39322)
#define FLIP_Q16_0_7            ((q16_t)45875)
#define FLIP_Q16_0_8            ((q16_t)52429)
#define FLIP_Q16_2              ((q16_t)0x00020000)
#define FLIP_Q16_1000           ((q16_t)0x03E80000)
#define FLIP_NORMALIZATION_EPSILON_Q16 ((q16_t)4)
#define FLIP_WEIGHT_SHIFT       4u
#define FLIP_WEIGHT_HALF_LSB    (1u << (FLIP_WEIGHT_SHIFT - 1u))

/*
 * 96 KB hard budget version:
 * default visible grid is 48x48 (50x50 internal cells) so particle separation
 * can stay enabled under the 96 KB cap.
 */
#ifndef FLIP_MEM_SIZE
#define FLIP_MEM_SIZE           (64u * 1024u)
#endif

static uint32_t s_wFlipMem[(FLIP_MEM_SIZE + sizeof(uint32_t) - 1u) / sizeof(uint32_t)];
static flip_fluid_t s_tFluid;
static int16_t s_nGridW = FLIP_GRID_W;
static int16_t s_nGridH = FLIP_GRID_H;
static int16_t s_nMaxParticles = FLIP_MAX_PARTICLES;

flip_scene_t flip_scene;

__STATIC_INLINE q16_t flip_mul(q16_t a, q16_t b)
{
#if defined(__ARM_ARCH_6M__)
    const uint8_t negative = ((a < 0) != (b < 0));
    const uint32_t aBits = (uint32_t)a;
    const uint32_t bBits = (uint32_t)b;
    const uint32_t ua = (a < 0) ? (~aBits + 1u) : aBits;
    const uint32_t ub = (b < 0) ? (~bBits + 1u) : bBits;
    const uint32_t aLo = ua & UINT16_MAX;
    const uint32_t bLo = ub & UINT16_MAX;
    const uint32_t aHi = ua >> 16;
    const uint32_t bHi = ub >> 16;
    const uint32_t p0 = aLo * bLo;
    const uint32_t p1 = aLo * bHi;
    const uint32_t p2 = aHi * bLo;
    const uint32_t p3 = aHi * bHi;
    const uint32_t carry = (p0 >> 16)
                         + (p1 & UINT16_MAX)
                         + (p2 & UINT16_MAX);
    const uint32_t middle = (carry >> 16)
                          + (p1 >> 16)
                          + (p2 >> 16)
                          + (p3 & UINT16_MAX);
    const uint32_t upper = (middle >> 16) + (p3 >> 16);
    const uint32_t magnitude = ((middle & UINT16_MAX) << 16)
                             | (carry & UINT16_MAX);

    /* Exact saturating Q16 multiply without the ARMv6-M 64-bit helper. */
    if (0u != upper) {
        return negative ? INT32_MIN : INT32_MAX;
    }
    if (!negative) {
        return (magnitude > INT32_MAX) ? INT32_MAX : (q16_t)magnitude;
    }
    if (magnitude >= (UINT32_C(1) << 31)) {
        return INT32_MIN;
    }
    return -(q16_t)magnitude;
#else
    int64_t product = (int64_t)a * (int64_t)b;
    if (product >= 0) {
        uint64_t scaled = (uint64_t)product >> 16;
        if (scaled > INT32_MAX) return INT32_MAX;
        return (q16_t)scaled;
    }

    /* Avoid implementation-defined right shifts of negative integers. */
    uint64_t magnitude = (uint64_t)(-(product + 1)) + 1u;
    uint64_t scaled = magnitude >> 16;
    if (scaled >= (UINT64_C(1) << 31)) return INT32_MIN;
    return -(q16_t)scaled;
#endif
}

__STATIC_INLINE q16_t flip_div(q16_t a, q16_t b)
{
    if (0 == b) {
        return 0;
    }
    if (0 == a) {
        return 0;
    }
    if (FLIP_Q16_ONE == b) {
        return a;
    }

    const uint8_t negative = ((a < 0) != (b < 0));
    const uint32_t numerator = (a < 0) ? (uint32_t)(-(int64_t)a) : (uint32_t)a;
    const uint32_t denominator = (b < 0) ? (uint32_t)(-(int64_t)b) : (uint32_t)b;
    const uint32_t whole = numerator / denominator;
    uint32_t remainder = numerator % denominator;

    if ((!negative && whole > 32767u) || (negative && whole > 32768u)) {
        return negative ? INT32_MIN : INT32_MAX;
    }

    uint32_t fraction = 0;
    for (uint_fast8_t bit = 0; bit < 16; bit++) {
        fraction <<= 1;
        if (remainder >= (denominator - remainder)) {
            remainder -= denominator - remainder;
            fraction |= 1u;
        } else {
            remainder += remainder;
        }
    }

    uint64_t magnitude = (uint64_t)whole * FLIP_Q16_ONE + fraction;
    if (!negative) {
        return (magnitude > INT32_MAX) ? INT32_MAX : (q16_t)magnitude;
    }
    if (magnitude >= (UINT64_C(1) << 31)) return INT32_MIN;
    return -(q16_t)magnitude;
}

__STATIC_INLINE q16_t flip_mul_n(q16_t a, int32_t n)
{
    int64_t product = (int64_t)a * n;
    if (product > INT32_MAX) return INT32_MAX;
    if (product < INT32_MIN) return INT32_MIN;
    return (q16_t)product;
}

__STATIC_INLINE q16_t flip_clamp(q16_t x, q16_t lo, q16_t hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

__STATIC_INLINE int16_t flip_clampi16(int32_t x, int16_t lo, int16_t hi)
{
    return (int16_t)(x < lo ? lo : (x > hi ? hi : x));
}

static uint32_t flip_isqrt_u64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 46;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t)result;
}

__STATIC_INLINE q16_t flip_sqrt_q16(q16_t x)
{
    if (x <= 0) {
        return 0;
    }
    return (q16_t)flip_isqrt_u64((uint64_t)(uint32_t)x * FLIP_Q16_ONE);
}

__STATIC_INLINE void flip_u16_sat_add(uint16_t *p, uint16_t v)
{
    uint32_t s = (uint32_t)(*p) + v;
    *p = (s > 0xFFFFu) ? 0xFFFFu : (uint16_t)s;
}

__STATIC_INLINE q16_t flip_limit_velocity(q16_t v)
{
    if (v > FLIP_VELOCITY_LIMIT_Q16) return FLIP_VELOCITY_LIMIT_Q16;
    if (v < -FLIP_VELOCITY_LIMIT_Q16) return -FLIP_VELOCITY_LIMIT_Q16;
    return v;
}

__STATIC_INLINE q16_t flip_div_small_positive(q16_t value, uint8_t divisor)
{
    switch (divisor) {
        case 1u: return value;
        case 2u: return value / 2;
        case 3u: return value / 3;
        case 4u: return value / 4;
        default: return 0;
    }
}


__STATIC_INLINE uint16_t flip_q16_to_weight(q16_t x)
{
    if (x <= 0) return 0;
    uint32_t weight = ((uint32_t)x + FLIP_WEIGHT_HALF_LSB) >> FLIP_WEIGHT_SHIFT;
    if (weight > UINT16_MAX) return UINT16_MAX;
    return (uint16_t)weight;
}

__STATIC_INLINE uint16_t flip_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8)
                    | ((uint16_t)(g & 0xFC) << 3)
                    | ((uint16_t)b >> 3));
}


static void *flip_mem_alloc(uint8_t **pp, uint8_t *pEnd, size_t nBytes, uint8_t align4)
{
    uint8_t *p = *pp;
    if (align4) {
        p = FLIP_MEM_ALIGN4(p);
    }
    if ((size_t)(pEnd - p) < nBytes) {
        return NULL;
    }
    memset(p, 0, nBytes);
    *pp = p + nBytes;
    return p;
}

static int flip_fluid_create(flip_fluid_t *f, int16_t gridW, int16_t gridH, int16_t maxParticles)
{
    memset(f, 0, sizeof(*f));

    f->numX = (int16_t)(gridW + 2);
    f->numY = (int16_t)(gridH + 2);
    f->numCells = (int16_t)((int32_t)f->numX * f->numY);
    f->density = FLIP_Q16_1000;
    f->h = (q16_t)(FLIP_Q16_ONE / gridH);
    f->hInv = flip_div(FLIP_Q16_ONE, f->h);
    f->hHalf = f->h >> 1;
    f->maxX = flip_mul_n(f->h, f->numX - 1);
    f->maxY = flip_mul_n(f->h, f->numY - 1);
    f->particleRadius = flip_mul(f->h, FLIP_Q16_0_3);
    f->particleDiameter = f->particleRadius * 2;
    q16_t particleBinSize = flip_mul(f->particleRadius, FLIP_PARTICLE_CELL_SCALE_Q16);
    if (particleBinSize <= 0) {
        return -1;
    }
    f->pInvSpacing = flip_div(FLIP_Q16_ONE, particleBinSize);
    f->pNumX = (int16_t)((int64_t)f->maxX / particleBinSize);
    f->pNumY = (int16_t)((int64_t)f->maxY / particleBinSize);
    if (f->pNumX < 2) f->pNumX = 2;
    if (f->pNumY < 2) f->pNumY = 2;
    f->pNumX += 1;
    f->pNumY += 1;
    if (((int32_t)f->pNumX * f->pNumY) > INT16_MAX) {
        return -1;
    }
    f->pNumCells = (int16_t)((int32_t)f->pNumX * f->pNumY);
    f->maxParticles = maxParticles;

    uint8_t *p = (uint8_t *)s_wFlipMem;
    uint8_t *pEnd = p + FLIP_MEM_SIZE;

    f->u = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(q16_t), 1);
    f->v = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(q16_t), 1);
    f->du = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(uint16_t), 1);
    f->dv = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(uint16_t), 1);
    f->prevU = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(q16_t), 1);
    f->prevV = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(q16_t), 1);
    f->particleDensity = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(uint16_t), 1);
    f->cellType = (uint8_t *)flip_mem_alloc(&p, pEnd, (size_t)f->numCells * sizeof(uint8_t), 0);

    f->particlePos = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)maxParticles * 2u * sizeof(q16_t), 1);
    f->particleVel = (q16_t *)flip_mem_alloc(&p, pEnd, (size_t)maxParticles * 2u * sizeof(q16_t), 1);
    f->particleColor = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)maxParticles * sizeof(uint16_t), 1);
    f->numCellParticles = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)f->pNumCells * sizeof(uint16_t), 1);
    f->firstCellParticle = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)(f->pNumCells + 1) * sizeof(uint16_t), 1);
    f->cellParticleIds = (uint16_t *)flip_mem_alloc(&p, pEnd, (size_t)maxParticles * sizeof(uint16_t), 1);

    if (!f->u || !f->v || !f->du || !f->dv || !f->prevU || !f->prevV
     || !f->particleDensity || !f->cellType || !f->particlePos
     || !f->particleVel || !f->particleColor || !f->numCellParticles
     || !f->firstCellParticle || !f->cellParticleIds) {
        return -1;
    }

    const int16_t n = f->numY;
    for (int16_t i = 0; i < f->numX; i++) {
        for (int16_t j = 0; j < f->numY; j++) {
            const int16_t c = i * n + j;
            f->cellType[c] = (i == 0 || i == f->numX - 1 || j == 0)
                           ? FLIP_SOLID_CELL
                           : FLIP_AIR_CELL;
        }
    }

    return 0;
}

static void flip_init_particles(flip_fluid_t *f)
{
    const q16_t domainW = flip_mul_n(f->h, f->numX - 2) - f->particleDiameter;
    const q16_t domainH = flip_mul_n(f->h, f->numY - 2) - f->particleDiameter;
    const q16_t waterW = flip_mul(domainW, FLIP_Q16_0_6);
    const q16_t waterH = flip_mul(domainH, FLIP_Q16_0_8);
    const q16_t startX = f->h + f->particleRadius;
    const q16_t startY = f->h + f->particleRadius;

    uint64_t columnEstimate = ((uint64_t)(uint16_t)f->maxParticles * (uint32_t)waterW)
                            / (uint32_t)MAX(waterH, 1);
    int16_t columns = (int16_t)flip_isqrt_u64(columnEstimate);
    uint64_t lowerError = columnEstimate - (uint64_t)columns * columns;
    uint64_t upperError = (uint64_t)(columns + 1) * (columns + 1) - columnEstimate;
    if (upperError < lowerError) columns++;
    if (columns < 1) columns = 1;
    if (columns > f->maxParticles) columns = f->maxParticles;

    int16_t rows = (int16_t)((f->maxParticles + columns - 1) / columns);
    if (rows < 1) rows = 1;
    const q16_t dx = waterW / columns;
    const q16_t dy = waterH / rows;

    int16_t count = 0;
    for (int16_t row = 0; row < rows && count < f->maxParticles; row++) {
        q16_t y = startY + dy / 2 + flip_mul_n(dy, row);
        for (int16_t column = 0; column < columns && count < f->maxParticles; column++) {
            q16_t x = startX + dx / 2 + flip_mul_n(dx, column);
            f->particlePos[count * 2] = x;
            f->particlePos[count * 2 + 1] = y;
            f->particleVel[count * 2] = 0;
            f->particleVel[count * 2 + 1] = 0;
            f->particleColor[count] = flip_rgb565(80, 150, 255);
            count++;
        }
    }
    f->numParticles = count;
}

static void flip_integrate_particles(flip_fluid_t *f, q16_t dt, q16_t gravityX, q16_t gravityY)
{
    const q16_t dgx = flip_mul(dt, gravityX);
    const q16_t dgy = flip_mul(dt, gravityY);
    for (int16_t i = 0; i < f->numParticles; i++) {
        q16_t *v = &f->particleVel[i * 2];
        q16_t *p = &f->particlePos[i * 2];
        v[0] = flip_limit_velocity(v[0] + dgx);
        v[1] = flip_limit_velocity(v[1] + dgy);
        p[0] += flip_mul(v[0], dt);
        p[1] += flip_mul(v[1], dt);
    }
}

static void flip_build_particle_bins(flip_fluid_t *f)
{
    memset(f->numCellParticles, 0, (size_t)f->pNumCells * sizeof(uint16_t));

    for (int16_t i = 0; i < f->numParticles; i++) {
        int16_t xi = flip_clampi16(flip_mul(f->particlePos[2 * i], f->pInvSpacing) >> 16, 0, f->pNumX - 1);
        int16_t yi = flip_clampi16(flip_mul(f->particlePos[2 * i + 1], f->pInvSpacing) >> 16, 0, f->pNumY - 1);
        f->numCellParticles[xi * f->pNumY + yi]++;
    }

    uint16_t first = 0;
    for (int16_t i = 0; i < f->pNumCells; i++) {
        first += f->numCellParticles[i];
        f->firstCellParticle[i] = first;
    }
    f->firstCellParticle[f->pNumCells] = first;

    for (int16_t i = 0; i < f->numParticles; i++) {
        int16_t xi = flip_clampi16(flip_mul(f->particlePos[2 * i], f->pInvSpacing) >> 16, 0, f->pNumX - 1);
        int16_t yi = flip_clampi16(flip_mul(f->particlePos[2 * i + 1], f->pInvSpacing) >> 16, 0, f->pNumY - 1);
        uint16_t *slot = &f->firstCellParticle[xi * f->pNumY + yi];
        --(*slot);
        f->cellParticleIds[*slot] = (uint16_t)i;
    }
}

static void flip_push_particles_apart(flip_fluid_t *f, int16_t numIters)
{
    const q16_t minDist = f->particleDiameter;
    const q16_t minDist2 = flip_mul(minDist, minDist);

    for (int16_t iter = 0; iter < numIters; iter++) {
        flip_build_particle_bins(f);

        for (int16_t i = 0; i < f->numParticles; i++) {
            q16_t px = f->particlePos[2 * i];
            q16_t py = f->particlePos[2 * i + 1];
            int16_t pxi = flip_mul(px, f->pInvSpacing) >> 16;
            int16_t pyi = flip_mul(py, f->pInvSpacing) >> 16;
            int16_t x0 = flip_clampi16(pxi - 1, 0, f->pNumX - 1);
            int16_t y0 = flip_clampi16(pyi - 1, 0, f->pNumY - 1);
            int16_t x1 = flip_clampi16(pxi + 1, 0, f->pNumX - 1);
            int16_t y1 = flip_clampi16(pyi + 1, 0, f->pNumY - 1);

            for (int16_t xi = x0; xi <= x1; xi++) {
                for (int16_t yi = y0; yi <= y1; yi++) {
                    const int16_t cell = xi * f->pNumY + yi;
                    for (uint16_t jj = f->firstCellParticle[cell]; jj < f->firstCellParticle[cell + 1]; jj++) {
                        const int16_t id = (int16_t)f->cellParticleIds[jj];
                        if (id <= i) continue;
                        q16_t dx = f->particlePos[2 * id] - px;
                        q16_t dy = f->particlePos[2 * id + 1] - py;
                        if (dx <= -minDist || dx >= minDist
                         || dy <= -minDist || dy >= minDist) {
                            continue;
                        }
                        q16_t d2 = flip_mul(dx, dx) + flip_mul(dy, dy);
                        if (d2 >= minDist2 || d2 <= 1) continue;
                        q16_t d = flip_sqrt_q16(d2);
                        if (d <= 0) continue;

                        q16_t corr = (minDist - d) >> 1;
                        q16_t scale = flip_div(corr, d);
                        dx = flip_mul(dx, scale);
                        dy = flip_mul(dy, scale);

                        px -= dx;
                        py -= dy;
                        f->particlePos[2 * i] = px;
                        f->particlePos[2 * i + 1] = py;
                        f->particlePos[2 * id] += dx;
                        f->particlePos[2 * id + 1] += dy;
                    }
                }
            }
        }
    }
}

static void flip_handle_container_collisions(flip_fluid_t *f)
{
    const q16_t r = f->particleRadius;
    const q16_t minX = f->h + r;
    const q16_t maxX = f->maxX - r;
    const q16_t minY = f->h + r;
    const q16_t maxY = f->maxY - r;

    for (int16_t i = 0; i < f->numParticles; i++) {
        q16_t *p = &f->particlePos[2 * i];
        q16_t *v = &f->particleVel[2 * i];
        if (p[0] < minX) { p[0] = minX; v[0] = 0; }
        if (p[0] > maxX) { p[0] = maxX; v[0] = 0; }
        if (p[1] < minY) { p[1] = minY; v[1] = 0; }
        if (p[1] > maxY) { p[1] = maxY; v[1] = 0; }
    }
}

static void flip_update_particle_density(flip_fluid_t *f)
{
    const int16_t n = f->numY;
    const q16_t maxX = f->maxX;
    const q16_t maxY = f->maxY;
    memset(f->particleDensity, 0, (size_t)f->numCells * sizeof(uint16_t));

    for (int16_t i = 0; i < f->numParticles; i++) {
        q16_t x = flip_clamp(f->particlePos[2 * i], f->h, maxX);
        q16_t y = flip_clamp(f->particlePos[2 * i + 1], f->h, maxY);
        q16_t sxp = x - f->hHalf;
        q16_t syp = y - f->hHalf;
        int16_t x0 = flip_clampi16(flip_mul(sxp, f->hInv) >> 16, 0, f->numX - 2);
        int16_t y0 = flip_clampi16(flip_mul(syp, f->hInv) >> 16, 0, f->numY - 2);
        q16_t tx = flip_mul(sxp - flip_mul_n(f->h, x0), f->hInv);
        q16_t ty = flip_mul(syp - flip_mul_n(f->h, y0), f->hInv);
        int16_t x1 = MIN(x0 + 1, f->numX - 2);
        int16_t y1 = MIN(y0 + 1, f->numY - 2);
        q16_t wx0 = FLIP_Q16_ONE - tx;
        q16_t wy0 = FLIP_Q16_ONE - ty;
        flip_u16_sat_add(&f->particleDensity[x0*n+y0], flip_q16_to_weight(flip_mul(wx0, wy0)));
        flip_u16_sat_add(&f->particleDensity[x1*n+y0], flip_q16_to_weight(flip_mul(tx, wy0)));
        flip_u16_sat_add(&f->particleDensity[x1*n+y1], flip_q16_to_weight(flip_mul(tx, ty)));
        flip_u16_sat_add(&f->particleDensity[x0*n+y1], flip_q16_to_weight(flip_mul(wx0, ty)));
    }

    if (0 == f->particleRestDensity) {
        int32_t num = 0;
        int64_t sum = 0;
        for (int16_t i = 0; i < f->numCells; i++) {
            if (FLIP_FLUID_CELL == f->cellType[i]) {
                sum += f->particleDensity[i];
                num++;
            }
        }
        if (num > 0) {
            f->particleRestDensity = (uint16_t)(sum / num);
        }
    }
}

__STATIC_INLINE uint8_t flip_face_valid(const flip_fluid_t *f,
                                        uint8_t component,
                                        int16_t x,
                                        int16_t y)
{
    const int16_t n = f->numY;
    const int16_t cell = x * n + y;
    int16_t adjacent;

    if (0 == component) {
        if (x <= 0) return 0;
        adjacent = (x - 1) * n + y;
    } else {
        if (y <= 0) return 0;
        adjacent = x * n + y - 1;
    }

    return (uint8_t)((f->cellType[cell] != FLIP_AIR_CELL)
                  || (f->cellType[adjacent] != FLIP_AIR_CELL));
}

static void flip_transfer_velocities(flip_fluid_t *f, uint8_t toGrid)
{
    const int16_t n = f->numY;

    if (toGrid) {
        memcpy(f->prevU, f->u, (size_t)f->numCells * sizeof(q16_t));
        memcpy(f->prevV, f->v, (size_t)f->numCells * sizeof(q16_t));
        memset(f->du, 0, (size_t)f->numCells * sizeof(uint16_t));
        memset(f->dv, 0, (size_t)f->numCells * sizeof(uint16_t));
        memset(f->u, 0, (size_t)f->numCells * sizeof(q16_t));
        memset(f->v, 0, (size_t)f->numCells * sizeof(q16_t));

        memset(f->cellType, FLIP_AIR_CELL, (size_t)f->numCells);
        for (int16_t y = 0; y < f->numY; y++) {
            f->cellType[y] = FLIP_SOLID_CELL;
            f->cellType[(f->numX - 1) * n + y] = FLIP_SOLID_CELL;
        }
        for (int16_t x = 0; x < f->numX; x++) {
            f->cellType[x * n] = FLIP_SOLID_CELL;
        }
        for (int16_t i = 0; i < f->numParticles; i++) {
            int16_t xi = flip_clampi16(flip_mul(f->particlePos[2*i], f->hInv) >> 16, 0, f->numX - 1);
            int16_t yi = flip_clampi16(flip_mul(f->particlePos[2*i + 1], f->hInv) >> 16, 0, f->numY - 1);
            int16_t c = xi * n + yi;
            if (FLIP_AIR_CELL == f->cellType[c]) {
                f->cellType[c] = FLIP_FLUID_CELL;
            }
        }
    }

    for (uint8_t component = 0; component < 2; component++) {
        q16_t dx = component ? f->hHalf : 0;
        q16_t dy = component ? 0 : f->hHalf;
        q16_t *field = component ? f->v : f->u;
        q16_t *prev = component ? f->prevV : f->prevU;
        uint16_t *weight = component ? f->dv : f->du;
        const q16_t maxX = f->maxX;
        const q16_t maxY = f->maxY;

        for (int16_t i = 0; i < f->numParticles; i++) {
            q16_t x = flip_clamp(f->particlePos[2*i], f->h, maxX);
            q16_t y = flip_clamp(f->particlePos[2*i + 1], f->h, maxY);
            q16_t sxp = x - dx;
            q16_t syp = y - dy;
            int16_t x0 = flip_clampi16(flip_mul(sxp, f->hInv) >> 16, 0, f->numX - 2);
            int16_t y0 = flip_clampi16(flip_mul(syp, f->hInv) >> 16, 0, f->numY - 2);
            q16_t tx = flip_mul(sxp - flip_mul_n(f->h, x0), f->hInv);
            q16_t ty = flip_mul(syp - flip_mul_n(f->h, y0), f->hInv);
            int16_t x1 = MIN(x0 + 1, f->numX - 2);
            int16_t y1 = MIN(y0 + 1, f->numY - 2);
            q16_t wx0 = FLIP_Q16_ONE - tx;
            q16_t wy0 = FLIP_Q16_ONE - ty;
            q16_t w0 = flip_mul(wx0, wy0);
            q16_t w1 = flip_mul(tx, wy0);
            q16_t w2 = flip_mul(tx, ty);
            q16_t w3 = flip_mul(wx0, ty);
            int16_t nr0 = x0*n+y0, nr1 = x1*n+y0, nr2 = x1*n+y1, nr3 = x0*n+y1;

            if (toGrid) {
                q16_t pv = f->particleVel[2*i + component];
                field[nr0] += flip_mul(pv, w0); flip_u16_sat_add(&weight[nr0], flip_q16_to_weight(w0));
                field[nr1] += flip_mul(pv, w1); flip_u16_sat_add(&weight[nr1], flip_q16_to_weight(w1));
                field[nr2] += flip_mul(pv, w2); flip_u16_sat_add(&weight[nr2], flip_q16_to_weight(w2));
                field[nr3] += flip_mul(pv, w3); flip_u16_sat_add(&weight[nr3], flip_q16_to_weight(w3));
            } else {
                uint8_t valid0 = flip_face_valid(f, component, x0, y0);
                uint8_t valid1 = flip_face_valid(f, component, x1, y0);
                uint8_t valid2 = flip_face_valid(f, component, x1, y1);
                uint8_t valid3 = flip_face_valid(f, component, x0, y1);
                q16_t d = (valid0 ? w0 : 0) + (valid1 ? w1 : 0)
                        + (valid2 ? w2 : 0) + (valid3 ? w3 : 0);
                if (d > 0) {
                    q16_t invD = ((d >= (FLIP_Q16_ONE - FLIP_NORMALIZATION_EPSILON_Q16))
                               && (d <= (FLIP_Q16_ONE + FLIP_NORMALIZATION_EPSILON_Q16)))
                               ? FLIP_Q16_ONE
                               : flip_div(FLIP_Q16_ONE, d);
                    q16_t picSum = (valid0 ? flip_mul(w0, field[nr0]) : 0)
                                     + (valid1 ? flip_mul(w1, field[nr1]) : 0)
                                     + (valid2 ? flip_mul(w2, field[nr2]) : 0)
                                     + (valid3 ? flip_mul(w3, field[nr3]) : 0);
                    q16_t corrSum = (valid0 ? flip_mul(w0, field[nr0] - prev[nr0]) : 0)
                                      + (valid1 ? flip_mul(w1, field[nr1] - prev[nr1]) : 0)
                                      + (valid2 ? flip_mul(w2, field[nr2] - prev[nr2]) : 0)
                                      + (valid3 ? flip_mul(w3, field[nr3] - prev[nr3]) : 0);
                    q16_t pic = flip_mul(picSum, invD);
                    q16_t corr = flip_mul(corrSum, invD);
                    q16_t flipv = f->particleVel[2*i + component] + corr;
                    f->particleVel[2*i + component] = flip_limit_velocity(flip_mul(FLIP_Q16_ONE - flip_scene.flipRatio, pic)
                                                    + flip_mul(flip_scene.flipRatio, flipv));
                }
            }
        }

        if (toGrid) {
            for (int16_t i = 0; i < f->numCells; i++) {
                if (weight[i] > 0) {
                    field[i] = flip_div(field[i], (q16_t)weight[i] * (q16_t)(1u << FLIP_WEIGHT_SHIFT));
                }
            }
            for (int16_t i = 0; i < f->numX; i++) {
                for (int16_t j = 0; j < f->numY; j++) {
                    int16_t c = i*n+j;
                    uint8_t solid = (FLIP_SOLID_CELL == f->cellType[c]);
                    if (solid || (i > 0 && FLIP_SOLID_CELL == f->cellType[(i-1)*n+j])) f->u[c] = f->prevU[c];
                    if (solid || (j > 0 && FLIP_SOLID_CELL == f->cellType[i*n+j-1])) f->v[c] = f->prevV[c];
                }
            }
        }
    }
}

static void flip_solve_incompressibility(flip_fluid_t *f)
{
    const int16_t n = f->numY;
    memcpy(f->prevU, f->u, (size_t)f->numCells * sizeof(q16_t));
    memcpy(f->prevV, f->v, (size_t)f->numCells * sizeof(q16_t));

    for (int16_t iter = 0; iter < flip_scene.numPressureIters; iter++) {
        for (int16_t i = 1; i < f->numX - 1; i++) {
            for (int16_t j = 1; j < f->numY - 1; j++) {
                int16_t c = i*n+j;
                if (FLIP_FLUID_CELL != f->cellType[c]) continue;
                int16_t right=(i+1)*n+j, top=i*n+j+1;
                int16_t sx0 = (i > 1);
                int16_t sx1 = (i < f->numX - 2);
                int16_t sy0 = (j > 1);
                int16_t sy1 = 1;
                int16_t ss = sx0 + sx1 + sy0 + sy1;
                if (0 == ss) continue;
                q16_t div = f->u[right] - f->u[c] + f->v[top] - f->v[c];
                if (f->particleRestDensity > 0 && flip_scene.compensateDrift) {
                    q16_t compression = ((q16_t)f->particleDensity[c] - (q16_t)f->particleRestDensity)
                                      * (q16_t)(1u << FLIP_WEIGHT_SHIFT);
                    if (compression > 0) div -= (compression >> 3);
                }
                q16_t pressure = (INT32_MIN == div)
                               ? INT32_MAX
                               : -flip_div_small_positive(div, (uint8_t)ss);
                pressure = flip_limit_velocity(flip_mul(pressure, flip_scene.overRelaxation));
                f->u[c] -= sx0 * pressure;
                f->u[right] += sx1 * pressure;
                f->v[c] -= sy0 * pressure;
                f->v[top] += sy1 * pressure;
            }
        }
    }
}

static void flip_update_particle_colours(flip_fluid_t *f)
{
    for (int16_t i = 0; i < f->numParticles; i++) {
        int16_t xi = flip_clampi16(flip_mul(f->particlePos[2*i], f->hInv) >> 16, 1, f->numX - 1);
        int16_t yi = flip_clampi16(flip_mul(f->particlePos[2*i + 1], f->hInv) >> 16, 1, f->numY - 1);
        int16_t c = xi * f->numY + yi;
        f->particleColor[i] = flip_density_sample_to_rgb565(
            f->particleDensity[c],
            f->particleRestDensity);
    }
}

static void flip_fluid_simulate(flip_fluid_t *f)
{
    flip_integrate_particles(f, flip_scene.dt, flip_scene.gravityX, flip_scene.gravityY);
    if (flip_scene.separateParticles) {
        flip_push_particles_apart(f, flip_scene.numParticleIters);
    }
    flip_handle_container_collisions(f);
    flip_transfer_velocities(f, 1);
    flip_update_particle_density(f);
    flip_solve_incompressibility(f);
    flip_transfer_velocities(f, 0);
    if (!flip_scene.showGrid) {
        flip_update_particle_colours(f);
    }
}

uint16_t flip_density_to_rgb565(q16_t densityRatio)
{
    densityRatio = flip_clamp(densityRatio, 0, FLIP_Q16_2);
    if (densityRatio < FLIP_Q16_0_7) {
        return flip_rgb565(155, 205, 255);
    }
    if (densityRatio < FLIP_Q16_ONE) {
        return flip_rgb565(80, 150, 255);
    }
    if (densityRatio < (q16_t)98304) { /* 1.5 */
        return flip_rgb565(30, 90, 235);
    }
    return flip_rgb565(20, 45, 180);
}

uint16_t flip_density_sample_to_rgb565(uint16_t density, uint16_t restDensity)
{
    if (0u == restDensity) {
        return flip_density_to_rgb565(FLIP_Q16_ONE);
    }
    if (((uint32_t)density << 16)
      < ((uint32_t)FLIP_Q16_0_7 * restDensity)) {
        return flip_rgb565(155, 205, 255);
    }
    if (density < restDensity) {
        return flip_rgb565(80, 150, 255);
    }
    if (((uint32_t)density * 2u) < ((uint32_t)restDensity * 3u)) {
        return flip_rgb565(30, 90, 235);
    }
    return flip_rgb565(20, 45, 180);
}

void flip_set_gravity(q16_t gx, q16_t gy)
{
    flip_scene.gravityX = gx;
    flip_scene.gravityY = gy;
    flip_scene.gravity = gy;
}

void flip_set_screen_motion_q15(int16_t gravityX,
                                int16_t gravityY,
                                int16_t accelerationX,
                                int16_t accelerationY)
{
    if (accelerationX >= -FLIP_MOTION_ACCEL_DEADZONE_Q15
     && accelerationX <= FLIP_MOTION_ACCEL_DEADZONE_Q15) {
        accelerationX = 0;
    }
    if (accelerationY >= -FLIP_MOTION_ACCEL_DEADZONE_Q15
     && accelerationY <= FLIP_MOTION_ACCEL_DEADZONE_Q15) {
        accelerationY = 0;
    }
    const q16_t gravityXQ16 = (q16_t)((int32_t)gravityX * 2);
    const q16_t gravityYQ16 = (q16_t)((int32_t)gravityY * 2);
    const q16_t accelerationXQ16 = (q16_t)((int32_t)accelerationX * 2);
    const q16_t accelerationYQ16 = (q16_t)((int32_t)accelerationY * 2);
    /* Static accelerometer gravity is support force, opposite real gravity. */
    q16_t forceX = -flip_mul(gravityXQ16, FLIP_MOTION_GRAVITY_SCALE_Q16)
                 - flip_mul(accelerationXQ16, FLIP_MOTION_ACCEL_SCALE_Q16);
    q16_t forceY = -flip_mul(gravityYQ16, FLIP_MOTION_GRAVITY_SCALE_Q16)
                 + flip_mul(accelerationYQ16, FLIP_MOTION_ACCEL_SCALE_Q16);

    forceX = flip_clamp(forceX,
                        -FLIP_MOTION_FORCE_LIMIT_Q16,
                        FLIP_MOTION_FORCE_LIMIT_Q16);
    forceY = flip_clamp(forceY,
                        -FLIP_MOTION_FORCE_LIMIT_Q16,
                        FLIP_MOTION_FORCE_LIMIT_Q16);
    flip_set_gravity(forceX, forceY);
}

uint16_t flip_get_particle_colour(const flip_fluid_t *f, int16_t particleIndex)
{
    if (!f || particleIndex < 0 || particleIndex >= f->numParticles) {
        return 0;
    }
    return f->particleColor[particleIndex];
}

int flip_init_with_grid(int16_t gridW, int16_t gridH, int16_t maxParticles)
{
    if (gridW < 8 || gridH < 8 || maxParticles < 64
     || gridW > (INT16_MAX - 2) || gridH > (INT16_MAX - 2)
     || ((int32_t)(gridW + 2) * (gridH + 2)) > INT16_MAX) {
        return -1;
    }

    s_nGridW = gridW;
    s_nGridH = gridH;
    s_nMaxParticles = maxParticles;

    memset(&flip_scene, 0, sizeof(flip_scene));
    flip_scene.gravity = FLIP_GRAVITY_Q16;
    flip_scene.gravityX = 0;
    flip_scene.gravityY = FLIP_GRAVITY_Q16;
    flip_scene.dt = FLIP_DT_Q16;
    flip_scene.flipRatio = FLIP_DEFAULT_FLIP_RATIO_Q16;
    flip_scene.overRelaxation = FLIP_OVER_RELAXATION_Q16;
    flip_scene.numPressureIters = FLIP_PRESSURE_ITERS;
    flip_scene.numParticleIters = FLIP_PARTICLE_ITERS;
    flip_scene.compensateDrift = 1;
    flip_scene.separateParticles = 1;
    flip_scene.paused = 0;
    flip_scene.showGrid = FLIP_RENDER_DENSITY_FIELD ? 1u : 0u;

    if (0 != flip_fluid_create(&s_tFluid, gridW, gridH, maxParticles)) {
        flip_scene.fluid = NULL;
        return -2;
    }
    flip_scene.fluid = &s_tFluid;
    flip_init_particles(&s_tFluid);
    if (!flip_scene.showGrid) {
        flip_update_particle_colours(&s_tFluid);
    }
    return 0;
}

int flip_init(void)
{
    return flip_init_with_grid(FLIP_GRID_W, FLIP_GRID_H, FLIP_MAX_PARTICLES);
}

void flip_reset(void)
{
    (void)flip_init_with_grid(s_nGridW, s_nGridH, s_nMaxParticles);
}

void flip_sim_update(void)
{
    if (flip_scene.paused || !flip_scene.fluid) {
        return;
    }
    flip_fluid_simulate(flip_scene.fluid);
    flip_scene.frameNr++;
}
