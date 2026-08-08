#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fire_sim.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define FIRE_GRID_W        60
#define FIRE_GRID_H        60
#define FIRE_STORAGE_CELLS ((FIRE_GRID_W + 2) * (FIRE_GRID_H + 2))
#define Q16_0_12           ((q16_t)7864)
#define Q16_0_85           ((q16_t)55706)
#define Q16_1_9            ((q16_t)124518)
#define Q16_2_2            ((q16_t)144179)
#define Q16_THREE          ((q16_t)0x00030000)
#define Q16_TEN            ((q16_t)0x000A0000)
#define Q16_TWENTY         ((q16_t)0x00140000)
#define FIRE_MOTION_ACCEL_GAIN_Q16 Q16_TWO
#define FIRE_MOTION_SWIRL_GAIN_Q16 Q16_ZERO_ZERO_TWO
#define FIRE_MOTION_DEADZONE_Q15   512
#define FIRE_SWIRL_RADIUS_Q16       Q16_ZERO_ZERO_FIVE
#define FIRE_SWIRL_RADIUS2_Q16      ((q16_t)163)
#define FIRE_SWIRL_FALLOFF2_Q16     ((q16_t)104)
#define FIRE_SWIRL_RADIUS_INV_Q16   ((q16_t)1310640)

typedef struct fire_fluid_storage_t {
    q16_t u[FIRE_STORAGE_CELLS];
    q16_t v[FIRE_STORAGE_CELLS];
    q16_t newU[FIRE_STORAGE_CELLS];
    q16_t newV[FIRE_STORAGE_CELLS];
    q16_t t[FIRE_STORAGE_CELLS];
    q16_t newT[FIRE_STORAGE_CELLS];
    uint8_t s[FIRE_STORAGE_CELLS];
} fire_fluid_storage_t;

int g_scale = 5;
int g_canvasWidth = 60;
int g_canvasHeight = 60;
float cScale = 0.0f;

static fire_fluid_storage_t s_tFluidStorage;
static uint32_t rng_state = 0x12345678;

#if defined(__ARM_ARCH_6M__)
__STATIC_FORCEINLINE q16_t fire_mul_q16(q16_t a, q16_t b)
{
    uint32_t aBits = (uint32_t)a;
    uint32_t bBits = (uint32_t)b;
    int32_t aHi = (int32_t)(aBits >> 16);
    int32_t bHi = (int32_t)(bBits >> 16);
    uint32_t aLo = aBits & UINT16_MAX;
    uint32_t bLo = bBits & UINT16_MAX;
    uint32_t result = (aLo * bLo) >> 16;

    if (aHi > INT16_MAX) {
        aHi -= 0x10000;
    }
    if (bHi > INT16_MAX) {
        bHi -= 0x10000;
    }

    result += (uint32_t)((int32_t)aLo * bHi);
    result += (uint32_t)(aHi * (int32_t)bLo);
    result += (uint32_t)(aHi * bHi) << 16;

    if (result <= INT32_MAX) {
        return (q16_t)result;
    }
    return (q16_t)(-1 - (int32_t)(UINT32_MAX - result));
}

/* Avoid the generic 64-bit multiply helper on ARMv6-M targets. */
#define mul_q16(__A, __B) fire_mul_q16((__A), (__B))
#endif

__STATIC_INLINE uint32_t fast_rand_u32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;

    return x;
}

__STATIC_INLINE q16_t fast_rand_q16(void)
{
    return (q16_t)(fast_rand_u32() >> 16);
}

