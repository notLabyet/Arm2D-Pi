"""Verify the production sliding cache contents and Flash read byte counts."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '_compile_check/cache_test'
out.mkdir(parents=True, exist_ok=True)
s = (root / 'application/arm_2d_scene_platformer.c').read_text()
cache = s[s.index('#define PLATFORMER_MID_FAR_CACHE_WIDTH'):s.index('/*============================ PROTOTYPES')]
stub = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define PLATFORMER_MID_FAR_LAYER_HEIGHT 114
#define PLATFORMER_MID_FAR_MASK_HEIGHT 32
#define PLATFORMER_MID_FAR_SOURCE_WIDTH 1040
#define PLATFORMER_CLOUD_LAYER_HEIGHT 60
#define PLATFORMER_CLOUD_SOURCE_WIDTH 1040
typedef struct { struct { int16_t iX,iY; } tLocation; struct { int16_t iWidth,iHeight; } tSize; } arm_2d_region_t;
typedef struct { arm_2d_region_t tRegion; union { uint16_t *phwBuffer; uint8_t *pchBuffer; }; } arm_2d_tile_t;
static uint16_t pixels[1040*114];
static uint8_t masks[1040*114];
static uint16_t clouds[1040*62];
static const arm_2d_tile_t c_tilePlatformerCloudRGB565={.phwBuffer=clouds};
static const arm_2d_tile_t c_tilePlatformerMidFarRGB565={.phwBuffer=pixels};
static const arm_2d_tile_t c_tilePlatformerMidFarMask={.pchBuffer=masks};
static size_t flash_bytes;
static void *flash_copy(void *dst,const void *src,size_t n) {
    uintptr_t p=(uintptr_t)src;
    assert((p>=(uintptr_t)pixels && p+n<=(uintptr_t)(pixels+1040*114))
        || (p>=(uintptr_t)clouds && p+n<=(uintptr_t)(clouds+1040*62))
        || (p>=(uintptr_t)masks && p+n<=(uintptr_t)(masks+1040*114)));
    flash_bytes+=n;return memcpy(dst,src,n);
}
#define memcpy flash_copy
'''
test = r'''
#undef memcpy
static void check(int x) {
    int delta=x-s_iMidFarCacheX;
    size_t expected=!s_bMidFarCacheValid || delta<0 || delta>=320 ? 83200 : (size_t)delta*260;
    flash_bytes=0;
    __platformer_update_mid_far_cache((int16_t)x);
    assert(flash_bytes==expected);
    for(int y=0;y<114;y++) {
        assert(memcmp(s_hwMidFarCache[y],pixels+y*1040+x,320*2)==0);
        if(y<32)assert(memcmp(s_chMidFarCacheMask[y],masks+y*1040+x,320)==0);
    }
    assert(s_tMidFarCacheTile.phwBuffer==&s_hwMidFarCache[0][0]);
    assert(s_tMidFarCacheMask.pchBuffer==&s_chMidFarCacheMask[0][0]);
    assert(s_tMidFarCacheTile.tRegion.tSize.iWidth==320);
    assert(s_tMidFarCacheTile.tRegion.tSize.iHeight==114);
    assert(s_tMidFarCacheMask.tRegion.tSize.iHeight==32);
    delta=x-s_iCloudCacheX;
    expected=!s_bCloudCacheValid || delta<0 || delta>=320 ? 38400 : (size_t)delta*120;
    flash_bytes=0;__platformer_update_cloud_cache((int16_t)x);
    assert(flash_bytes==expected);
    for(int y=0;y<60;y++)assert(memcmp(s_hwCloudCache[y],clouds+y*1040+x,640)==0);
    assert(s_tCloudCacheTile.phwBuffer==&s_hwCloudCache[0][0]);
    assert(s_tCloudCacheTile.tRegion.tSize.iWidth==320 && s_tCloudCacheTile.tRegion.tSize.iHeight==60);
}
int main(void) {
    for(unsigned n=0;n<1040*114;n++) {
        pixels[n]=(uint16_t)(n*317u+(n/1040)*193u);
        masks[n]=(uint8_t)(n*73u+(n/1040)*11u);
    }
    for(unsigned n=0;n<1040*62;n++)clouds[n]=(uint16_t)(n*997u+n/1040);
    int positions[]={0,0,1,3,319,320,719,0,400,399,720,0};
    for(unsigned n=0;n<sizeof(positions)/sizeof(*positions);n++)check(positions[n]);
    for(int x=0;x<720;x++)check(x);
    puts("PASS: production far/cloud cache RGB565/A8 pixels, no-op, 1px=260B reads, wide jumps, reverse, wrap, all 720 scroll positions");
}
'''
p = out / 'cache.c'
p.write_text(stub + cache + test)
exe = out / 'cache.exe'
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
