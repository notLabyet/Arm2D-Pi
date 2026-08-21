# ARM-2D Generic Loader 参考

## 适用场景

使用 generic loader 的情况：

- 动态渲染，软件生成式内容，例如粒子模拟。
- 需要按 ROI 逐块生成或解码资源内容。
- 资源太大，不适合整帧解码到 SRAM。
- 希望配合 PFB / ROI / dirty region 做局部刷新。

## 基本结构

建议新建一对文件：

```text
project/mdk/<feature>/user_generic_loader_<feature>.c
project/mdk/<feature>/user_generic_loader_<feature>.h
```

头文件通常包含：

```c
typedef struct xxx_cfg_t {
    arm_2d_size_t tSize;
    uint16_t bUseHeapForVRES : 1;

    struct {
        const arm_loader_io_t *ptIO;
        uintptr_t pTarget;
    } ImageIO;

    arm_2d_scene_t *ptScene;
} xxx_cfg_t;

typedef struct xxx_t xxx_t;

struct xxx_t {
    union {
        arm_2d_tile_t tTile;
        inherit(arm_generic_loader_t);
    };

ARM_PRIVATE(
    xxx_cfg_t tCFG;
)
};
```

C 文件通常开头：

```c
#define __GENERIC_LOADER_INHERIT__
#define __xxx_IMPLEMENT__

#include "user_generic_loader_xxx.h"
```

## init 模式

```c
arm_generic_loader_cfg_t tCFG = {
    .bUseHeapForVRES = this.tCFG.bUseHeapForVRES,
    .tColourInfo.chScheme = ARM_2D_COLOUR,
    .bBlendWithBG = false,
    .ImageIO = {
        .ptIO = this.tCFG.ImageIO.ptIO,
        .pTarget = this.tCFG.ImageIO.pTarget,
    },
    .UserDecoder = {
        .fnDecoderInit = &__xxx_decoder_init,
        .fnDecode = &__xxx_draw,
    },
    .ptScene = this.tCFG.ptScene,
};

arm_generic_loader_init(&this.use_as__arm_generic_loader_t, &tCFG);
```

初始化后要设置 tile 尺寸：

```c
this.tTile.tRegion.tSize = this.tCFG.tSize;
```

## show 模式

```c
void xxx_show(xxx_t *ptThis,
              const arm_2d_tile_t *ptTile,
              const arm_2d_region_t *ptRegion,
              bool bIsNewFrame)
{
    ARM_2D_UNUSED(bIsNewFrame);

    if (-1 == (intptr_t)ptTile) {
        ptTile = arm_2d_get_default_frame_buffer();
    }

    arm_2d_tile_copy_only(&this.tTile, ptTile, ptRegion);
}
```

## fnDecode 约定

函数签名：

```c
static arm_2d_err_t __xxx_draw(
    arm_generic_loader_t *ptObj,
    arm_2d_region_t *ptROI,
    uint8_t *pchBuffer,
    uint32_t iTargetStrideInByte,
    uint_fast8_t chBitsPerPixel)
```

规则：

- 必须只写 `ptROI` 对应区域。
- 不能假设目标 buffer 是全屏连续 framebuffer。
- 每行写完后用 `iTargetStrideInByte` 跳到下一行。
- RGB565 时通常这样写：

```c
for (int_fast16_t y = yStart; y < yLimit; y++) {
    uint16_t *phwPixel = (uint16_t *)pchBuffer;

    for (int_fast16_t x = xStart; x < xLimit; x++) {
        *phwPixel++ = colour;
    }

    pchBuffer += iTargetStrideInByte;
}
```

## scene 接入

在 `arm_2d_scene_0.h` 中 include 对应 loader 头文件。

在 `arm_2d_scene_0.c` 中：

```c
xxx_t Xxx;
```

load：

```c
xxx_on_load(&Xxx);
```

frame start：

```c
xxx_on_frame_start(&Xxx);
```

depose：

```c
xxx_depose(&Xxx);
```

PFB draw handler：

```c
arm_2d_align_centre(__top_canvas, 240, 240) {
    xxx_show(&Xxx, ptTile, &__centre_region, bIsNewFrame);
}
```

init：

```c
xxx_cfg_t tCFG = {
    .tSize = {
        .iWidth = 240,
        .iHeight = 240,
    },
    .ptScene = &this.use_as__arm_2d_scene_t,
};
xxx_init(&Xxx, &tCFG);
```

## 注意事项

- 如果 decoder 很重，不要在每个 ROI 里重复计算整帧。
- 可以使用低分辨率缓存，然后 ROI 采样放大。
- 如果使用压缩资源，尽量做 ROI 解码或 tile/block 解码。