__STATIC_INLINE void swap_q16_ptr(q16_t **a, q16_t **b)
{
    q16_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

Scene scene;  // 全局火焰仿真场景


  
static q16_t fire_sqrt_q16(q16_t x)
{
    if (x <= 0) {
        return 0;
    }

    // 以 1.0 作为最小初值，避免定点除法初期发散。
    q16_t guess = (x > (1 << 16)) ? x : (1 << 16);

    // 牛顿迭代求 Q16 平方根。
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;

    return guess;
}

static inline q16_t swirl_strength_q16(q16_t r2,
                                       q16_t radius2,
                                       q16_t falloff2,
                                       q16_t radiusInv)
{
    if (r2 >= radius2) {
        return Q16_ZERO;
    }
    if (r2 <= falloff2) {
        return Q16_ONE;
    }
    if ((radius2 == FIRE_SWIRL_RADIUS2_Q16)
     && (falloff2 == FIRE_SWIRL_FALLOFF2_Q16)
     && (radiusInv == FIRE_SWIRL_RADIUS_INV_Q16)) {
        return Q16_ZERO;
    }

    q16_t r = fire_sqrt_q16(r2);
    q16_t s = Q16_FIVE - mul_q16(mul_q16(Q16_FIVE, r), radiusInv);
    return max(min(s, Q16_ONE), Q16_ZERO);
}
  
  
static inline int32_t floor_q16(q16_t x)
{
    if (x >= 0) {
        return x >> 16;
    }

    uint32_t magnitude = (~(uint32_t)x) + 1u;
    uint32_t rounded = (magnitude >> 16)
                     + ((magnitude & UINT16_MAX) != 0u);
    return -(int32_t)rounded;
}
// 创建并初始化流体网格。
Fluid * Fluid_create(int numX, int numY, q16_t h) {
	Fluid *f = (Fluid*)malloc(sizeof(Fluid));
	if (NULL == f) {
		return NULL;
	}
	if ((numX < 0)
	 || (numY < 0)
	 || (numX > (INT16_MAX - 2))
	 || (numY > (INT16_MAX - 2))
	 || (h <= Q16_ZERO)) {
		free(f);
		return NULL;
	}

	int32_t numXWithBoundary = numX + 2;
	int32_t numYWithBoundary = numY + 2;
	int32_t numCells = numXWithBoundary * numYWithBoundary;
	if ((numXWithBoundary <= 0)
	 || (numYWithBoundary <= 0)
	 || (numXWithBoundary > INT16_MAX)
	 || (numYWithBoundary > INT16_MAX)
	 || (numCells > FIRE_STORAGE_CELLS)) {
		free(f);
		return NULL;
	}

	f->numX = (int16_t)numXWithBoundary;
	f->numY = (int16_t)numYWithBoundary;
	f->numCells = (int16_t)numCells;
	f->h = h;
	f->hInv = div_q16(Q16_ONE, h);
	f->hHalf = h >> 1;
	f->maxX = mul_n_q16(h, f->numX - 1);
	f->maxY = mul_n_q16(h, f->numY - 1);
	
	// Typed storage avoids aliasing and alignment assumptions from a byte buffer.
	f->u = s_tFluidStorage.u;
	f->v = s_tFluidStorage.v;
	f->newU = s_tFluidStorage.newU;
	f->newV = s_tFluidStorage.newV;
	f->s = s_tFluidStorage.s;
	f->t = s_tFluidStorage.t;
	f->newT = s_tFluidStorage.newT;
	
	// 默认所有单元可流动，温度场清零。
	for (int i = 0; i < f->numCells; i++) {
		f->s[i] = 1;
		f->t[i] = Q16_ZERO;
	}
	
	// 初始化涡旋状态。
	f->numSwirls = 0;
	f->swirlGlobalTime = Q16_ZERO;
	for (int i = 0; i < MAX_SWIRLS; i++) {
	   f->swirlTime[i] = Q16_ZERO;
	}
	
	return f;
}

// 将重力积分到速度场。
void Fluid_integrate(Fluid *f, q16_t dt, q16_t gravity) {
	int16_t n = f->numY;
	for (int i = 1; i < f->numX; i++) {
		for (int j = 1; j < f->numY - 1; j++) {
			// 只有速度面两侧都是流体时才更新该竖直速度。
			if (f->s[i * n + j] != 0 && f->s[i * n + j - 1] != 0) {
				f->v[i * n + j] += mul_q16(gravity , dt);
			}
		}
	}
}

// 迭代投影速度场，使流体近似不可压缩。
void Fluid_solveIncompressibility(Fluid *f, int16_t numIters, q16_t dt) {
	(void)dt;
	int16_t n = f->numY;
	q16_t overRelaxation = Q16_1_9;  // 超松弛系数，加快压力迭代收敛。
	int16_t nInteriorRows = f->numY - 2;
	/* The solid mask is initialized to all-fluid and never changes. */
	
	for (int iter = 0; iter < numIters; iter++) {
		for (int i = 1; i < f->numX - 1; i++) {
			q16_t *u = &f->u[i * n + 1];
			q16_t *uNext = u + n;
			q16_t *v = &f->v[i * n + 1];

			for (int j = 0; j < nInteriorRows; j++) {
				q16_t div = *uNext - *u + v[1] - v[0];
				q16_t p = -(div / 4);

				p = mul_q16(overRelaxation, p);
				*u -= p;
				*uNext += p;
				v[0] -= p;
				v[1] += p;

				u++;
				uNext++;
				v++;
			}
		}
	}
}

// 将边界速度外推到外圈，保证采样边缘稳定。
void Fluid_extrapolate(Fluid *f) {
	int16_t n = f->numY;
	// 上下边界的 u 分量。
	for (int i = 0; i < f->numX; i++) {
		f->u[i * n + 0] = f->u[i * n + 1];
		f->u[i * n + f->numY - 1] = f->u[i * n + f->numY - 2];
	}
	// 左右边界的 v 分量。
	for (int j = 0; j < f->numY; j++) {
		f->v[0 * n + j] = f->v[1 * n + j];
		f->v[(f->numX - 1) * n + j] = f->v[(f->numX - 2) * n + j];
	}
}

// 在 MAC 网格上双线性采样指定场。
q16_t Fluid_sampleField(Fluid *f, q16_t x, q16_t y, int field) {
	int16_t n = f->numY;
	q16_t h = f->h;
	q16_t h1 = f->hInv;
	q16_t h2 = f->hHalf;
	
	// 将采样点限制在仿真域内。
	x = max(min(x, f->maxX + h), h);
	y = max(min(y, f->maxY + h), h);
	
	q16_t dx = Q16_ZERO;
	q16_t dy = Q16_ZERO;
	
	q16_t *fieldData;
	
	// 不同字段位于交错网格的不同位置，需要对应的半格偏移。
	switch (field) {
		case U_FIELD: fieldData = f->u; dy = h2; break;
		case V_FIELD: fieldData = f->v; dx = h2; break;
		case T_FIELD: fieldData = f->t; dx = h2; dy = h2; break;
		default: return Q16_ZERO;
	}
	
	// 转换到网格坐标并计算插值权重。
	q16_t sampleX = x - dx;
	q16_t sampleY = y - dy;
	int16_t x0 = min(floor_q16(mul_q16(sampleX , h1)), f->numX - 1);

	q16_t tx = mul_q16((sampleX - mul_n_q16(h, x0)) , h1);
	
	int16_t x1 = min(x0 + 1, f->numX - 1);
	
	int16_t y0 = min(floor_q16(mul_q16(sampleY , h1)), f->numY - 1);
	q16_t ty = mul_q16((sampleY - mul_n_q16(h , y0)) , h1);
	int16_t y1 = min(y0 + 1, f->numY - 1);
	
	q16_t v00 = fieldData[x0 * n + y0];
	q16_t v10 = fieldData[x1 * n + y0];
	q16_t v01 = fieldData[x0 * n + y1];
	q16_t v11 = fieldData[x1 * n + y1];
	q16_t vx0 = v00 + mul_q16(tx, v10 - v00);
	q16_t vx1 = v01 + mul_q16(tx, v11 - v01);
	
	// 双线性插值得到采样值。
	q16_t val = vx0 + mul_q16(ty, vx1 - vx0);
	
	return val;
}

// 将 u 分量平均到 v 速度面所在位置。
q16_t Fluid_avgU(Fluid *f, int16_t i, int16_t j) {
	int16_t n = f->numY;
	return (f->u[i * n + j - 1] + f->u[i * n + j] +
	        f->u[(i + 1) * n + j - 1] + f->u[(i + 1) * n + j]) >> 2;
}

// 将 v 分量平均到 u 速度面所在位置。
q16_t Fluid_avgV(Fluid *f, int i, int j) {
	int16_t n = f->numY;
	return (f->v[(i - 1) * n + j] + f->v[i * n + j] +
	        f->v[(i - 1) * n + j + 1] + f->v[i * n + j + 1]) >> 2;
}

// 半拉格朗日法平流速度场。
void Fluid_advectVel(Fluid *f, q16_t dt) {
	int16_t n = f->numY;
	int16_t iLast = f->numX - 1;
	int16_t jLast = f->numY - 1;
	q16_t h = f->h;
	q16_t h2 = f->hHalf;
	q16_t iPos = h;
	
	// Only boundary faces survive the all-fluid advection pass unchanged.
	memcpy(f->newU, f->u, n * sizeof(q16_t));
	memcpy(f->newV, f->v, n * sizeof(q16_t));
	memcpy(&f->newV[iLast * n], &f->v[iLast * n], n * sizeof(q16_t));
	for (int i = 1; i <= iLast; i++) {
		int16_t idx = i * n;
		f->newU[idx] = f->u[idx];
		f->newU[idx + jLast] = f->u[idx + jLast];
		if (i < iLast) {
			f->newV[idx] = f->v[idx];
		}
	}
	
	for (int i = 1; i < f->numX; i++) {
		q16_t jPos = h;
		for (int j = 1; j < f->numY; j++) {
			// 回溯并采样 u 分量。
			if (j < jLast) {
				q16_t x = iPos;
				q16_t y = jPos + h2;
				q16_t u = f->u[i * n + j];
				q16_t v = Fluid_avgV(f, i, j);
				x = x - mul_q16(dt, u);  // 沿速度方向反向追踪上一时刻位置。
				y = y - mul_q16(dt, v);
				u = Fluid_sampleField(f, x, y, U_FIELD);
				f->newU[i * n + j] = u;
			}
			
			// 回溯并采样 v 分量。
			if (i < iLast) {
				q16_t x = iPos + h2;
				q16_t y = jPos;
				q16_t u = Fluid_avgU(f, i, j);
				q16_t v = f->v[i * n + j];
				x = x - mul_q16(dt, u);
				y = y - mul_q16(dt, v);
				v = Fluid_sampleField(f, x, y, V_FIELD);
				f->newV[i * n + j] = v;
			}
			jPos += h;
		}
		iPos += h;
	}
	
	// 新旧速度缓冲区交换。
	swap_q16_ptr(&f->u, &f->newU);
	swap_q16_ptr(&f->v, &f->newV);
}

// 半拉格朗日法平流温度/燃烧强度场。
void Fluid_advectTemperature(Fluid *f, q16_t dt) {
	int16_t n = f->numY;
	int16_t iLast = f->numX - 1;
	int16_t jLast = f->numY - 1;
	q16_t h = f->h;
	q16_t h2 = f->hHalf;
	q16_t iPos = h;
	
	memcpy(f->newT, f->t, n * sizeof(q16_t));
	memcpy(&f->newT[iLast * n], &f->t[iLast * n], n * sizeof(q16_t));
	for (int i = 1; i < iLast; i++) {
		int16_t idx = i * n;
		f->newT[idx] = f->t[idx];
		f->newT[idx + jLast] = f->t[idx + jLast];
	}
	
	for (int i = 1; i < iLast; i++) {
		q16_t jPos = h;
		for (int j = 1; j < jLast; j++) {
			q16_t u = (f->u[i * n + j] + f->u[(i + 1) * n + j]) >> 1;
			q16_t v = (f->v[i * n + j] + f->v[i * n + j + 1]) >> 1;
			q16_t x = iPos + h2 - mul_q16(dt , u);
			q16_t y = jPos + h2 - mul_q16(dt , v);
			f->newT[i * n + j] = Fluid_sampleField(f, x, y, T_FIELD);
			jPos += h;
		}
		iPos += h;
	}
	swap_q16_ptr(&f->t, &f->newT);
}

// 更新燃烧、冷却、浮力和随机涡旋。

void Fluid_updateFire(Fluid *f, q16_t dt) {
	q16_t h = f->h;
	q16_t hInv = f->hInv;
	q16_t hHalf = f->hHalf;
	q16_t swirlTimeSpan = Q16_ONE;
	q16_t swirlOmega = Q16_TWENTY;
	q16_t swirlDamping = mul_q16(dt, Q16_TEN);
	q16_t swirlVelocityScale = Q16_ONE - swirlDamping;
	q16_t swirlProbability = mul_q16(mul_q16(scene.swirlProbability, h), h);
	q16_t swirlThreshold = swirlProbability >> 1;
	q16_t fireCooling = mul_q16(dt, Q16_2_2);
	q16_t smokeCooling = mul_q16(dt, Q16_ZERO_EIGHT);
	q16_t Xlift = mul_q16(scene.liftX, Q16_THREE)
	              + mul_q16(scene.motionAccelerationX,
	                        FIRE_MOTION_ACCEL_GAIN_Q16);
	q16_t Ylift = mul_q16(scene.liftY, Q16_THREE)
	              + mul_q16(scene.motionAccelerationY,
	                        FIRE_MOTION_ACCEL_GAIN_Q16);
	bool applyHorizontalLift = (Xlift != Q16_ZERO);
	q16_t acceleration = mul_q16(dt, Q16_FIVE);
	q16_t kernelRadius = scene.swirlMaxRadius;
	q16_t kernelRadius2;
	q16_t kernelFalloff2;
	q16_t kernelRadiusInv;
	if (kernelRadius == FIRE_SWIRL_RADIUS_Q16) {
		kernelRadius2 = FIRE_SWIRL_RADIUS2_Q16;
		kernelFalloff2 = FIRE_SWIRL_FALLOFF2_Q16;
		kernelRadiusInv = FIRE_SWIRL_RADIUS_INV_Q16;
	} else {
		q16_t kernelFalloff = mul_q16(Q16_ZERO_EIGHT, kernelRadius);
		kernelRadius2 = mul_q16(kernelRadius, kernelRadius);
		kernelFalloff2 = mul_q16(kernelFalloff, kernelFalloff);
		kernelRadiusInv = div_q16(Q16_ONE, kernelRadius);
	}
	int16_t n = f->numY;
	q16_t maxX = f->maxX;
	q16_t maxY = f->maxY;
	swirlThreshold = min(
		swirlThreshold + mul_q16(scene.motionDisturbance,
		                         FIRE_MOTION_SWIRL_GAIN_Q16),
		Q16_ZERO_ONE);

	int16_t num = 0;
	for (int nr = 0; nr < f->numSwirls; nr++) {
		f->swirlTime[nr] -= dt;
		if (f->swirlTime[nr] > Q16_ZERO) {
			f->swirlTime[num] = f->swirlTime[nr];
			f->swirlX[num] = f->swirlX[nr];
			f->swirlY[num] = f->swirlY[nr];
			f->swirlOmega[num] = f->swirlOmega[nr];
			num++;
		}
	}
	f->numSwirls = num;

	for (int nr = 0; nr < f->numSwirls; nr++) {
		q16_t x = f->swirlX[nr];
		q16_t y = f->swirlY[nr];
		q16_t swirlU = mul_q16(swirlVelocityScale, Fluid_sampleField(f, x, y, U_FIELD));
		q16_t swirlV = mul_q16(swirlVelocityScale, Fluid_sampleField(f, x, y, V_FIELD));
		x += mul_q16(swirlU, dt);
		y += mul_q16(swirlV, dt);
		x = min(max(x, h), maxX);
		y = min(max(y, h), maxY);

		f->swirlX[nr] = x;
		f->swirlY[nr] = y;
		q16_t omega = f->swirlOmega[nr];

		int16_t x0 = max(floor_q16(mul_q16(x - kernelRadius, hInv)), 0);
		int16_t y0 = max(floor_q16(mul_q16(y - kernelRadius, hInv)), 0);
		int16_t x1 = min(floor_q16(mul_q16(x + kernelRadius, hInv)) + 1, f->numX - 1);
		int16_t y1 = min(floor_q16(mul_q16(y + kernelRadius, hInv)) + 1, f->numY - 1);

		for (int16_t i = x0; i <= x1; i++) {
			q16_t cellX = mul_n_q16(h, i);
			q16_t cellXHalf = cellX + hHalf;

			for (int16_t j = y0; j <= y1; j++) {
				q16_t cellY = mul_n_q16(h, j);
				q16_t cellYHalf = cellY + hHalf;
				int16_t idx = n * i + j;
				q16_t rx = cellX - x;
				q16_t ry = cellYHalf - y;
				q16_t s = swirl_strength_q16(mul_q16(rx, rx) + mul_q16(ry, ry),
				                              kernelRadius2,
				                              kernelFalloff2,
				                              kernelRadiusInv);
				if (s != Q16_ZERO) {
					q16_t target = mul_q16(ry, omega) + swirlU;
					q16_t u = f->u[idx];
					f->u[idx] += mul_q16(target - u, s);
				}

				rx = cellXHalf - x;
				ry = cellY - y;
				s = swirl_strength_q16(mul_q16(rx, rx) + mul_q16(ry, ry),
				                        kernelRadius2,
				                        kernelFalloff2,
				                        kernelRadiusInv);
				if (s != Q16_ZERO) {
					q16_t target = swirlV - mul_q16(rx, omega);
					q16_t v = f->v[idx];
					f->v[idx] += mul_q16(target - v, s);
				}
			}
		}
	}

	q16_t maxR = scene.obstacleRadius + h;
	q16_t maxR2 = mul_q16(maxR, maxR);

	q16_t gridX = Q16_ZERO;
	for (int16_t i = 0; i < f->numX; i++) {
		q16_t cellX = gridX + hHalf;
		q16_t obstacleDx2 = Q16_ZERO;
		if (scene.burningObstacle) {
			q16_t dxWide = (cellX - scene.obstacleX) >> 1;
			obstacleDx2 = mul_q16(dxWide, dxWide);
		}
		q16_t gridY = Q16_ZERO;
		for (int16_t j = 0; j < f->numY; j++) {
			int16_t idx = i * n + j;
			q16_t t = f->t[idx];
			q16_t cooling = t < Q16_ZERO_THREE ? smokeCooling : fireCooling;
			f->t[idx] = max(t - cooling, Q16_ZERO);
			q16_t v = f->v[idx];
			q16_t targetV = mul_q16(t, Ylift);
			f->v[idx] += mul_q16(targetV - v, acceleration);
			if (applyHorizontalLift) {
				q16_t u = f->u[idx];
				q16_t targetU = mul_q16(t, Xlift);
				f->u[idx] += mul_q16(targetU - u, acceleration);
			}

			int16_t numNewSwirls = 0;
			if (scene.burningObstacle) {
				q16_t cellY = gridY + hHalf;
				q16_t dy = cellY - scene.obstacleY;
				q16_t d = obstacleDx2 + mul_q16(dy, dy);
				if (d < maxR2) {
					f->t[idx] = scene.close ? Q16_ZERO : Q16_ONE;
					if (fast_rand_q16() < swirlThreshold) {
						numNewSwirls++;
					}
				}
			}

			if (j < 4 && scene.burningFloor) {
				f->t[idx] = Q16_ONE;
				f->u[idx] = Q16_ZERO;
				f->v[idx] = Q16_ZERO;
				if (fast_rand_q16() < swirlThreshold) {
					numNewSwirls++;
				}
			}

			for (int16_t k = 0; k < numNewSwirls; k++) {
				if (f->numSwirls >= MAX_SWIRLS) break;
				int16_t nr = f->numSwirls;
				f->swirlX[nr] = gridX;
				f->swirlY[nr] = gridY;
				q16_t random = fast_rand_q16();
				f->swirlOmega[nr] = mul_q16((random << 1) - Q16_ONE, swirlOmega);
				f->swirlTime[nr] = swirlTimeSpan;
				f->numSwirls++;
			}
			gridY += h;
		}
		gridX += h;
	}

	for (int16_t i = 1; i < f->numX - 1; i++) {
		for (int16_t j = 1; j < f->numY - 1; j++) {
			q16_t t = f->t[i * n + j];
			if (t == Q16_ONE) {
				q16_t avg = (f->t[(i - 1) * n + (j - 1)] +
				             f->t[(i + 1) * n + (j - 1)] +
				             f->t[(i + 1) * n + (j + 1)] +
				             f->t[(i - 1) * n + (j + 1)]) >> 2;
				f->t[i * n + j] = avg;
			}
		}
	}
}

// 执行一帧完整的流体和火焰仿真。
void Fluid_simulate(Fluid *f, q16_t dt, q16_t gravity, int16_t  numIters) {
	(void)gravity;
//	Fluid_integrate(f, dt, gravity);
	Fluid_solveIncompressibility(f, numIters, dt);
	Fluid_extrapolate(f);
	Fluid_advectVel(f, dt);
	Fluid_advectTemperature(f, dt);
	Fluid_updateFire(f, dt);
}
void fire_sim_update(void) {  // 上层循环调用的火焰仿真更新入口。
    if (scene.paused || !scene.fluid) return;
    q16_t gravity = scene.gravity;
    
    Fluid_simulate(scene.fluid, scene.dt, gravity, scene.numIters);
    scene.frameNr++;
}

void fire_sim_set_screen_motion_q15(int16_t gravity_x_q15,
                                    int16_t gravity_y_q15,
                                    int16_t acceleration_x_q15,
                                    int16_t acceleration_y_q15,
                                    uint16_t disturbance_q15)
{
    if ((gravity_x_q15 > -FIRE_MOTION_DEADZONE_Q15) &&
        (gravity_x_q15 < FIRE_MOTION_DEADZONE_Q15)) {
        gravity_x_q15 = 0;
    }
    if ((acceleration_x_q15 > -FIRE_MOTION_DEADZONE_Q15) &&
        (acceleration_x_q15 < FIRE_MOTION_DEADZONE_Q15)) {
        acceleration_x_q15 = 0;
    }

    q16_t gravityX = (q16_t)((int32_t)gravity_x_q15 * 2);
    q16_t gravityY = (q16_t)((int32_t)gravity_y_q15 * 2);
    q16_t accelerationX = (q16_t)((int32_t)acceleration_x_q15 * 2);
    q16_t accelerationY = (q16_t)((int32_t)acceleration_y_q15 * 2);

    /* Screen +Y is down, while the fluid cache renders fluid +Y upward. */
    scene.liftX = gravityX;
    scene.liftY = gravityY;
    scene.motionAccelerationX = -accelerationX;
    scene.motionAccelerationY = accelerationY;
    scene.motionDisturbance = (q16_t)((uint32_t)disturbance_q15 * 2u);
}

static inline uint32_t clamp_q16(uint32_t x)
{
    if (x > Q16_ONE) return Q16_ONE;
    return x;
}
   static uint16_t fire_lut[256];
   void fire_lut_init(void) {
       for (int i = 0; i < 256; i++) {
           q16_t val = i << 8; // 将 0~255 索引映射到 Q16 低 16 位范围。
           // 预计算温度到 RGB565 的颜色映射，运行时直接查表。
           q16_t fr, fg, fb;
           const q16_t T1 = Q16_ZERO_THREE;
           const q16_t T2 = Q16_ZERO_SIX;
           if (val < T1) {
               q16_t s = div_q16(val , T1);
               s = mul_q16(s, s);
               fr =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_QUATER, s);
               fg =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_QUATER, s);
               fb =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_THREE, s);
           } else if (val < T2) {
               q16_t s = div_q16((val - T1) , (T2 - T1));
               fr = Q16_ZERO_THREE + mul_q16(Q16_ZERO_SEVEN, s);
               fg = Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_FOUR, s);
               fb = Q16_ZERO_ZERO_TWO;
           } else {
               q16_t s = div_q16((val - T2),(Q16_ONE - T2));
               s = mul_q16(s, s);
               fr = Q16_ONE - 1;
               fg = Q16_HALF + mul_q16(Q16_HALF, s);
               fb = Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_TWO, s);
           }
           uint16_t R = (fr >> 11) & 0x1F;
           uint16_t G = (fg >> 10) & 0x3F;
           uint16_t B = (fb >> 11) & 0x1F;
           fire_lut[i] = (R << 11) | (G << 5) | B;
       }
   }

   uint16_t getFireColor_RGB565_check_list(q16_t val) {
       uint8_t idx;
       if (val <= Q16_ZERO) {
           idx = 0;
       } else if (val >= Q16_ONE) {
           idx = 255;
       } else {
           idx = (uint8_t)((uint32_t)val >> 8);
       }
       return fire_lut[idx];
   }

uint16_t getFireColor_RGB565_Q16(q16_t val)
{
//  val_q16 = clamp_q16(val_q16);

    q16_t fr, fg, fb;

    // 温度分段阈值，使用 Q16 定点表示。
    const q16_t T1 = Q16_ZERO_THREE;//(uint32_t)(0.3f * 65535); 
    const q16_t T2 = Q16_ZERO_SIX;//(uint32_t)(0.6f * 65535); 

    // ===== 低温：暗烟灰到暗红 =====
    if (val < T1)
    {

        q16_t s = div_q16(val , T1);
        s = mul_q16(s, s);

        // fr = 0.05 + 0.25*s
        fr =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_QUATER, s);
        fg =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_QUATER, s);
        fb =  Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_THREE, s);
    }
    // ===== 中温：红色火焰 =====
    else if (val < T2)
    {
        q16_t s = div_q16((val - T1) , (T2 - T1));

        fr = Q16_ZERO_THREE     + mul_q16(Q16_ZERO_SEVEN, s);
        fg = Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_FOUR, s);
        fb = Q16_ZERO_ZERO_TWO;
    }
    // ===== 高温：黄白火芯 =====
    else
    {
        uint32_t s = div_q16((val - T2),(Q16_ONE - T2));
        s = mul_q16(s, s);

        fr = Q16_ONE;
        fg = Q16_HALF + mul_q16(Q16_HALF, s);
        fb = Q16_ZERO_ZERO_FIVE + mul_q16(Q16_ZERO_TWO, s);
    }

    // ===== 转换为 RGB565 =====
    uint16_t R = fr >> 11;  // 16 -> 5bit
    uint16_t G = fg >> 10;  // 16 -> 6bit
    uint16_t B = fb >> 11;

    return (R << 11) | (G << 5) | B;
}

uint16_t getFireColor_RGB565_Q12(float val)
{
	val = val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val);
	float fr, fg, fb;
	
	if (val < 0.3f) {
		float s = val / 0.3f;
		fr = 0.2f * s;
		fg = 0.2f * s;
		fb = 0.2f * s;
	} else if (val < 0.5f) {
		float s = (val - 0.3f) / 0.2f;
		fr = 0.2f + 0.8f * s;
		fg = 0.1f;
		fb = 0.1f;
	} else {
		float s = (val - 0.5f) / 0.48f;
		fr = 1.0f;
		fg = s;
		fb = 0.5f;
	}

    uint16_t R = ((uint32_t)fr * 31);
    uint16_t G = ((uint32_t)fg * 63);
    uint16_t B = ((uint32_t)fb * 31);

    return (R<<11)|(G<<5)|B;
}

uint16_t getFireColor_BlueGas_RGB565(float val)
{
    // 低亮度直接视为熄灭。
    if (val <= 0.05f) return 0x0000;
    if (val > 1.0f) val = 1.0f;

    // 平方压低暗部，保留蓝焰中心的亮度层次。
    val = val * val;

    float fr, fg, fb;

    if (val < 0.3f) {
        // 低温：微弱暗红。
        float s = val / 0.3f;
        fr = 0.4f * s;
        fg = 0.05f * s;
        fb = 0.1f * s;
    } 
    else if (val < 0.6f) {
        // 中低温：逐渐转为蓝色。
        float s = (val - 0.3f) / 0.3f;
        fr = 0.0f;
        fg = 0.2f * s;       // 略微补一点绿色，避免蓝色过死。
        fb = 0.6f + 0.4f * s; // 蓝色逐渐增强。
    } 
    else if (val < 0.85f) {
        // 高温：从蓝转青白。
        float s = (val - 0.6f) / 0.25f;
        fr = 0.3f * s;
        fg = 0.4f + 0.6f * s;
        fb = 1.0f;
    } 
    else {
        // 极高温：接近白色。
        float s = (val - 0.85f) / 0.15f;
        fr = 0.8f + 0.2f * s;
        fg = 0.9f + 0.1f * s;
        fb = 1.0f;
    }

    uint16_t R = (uint16_t)(fr * 31.0f);
    uint16_t G = (uint16_t)(fg * 63.0f);
    uint16_t B = (uint16_t)(fb * 31.0f);

    return (R << 11) | (G << 5) | B;
}



uint16_t getFireColor_RGB565_Q12_gpt_version(float val)
{
    // clamp
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;

    float fr, fg, fb;

    // ===== 低温：暗烟灰到暗红 =====
    if (val < 0.3f) {
        float s = val / 0.3f;

        // 平方曲线让暗部过渡更柔和。
        s = s * s;

        fr = 0.05f + 0.25f * s;
        fg = 0.05f + 0.25f * s;
        fb = 0.05f + 0.30f * s;   // 暗部保留一点蓝灰色。
    }

    // ===== 中温：红橙火焰 =====
    else if (val < 0.6f) {
        float s = (val - 0.3f) / 0.3f;

        fr = 0.3f + 0.7f * s;     // 红色快速增强。
        fg = 0.05f + 0.4f * s;    // 绿色缓慢增强形成橙色。
        fb = 0.02f;               // 蓝色保持很低，避免发紫。
    }

    // ===== 高温：黄白火芯 =====
    else {
        float s = (val - 0.6f) / 0.4f;

        // 高温段使用平方曲线，让白芯只在最高亮度出现。
        s = s * s;

        fr = 1.0f;
        fg = 0.5f + 0.5f * s;     // 绿色增强后形成黄白。
        fb = 0.05f + 0.2f * s;    // 蓝色只少量增加。
    }

    // ===== 转换为 RGB565 =====
    uint16_t R = (uint16_t)(fr * 31.0f);
    uint16_t G = (uint16_t)(fg * 63.0f);
    uint16_t B = (uint16_t)(fb * 31.0f);

    return (R << 11) | (G << 5) | B;
}


uint16_t getFireColor_Fireplace_Smoke(float val)
{
    // ===== 添加轻微随机扰动，减弱色带 =====
    val += ((rand() & 0xFF) / 255.0f - 0.5f) * 0.02f;

    // clamp
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;

    // ===== 非线性压暗，让烟雾和火焰过渡更自然 =====
    val = val * val;

    float fr, fg, fb;
    // ===== 旧版暗部映射，保留作为调色参考 =====
    //if (val < 0.2f) {
    //    val = val * val * 0.9f;
    //    float s = val / 0.2f;

    //    // 暗烟灰到冷色烟雾。
    //    fr = 0.15f + 0.35f * s;
    //    fg = 0.15f + 0.35f * s;
    //    fb = 0.18f + 0.40f * s;
    //}
    if (val < 0.25f) {
        float s = val / 0.25f;

        // 使用 smoothstep 让低亮度烟雾柔和起亮。
        s = s * s * (3.0f - 2.0f * s);   // smoothstep

        // 低温烟雾保留中性灰底色。
        float base = 0.12f;

        fr = base + 0.35f * s;
        fg = base + 0.35f * s;
        fb = base + 0.38f * s;   // 蓝色略高一点，让烟雾偏冷。
    }
    // ===== 低温火焰：红色 =====
    else if (val < 0.4f) {
        float s = (val - 0.2f) / 0.2f;

        fr = 0.6f + 0.4f * s;
        fg = 0.05f + 0.10f * s;
        fb = 0.01f;
    }

    // ===== 中温火焰：橙黄 =====
    else if (val < 0.7f) {
        float s = (val - 0.4f) / 0.3f;

        fr = 1.0f;
        fg = 0.2f + 0.6f * s;
        fb = 0.02f;
    }

    // ===== 高温火芯：黄白 =====
    else {
        float s = (val - 0.7f) / 0.3f;
        s = s * s;

        fr = 1.0f;
        fg = 0.75f + 0.25f * s;
        fb = 0.03f + 0.10f * s;
    }

    uint16_t R = (uint16_t)(fr * 31.0f);
    uint16_t G = (uint16_t)(fg * 63.0f);
    uint16_t B = (uint16_t)(fb * 31.0f);

    return (R << 11) | (G << 5) | B;
}

#ifdef _WIN32
HWND g_hwnd = NULL;
int g_scale = 5;
int g_canvasWidth = 240;
int g_canvasHeight = 135;
float cScale = 0.0f;

// 仿真坐标转换为窗口像素坐标。
float cX(float x) {
	return x * cScale;
}

float cY(float y) {
	return g_canvasHeight - y * cScale;
}

// Windows 预览用的浮点火焰调色。
void getFireColor(float val, unsigned char *r, unsigned char *g, unsigned char *b) {
	val = val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val);
	float fr, fg, fb;
	
	if (val < 0.3f) {
		float s = val / 0.3f;
		fr = 0.2f * s;
		fg = 0.2f * s;
		fb = 0.2f * s;
	} else if (val < 0.5f) {
		float s = (val - 0.3f) / 0.2f;
		fr = 0.2f + 0.8f * s;
		fg = 0.1f;
		fb = 0.1f;
	} else {
		float s = (val - 0.5f) / 0.48f;
		fr = 1.0f;
		fg = s;
		fb = 0.5f;
	}
	
	*r = (unsigned char)(fr * 255);
	*g = (unsigned char)(fg * 255);
	*b = (unsigned char)(fb * 255);
}

// Windows 消息处理函数。
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
		
	case WM_ERASEBKGND:
		return 1;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			
			// 使用内存 DC 双缓冲，减少窗口闪烁。
			HDC hdcMem = CreateCompatibleDC(hdc);
			HBITMAP hbmMem = CreateCompatibleBitmap(hdc, g_canvasWidth, g_canvasHeight);
			HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
			
			RECT rect = {0, 0, g_canvasWidth, g_canvasHeight};
			HBRUSH hbrBlack = CreateSolidBrush(RGB(0, 0, 0));
			FillRect(hdcMem, &rect, hbrBlack);
			DeleteObject(hbrBlack);
			
			// 绘制温度场。
			if (scene.fluid) {
				Fluid *f = scene.fluid;
				int n = f->numY;
				float h = f->h;
				float cellScale = 1.1f;
				
				for (int i = 0; i < f->numX; i++) {
					for (int j = 0; j < f->numY; j++) {
						float t = f->t[i * n + j];
						if (t > 0.01f) {
							unsigned char r, g, b;
							getFireColor(t, &r, &g, &b);
							HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
							
							int x = (int)cX(i * h);
							int y = (int)cY((j + 1) * h);
							int cx = (int)(cScale * cellScale * h) + 1;
							int cy = (int)(cScale * cellScale * h) + 1;
							
							RECT cellRect = {x, y, x + cx, y + cy};
							FillRect(hdcMem, &cellRect, brush);
							DeleteObject(brush);
						}
					}
				}
			}
			
			// 将离屏缓冲拷贝到窗口。
			BitBlt(hdc, 0, 0, g_canvasWidth, g_canvasHeight, hdcMem, 0, 0, SRCCOPY);
			
			SelectObject(hdcMem, hbmOld);
			DeleteObject(hbmMem);
			DeleteDC(hdcMem);
			
			EndPaint(hwnd, &ps);
			return 0;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// 创建 Windows 预览窗口。
void create_window() {
	HINSTANCE hInstance = GetModuleHandle(NULL);
	
	WNDCLASS wc = {0};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = "FireSimWindow";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	
	RegisterClass(&wc);
	
	RECT rect = {0, 0, g_canvasWidth, g_canvasHeight};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	
	g_hwnd = CreateWindowEx(
		0, "FireSimWindow", "Fire Simulation",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, hInstance, NULL
		);
	
	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);
}

// 初始化 Windows 预览场景。
void setupScene() {
	float simHeight = 1.0f;
	cScale = g_canvasHeight / simHeight;
	float simWidth = g_canvasWidth / cScale;
	
	int numCells = 3600;
	float h = sqrtf(simWidth * simHeight / numCells);
	
	int numX = (int)(simWidth / h);
	int numY = (int)(simHeight / h);
	
	scene.obstacleX = 0.5f * numX * h;
	scene.obstacleY = 0.12f * numY * h;
	scene.obstacleRadius = 0.08f;
	scene.swirlProbability = 60.0f;
	scene.showObstacle = scene.burningObstacle;
	
	scene.fluid = Fluid_create(numX, numY, h);
}

// Windows 预览主循环。
void run_window_loop() {
	MSG msg;
	
	while (1) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				return;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		
		if (!scene.paused && scene.fluid) {
			Fluid_simulate(scene.fluid, scene.dt, scene.gravity, scene.numIters);
			scene.frameNr++;
		}
		
		InvalidateRect(g_hwnd, NULL, FALSE);
		UpdateWindow(g_hwnd);
		
		Sleep(16);
	}
}
#endif

int fire_init() {   // 初始化火焰仿真。
	// 设置场景默认参数。
	scene.gravity = Q16_ONE;
	scene.dt = reinterpret_q16_f32(1.0f / 20.0f);
	scene.numIters = 10;
	scene.frameNr = 0;
	scene.obstacleX = Q16_ZERO;
	scene.obstacleY = Q16_ZERO;
	scene.obstacleRadius = Q16_ZERO_ONE;
	scene.burningObstacle = 1;
	scene.burningFloor = 0;
	scene.paused = 0;
	scene.showObstacle = 0;
	scene.showSwirls = 0;
	scene.swirlProbability = reinterpret_q16_f32(50.0f);
	scene.swirlMaxRadius = FIRE_SWIRL_RADIUS_Q16;
	scene.liftX = Q16_ZERO;
	scene.liftY = Q16_ONE;
	scene.motionAccelerationX = Q16_ZERO;
	scene.motionAccelerationY = Q16_ZERO;
	scene.motionDisturbance = Q16_ZERO;
	scene.fluid = NULL;
	fire_lut_init();
#ifdef _WIN32
//	create_window();
//	setupScene();
//	run_window_loop();
#endif
	float simHeight = 1.0f;
	cScale = g_canvasHeight / simHeight;
	float simWidth = g_canvasWidth / cScale;
	
	int numCells = 3600;
	float h = sqrtf(simWidth * simHeight / numCells);
	
	int numX = (int)(simWidth / h);
	int numY = (int)(simHeight / h);
	
	scene.obstacleX = reinterpret_q16_f32(0.5f * numX * h);
	scene.obstacleY = reinterpret_q16_f32(0.12f * numY * h);
	scene.obstacleRadius = Q16_ZERO_ONE;
	scene.swirlProbability = reinterpret_q16_f32(60.0f);
	scene.showObstacle = scene.burningObstacle;
	scene.fluid = Fluid_create(numX, numY, reinterpret_q16_f32(h));

	
	return 0;
}

