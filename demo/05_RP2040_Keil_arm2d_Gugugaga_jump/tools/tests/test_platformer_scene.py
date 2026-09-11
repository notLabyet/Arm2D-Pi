"""Exercise actual scene update/draw functions with host Arm-2D boundary stubs.

Checks dirty-region coverage with real sprite pixels and rendering culls with
clipped host targets. This does not emulate the PFB optimizer, RGB565 rounding,
DMA, or hardware display timing.
"""
from pathlib import Path
import re
import subprocess
import json
from PIL import Image, ImageChops, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
source_dir = root / 'application'
work = root / '_compile_check/platformer_render_after'
work.mkdir(parents=True, exist_ok=True)
source = (source_dir / 'arm_2d_scene_platformer.c').read_text()
header = (source_dir / 'arm_2d_scene_platformer.h').read_text()


def function(name):
    match = re.search(r'^static[^;{}]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    if name == '__pfb_draw_scene_platformer_handler':
        match = re.search(r'^static\s+IMPL_PFB_ON_DRAW\(' + name + r'\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


types = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "platformer_game.h"
#include "platformer_speed_control.h"
typedef struct { int16_t iX, iY; } arm_2d_location_t;
typedef struct { int16_t iWidth, iHeight; } arm_2d_size_t;
typedef struct { arm_2d_location_t tLocation; arm_2d_size_t tSize; } arm_2d_region_t;
typedef struct { arm_2d_region_t tRegion; struct { bool bDerivedResource; } tInfo;
    int id, sx, sy; uint16_t *phwBuffer; uint8_t *pchBuffer; } arm_2d_tile_t;
typedef struct { bool suspended; arm_2d_region_t region, dirty; unsigned frame; } arm_2d_helper_dirty_region_item_t;
typedef struct { void *ptPlayer; } arm_2d_scene_t;
#define this (*ptThis)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define dimof(a) (sizeof(a)/sizeof((a)[0]))
#define __RGB(r,g,b) (((r)<<16)|((g)<<8)|(b))
#define GLCD_COLOR_WHITE 0xFFFFFF
#define GLCD_COLOR_BLACK 0
#define ARM_2D_OP_WAIT_ASYNC() ((void)0)
#define ARM_2D_UNUSED(v) ((void)(v))
#define ARM_2D_DRW_PATN_MODE_COPY 0
#define ARM_2D_ALIGN_CENTRE 0
#define IMPL_PFB_ON_DRAW(name) int name(void *pTarget, const arm_2d_tile_t *ptTile, bool bIsNewFrame)
#define arm_2d_canvas(tile, name) for (arm_2d_region_t name = {.tSize = {320,240}}; name.tSize.iWidth; name.tSize.iWidth=0)
#define arm_fsm_rt_cpl 0
typedef struct { int width, height; } arm_2d_font_t;
const struct { arm_2d_font_t use_as__arm_2d_font_t; } ARM_2D_FONT_6x8 = {{6,8}}, ARM_2D_FONT_16x24 = {{16,24}};
'''
types += '#include "' + (root / "platform/display_profile.h").as_posix() + '"\n'
types += "platform_display_profile_t platform_display_profile_get(void) { return (platform_display_profile_t){.clock_hz=250000000}; }\n"
types += "uint32_t platform_display_profile_timestamp(void) { return 0; }\nvoid platform_display_profile_section(unsigned n, uint32_t t) { (void)n;(void)t; }\n"
macros = source[source.index('#define PLATFORMER_WALK_FRAME_COUNT'):
                source.index('/*============================ GLOBAL VARIABLES')]
audit_type = (source_dir / 'power_key_audit.h').read_text()
fields = header.split('ARM_PRIVATE(\n', 1)[1].split('\n)\n', 1)[0]
counts = '\n'.join(line for line in header.splitlines() if line.startswith('#define PLATFORMER_'))
assets = [('HeroWalk', 'platformer_hero_walk_8x48x64.png'),
          ('HeroIdle', 'platformer_hero_idle_12x48x64.png'),
          ('HeroJump', 'platformer_hero_jump_12x64x64.png'),
          ('Grass', 'platformer_grass_12x33x21.png'),
          ('Stone', 'platformer_stone_8x48x31.png'),
          ('Cloud', 'platformer_cloud.png'), ('MidFar', 'platformer_mid_far.png'),
          ('Cookie', 'platformer_cookie_24x24.png'),
          ('HeroGlide', 'platformer_hero_glide_throughflow_9x96x64.png'),
          ('Combo', 'platformer_combo_pixel_99x24.png')]
tiles = 'static uint16_t fake_far_rgb[1040*114];\nstatic uint8_t fake_far_mask[1040*114];\nstatic uint16_t fake_cloud_rgb[1040*62];\n'
for index, (name, file) in enumerate(assets):
    w, h = Image.open(root / 'assets/platformer' / file).size
    for suffix in ['RGB565', 'Mask']:
        extra = ', .phwBuffer=fake_far_rgb, .pchBuffer=fake_far_mask' if name == 'MidFar' else ', .phwBuffer=fake_cloud_rgb' if name == 'Cloud' else ''
        tiles += 'const arm_2d_tile_t c_tilePlatformer%s%s = {.tRegion.tSize = {%d,%d}, .id=%d%s};\n' % (name,suffix,w,h,index,extra)

stubs = r'''
static uint32_t clock_ms;
static int16_t tilt_deg10, input_x = 15000;
static bool imu_ready = true, key_press, key_held;
static unsigned bg_refreshes;
static FILE *trace;
static arm_2d_region_t text_region;
static int text_w = 6, text_h = 8;
static unsigned text_colour;
static bool dry_run, new_frame;
static arm_2d_region_t clip = {{0,0},{320,240}};
static unsigned draw_ops, dry_ops, clipped_ops, frames;
static FILE *audit;
static bool qmi8658_motion_get_tilt_angle_deg10(int16_t *angle) { *angle=tilt_deg10; return imu_ready; }
static bool qmi8658_motion_get_position(int16_t *x, void *y) { (void)y; *x=input_x; return imu_ready; }
static void qmi8658_motion_reset_position(int16_t x, int16_t y) { (void)y; input_x=x; }
static bool power_key_service_is_pressed(void) { return key_held; }
static bool power_key_service_is_raw_pressed(void) { return key_held; }
static bool power_key_service_glide_ready(void) { return key_held; }
void power_key_service_get_audit(power_key_audit_t *out) { *out=(power_key_audit_t){.edges=clock_ms/250, .age_ms=(uint16_t)(clock_ms%250), .sio_changes=4, .pad_changes=4, .sio_seen=3, .pad_seen=3, .window_ms=1000}; }
static uint16_t power_key_service_max_sample_gap_ms(void) { return 1; }
static void power_key_service_get_pulse_ms(uint16_t *low, uint16_t *high) { *low=115; *high=35; }
static void power_key_service_set_airborne(bool airborne) { (void)airborne; }
static bool power_key_service_consume_press(void) { bool p=key_press; key_press=false; return p; }
static int64_t arm_2d_helper_get_system_timestamp(void) { return clock_ms; }
static int64_t arm_2d_helper_convert_ticks_to_ms(int64_t t) { return t; }
static bool arm_2d_helper_is_time_out(uint32_t p, int64_t *t) {
    if ((int64_t)clock_ms - *t >= p) { *t=clock_ms; return true; } return false;
}
static void arm_2d_scene_player_update_scene_background(void *p) { (void)p; bg_refreshes++; }
static void arm_2d_helper_dirty_region_item_suspend_update(arm_2d_helper_dirty_region_item_t *d, bool v) { d->suspended=v; }
static void arm_2d_region_get_minimal_enclosure(const arm_2d_region_t *,const arm_2d_region_t *,arm_2d_region_t *);
static void arm_2d_helper_dirty_region_update_item(arm_2d_helper_dirty_region_item_t *d,
    arm_2d_tile_t *t, arm_2d_region_t *c, arm_2d_region_t *r) {
    (void)t; (void)c;
    if(d->frame==clock_ms)return;
    d->frame=clock_ms;
    arm_2d_region_t next=r?*r:(arm_2d_region_t){0};
    arm_2d_region_get_minimal_enclosure(&d->region,&next,&d->dirty);
    d->region=next;
}
static bool arm_2d_helper_pfb_is_region_being_drawing(const arm_2d_tile_t *t, const arm_2d_region_t *r, const arm_2d_tile_t **v) {
    (void)t; (void)v;
    if(dry_run)return false;
    return !r || (r->tLocation.iX<clip.tLocation.iX+clip.tSize.iWidth
      && r->tLocation.iY<clip.tLocation.iY+clip.tSize.iHeight
      && r->tLocation.iX+r->tSize.iWidth>clip.tLocation.iX
      && r->tLocation.iY+r->tSize.iHeight>clip.tLocation.iY);
}
static bool arm_2d_helper_pfb_is_region_active(const arm_2d_tile_t *t, const arm_2d_region_t *r, bool b) {
    return new_frame || (b && dry_run) || arm_2d_helper_pfb_is_region_being_drawing(t,r,NULL);
}
static void arm_2d_region_get_minimal_enclosure(const arm_2d_region_t *a, const arm_2d_region_t *b, arm_2d_region_t *r) {
    if(a->tSize.iWidth<=0 || a->tSize.iHeight<=0) { *r=*b;return; }
    if(b->tSize.iWidth<=0 || b->tSize.iHeight<=0) { *r=*a;return; }
    int x=MIN(a->tLocation.iX,b->tLocation.iX),y=MIN(a->tLocation.iY,b->tLocation.iY);
    int right=MAX(a->tLocation.iX+a->tSize.iWidth,b->tLocation.iX+b->tSize.iWidth);
    int bottom=MAX(a->tLocation.iY+a->tSize.iHeight,b->tLocation.iY+b->tSize.iHeight);
    *r=(arm_2d_region_t){{x,y},{right-x,bottom-y}};
}
static void arm_2d_tile_generate_child(const arm_2d_tile_t *s, const arm_2d_region_t *r, arm_2d_tile_t *d, bool c) {
    (void)c; assert(r->tLocation.iX>=0 && r->tLocation.iY>=0);
    assert(r->tLocation.iX+r->tSize.iWidth<=s->tRegion.tSize.iWidth);
    assert(r->tLocation.iY+r->tSize.iHeight<=s->tRegion.tSize.iHeight);
    *d=*s; d->tRegion=*r; d->sx+=r->tLocation.iX; d->sy+=r->tLocation.iY;
}
static void copy(const arm_2d_tile_t *s, const arm_2d_tile_t *m, const arm_2d_tile_t *t, const arm_2d_region_t *r) {
    (void)t;
    draw_ops++;
    if(m) { assert(s->id==m->id && s->sx==m->sx && s->sy==m->sy); }
    if(trace) fprintf(trace,"[\"blit\",%d,%d,%d,%d,%d,%d,%d,%d]\n",s->id,s->sx+(s->phwBuffer==&s_hwMidFarCache[0][0]?s_iMidFarCacheX:s->phwBuffer==&s_hwCloudCache[0][0]?s_iCloudCacheX:0),s->sy,
        s->tRegion.tSize.iWidth,s->tRegion.tSize.iHeight,r->tLocation.iX,r->tLocation.iY,m!=NULL);
}
static void arm_2d_tile_copy_only(const arm_2d_tile_t *s,const arm_2d_tile_t *t,const arm_2d_region_t *r) { copy(s,NULL,t,r); }
static void arm_2d_tile_copy_with_src_mask_only(const arm_2d_tile_t *s,const arm_2d_tile_t *m,const arm_2d_tile_t *t,const arm_2d_region_t *r) { copy(s,m,t,r); }
static void arm_2d_fill_colour(const arm_2d_tile_t *t, const arm_2d_region_t *r, unsigned c) {
    draw_ops++;
    (void)t; if(trace) fprintf(trace,"[\"fill\",%d,%d,%d,%d,%u]\n",r->tLocation.iX,r->tLocation.iY,r->tSize.iWidth,r->tSize.iHeight,c);
}
static void arm_lcd_text_set_target_framebuffer(const arm_2d_tile_t *t) { (void)t; }
static void arm_lcd_text_set_font(const arm_2d_font_t *f) { text_w=f->width; text_h=f->height; }
static void arm_lcd_text_set_display_mode(unsigned m) { (void)m; }
static void arm_lcd_text_set_opacity(unsigned o) { (void)o; }
static void arm_lcd_text_set_draw_region(arm_2d_region_t *r) { if(r) text_region=*r; }
static void arm_lcd_text_set_colour(unsigned f, unsigned b) { text_colour=f; (void)b; }
static void arm_lcd_text_location(int a,int b) { (void)a; (void)b; }
static void text_line(bool centre, const char *fmt, va_list args) {
    char buf[128]; vsnprintf(buf,sizeof(buf),fmt,args);
    draw_ops++;
    unsigned length=0, lines=1, current=0;
    for(unsigned n=0;buf[n];n++) { if(buf[n]=='\n') {if(current>length)length=current;current=0;lines++;}else current++; }
    if(current>length)length=current;
    assert(length*(unsigned)text_w<=(unsigned)text_region.tSize.iWidth);
    assert(lines*(unsigned)text_h<=(unsigned)text_region.tSize.iHeight);
    if(trace) {
        fprintf(trace,"[\"text\",%d,%d,%d,%d,%d,%d,%u,%d,\"",text_region.tLocation.iX,text_region.tLocation.iY,
          text_region.tSize.iWidth,text_region.tSize.iHeight,text_w,text_h,text_colour,centre);
        for(unsigned n=0;buf[n];n++) { if(buf[n]=='\n')fputs("\\n",trace);else fputc(buf[n],trace); }
        fputs("\"]\n",trace);
    }
}
static void arm_lcd_printf(const char *fmt,...) { va_list args; va_start(args,fmt);text_line(false,fmt,args);va_end(args); }
static void arm_lcd_puts(const char *str) { arm_lcd_printf("%s",str); }
static void arm_lcd_printf_label(int a,const char *fmt,...) { (void)a;va_list args;va_start(args,fmt);text_line(true,fmt,args);va_end(args); }
'''
code = types + audit_type + macros + counts + '\ntypedef struct { arm_2d_scene_t use_as__arm_2d_scene_t;\n' + fields
code += '\n} user_scene_platformer_t;\n' + tiles + source[source.index('#define PLATFORMER_MID_FAR_CACHE_WIDTH'):source.index('/*============================ PROTOTYPES')] + stubs
for name in ['__platformer_random_next','__platformer_update_foreground','__platformer_reset_foreground',
             '__platformer_rebase_world','__platformer_draw_parallax_layer','__platformer_draw_mid_far_layer',
             '__platformer_draw_foreground','__platformer_get_player_hit_box','__platformer_set_animation_frame',
             '__platformer_reset_player','__platformer_update_player','__platformer_update_animation',
             '__platformer_update_course_regions','__on_scene_platformer_frame_start','__on_scene_platformer_frame_complete','__platformer_draw_course','__platformer_draw_hud','__platformer_draw_combo_part','__platformer_draw_combo',
             '__pfb_draw_scene_platformer_handler']:
    if name in source:
        code += function(name)
code += r'''
static void draw(user_scene_platformer_t *s, const char *name) {
    arm_2d_tile_t target={0};
    /* One dry run is a lower bound: real dynamic PFB can invoke several. */
    dry_run=true;new_frame=true;draw_ops=0;
    __pfb_draw_scene_platformer_handler(s,&target,true);
    dry_ops+=draw_ops;
    for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++) {
        assert(s->tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COURSE+n].frame==clock_ms);
    }
    dry_run=false;
    trace=audit;
    fprintf(trace,"[\"frame\",%u,%u]\n",frames++,bg_refreshes);
    for(unsigned n=0;n<PLATFORMER_DIRTY_REGION_ITEM_COUNT;n++) {
        arm_2d_helper_dirty_region_item_t *d=&s->tDirtyRegionItems[n];
        if(!d->suspended)fprintf(trace,"[\"dirty\",%d,%d,%d,%d]\n",d->dirty.tLocation.iX,
          d->dirty.tLocation.iY,d->dirty.tSize.iWidth,d->dirty.tSize.iHeight);
    }
    clip=(arm_2d_region_t){{0,0},{320,240}};
    __pfb_draw_scene_platformer_handler(s,&target,false);
    trace=NULL;
    if(name) { trace=fopen(name,"w");assert(trace); }
    if(name)__pfb_draw_scene_platformer_handler(s,&target,true);
    if(trace) { fclose(trace);trace=NULL; }
    /* A score-only PFB must not submit unrelated world/text-hint operations. */
    clip=(arm_2d_region_t){{38,8},{120,8}};draw_ops=0;
    trace=audit;fputs("[\"clip\",38,8,120,8]\n",trace);
    __pfb_draw_scene_platformer_handler(s,&target,true);
    clipped_ops+=draw_ops;
    clip=(arm_2d_region_t){{0,84},{320,60}};
    fputs("[\"clip\",0,84,320,60]\n",trace);
    __pfb_draw_scene_platformer_handler(s,&target,true);
    clip=(arm_2d_region_t){{110,150},{88,54}};
    fputs("[\"clip\",110,150,88,54]\n",trace);
    __pfb_draw_scene_platformer_handler(s,&target,true);
    trace=NULL;
    clip=(arm_2d_region_t){{0,0},{320,240}};
    assert(s->tDirtyRegionItems[0].region.tLocation.iY==s->tPlayerLocation.iY+2);
    __on_scene_platformer_frame_complete((arm_2d_scene_t *)s);
}
static void tick(user_scene_platformer_t *s, unsigned dt, bool key) {
    clock_ms+=dt;key_press=key;__on_scene_platformer_frame_start((arm_2d_scene_t *)s);
    assert(s->tPlayerHitBox.tLocation.iY+s->tPlayerHitBox.tSize.iHeight == s->tGame.lFootYQ8/256);
    assert(s->lPlayerWorldX==s->tGame.lXQ8/256);
}

int main(void) {
    audit=fopen("frames.jsonl","w");assert(audit);
    user_scene_platformer_t s={0};
    s.tPlayfield.tSize=(arm_2d_size_t){320,240};
    s.tGround=(arm_2d_region_t){.tLocation={0,198},.tSize={320,42}};
    s.tPlayerStartLocation=(arm_2d_location_t){90,134};
    __platformer_reset_player(&s);__platformer_reset_foreground(&s);
    tick(&s,10,false);draw(&s,"start.jsonl");
    assert(s.tSpeedControl.speed_milli==90000 && s.bWalking);
    int32_t cruise_x=s.tGame.lXQ8;
    tick(&s,100,false);draw(&s,NULL);
    assert(s.tSpeedControl.speed_milli==90000 && s.tGame.lXQ8>cruise_x);
    s.tSpeedControl.speed_milli=0;tilt_deg10=0;imu_ready=false;tick(&s,10,true);tick(&s,100,false);
    assert(!s.tGame.bGrounded && s.tPlayerLocation.iY<134);
    for(unsigned n=0;n<100;n++) { tick(&s,10,false);draw(&s,NULL); }
    assert(s.tGame.bGrounded);
    imu_ready=true;s.tSpeedControl.speed_milli=180000;tilt_deg10=0;
    bool landed=false;
    int32_t first_platform=s.tGame.tObjects[0].lX;
    for(unsigned n=0;n<300;n++) {
        bool key=s.lPlayerWorldX>=first_platform-44
                 && s.lPlayerWorldX<=first_platform-42 && s.tGame.bGrounded;
        tick(&s,10,key);draw(&s,NULL);
        if(s.tGame.bGrounded && s.tGame.lFootYQ8/256<198) {
            draw(&s,"platform.jsonl");landed=true;break;
        }
    }
    assert(landed);
    for(unsigned n=0;n<300;n++) { tick(&s,10,n%55u==0);draw(&s,NULL); }
    draw(&s,"cookies.jsonl");
    /* Prime a high cookie while stationary, then jump through it. Its dirty
     * rectangle must retire even when the camera has not moved at all. */
    memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
    s.tGame.lNextSectionX=100000;s.tGame.lFootYQ8=198*256;
    s.tGame.lVelocityYQ8=0;s.tGame.bGrounded=true;s.tGame.hwBufferMs=0;
    s.tGame.wCookies=99999;
    s.tGame.tObjects[0]=(platformer_game_object_t){.lX=s.lPlayerWorldX+10,
        .iTop=94,.hwWidth=24,.chHeight=24,.chType=PLATFORMER_GAME_COOKIE,.bActive=true};
    s.tSpeedControl.speed_milli=0;tilt_deg10=0;bg_refreshes++;
    tick(&s,10,false);draw(&s,NULL);
    int32_t camera=s.lCameraWorldX;
    bool erased=false;
    for(unsigned n=0;n<70;n++) {
        tick(&s,10,n==0);draw(&s,NULL);
        if(!s.tGame.tObjects[0].bActive && !erased) {
            erased=true;
            assert(!s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COURSE].suspended);
            assert(!s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_HUD].suspended);
        }
        assert(s.lCameraWorldX==camera);
    }
    assert(erased && s.tGame.wCookies==100000);
    draw(&s,"count.jsonl");
    /* A platform crossing the left boundary also erases its final 2 pixels. */
    s.tGame.tObjects[0]=(platformer_game_object_t){.lX=s.lCameraWorldX-8,.iTop=150,.hwWidth=10,
        .chHeight=10,.chType=PLATFORMER_GAME_PLATFORM,.bActive=true};
    bg_refreshes++;tick(&s,10,false);draw(&s,NULL);s.tSpeedControl.speed_milli=180000;tilt_deg10=0;
    for(unsigned n=0;n<4;n++) { tick(&s,10,false);draw(&s,NULL); }
    /* Exercise airflow onset and six stable phases at different cruise speeds, release and landing,
     * with real sprite pixels included in dirty-region coverage checks. */
    s.tSpeedControl.speed_milli=0;tilt_deg10=0;key_held=true;
    memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
    s.tGame.lNextSectionX=100000;
    s.tGame.lFootYQ8=198*256;s.tGame.lVelocityYQ8=0;
    s.tGame.bGrounded=true;s.tGame.hwBufferMs=0;
    bg_refreshes++;
    unsigned glide_frames=0;
    unsigned last_glide_frame=0, glide_loops=0;
    for(unsigned n=0;n<160;n++) {
        s.tSpeedControl.speed_milli=n<40 ? 45000 : n<55 ? 90000 : n<80 ? 180000 : 0;
        tick(&s,10,n==0);draw(&s,NULL);
        if(n==38)draw(&s,"glide_slow.jsonl");
        if(n==50)draw(&s,"glide_medium.jsonl");
        if(n==70)draw(&s,"glide_fast.jsonl");

        if(s.tGame.bGliding) {
            assert(s.chPlayingAnimation==PLATFORMER_ANIMATION_GLIDE);
            assert(s.tHeroTile.sx==s.u8AnimationFrame*96);
            assert(s.tHeroMask.sx==s.tHeroTile.sx);
            assert(s.tHeroTile.tRegion.tSize.iWidth==96);
            if(glide_frames==0) assert(s.u8AnimationFrame==0);
            if(s.u8AnimationFrame != last_glide_frame) {
                assert(s.u8AnimationFrame==(last_glide_frame==8 ? 3 : last_glide_frame+1));
                if(last_glide_frame==8) glide_loops++;
            }
            last_glide_frame=s.u8AnimationFrame;
            glide_frames|=1u<<s.u8AnimationFrame;
        }
    }
    assert(glide_frames==511u && glide_loops>=1);
    key_held=false;tick(&s,10,false);draw(&s,NULL);
    assert(!s.tGame.bGliding && s.chPlayingAnimation==PLATFORMER_ANIMATION_JUMP);
    for(unsigned n=0;n<100;n++) { tick(&s,10,false);draw(&s,NULL); }
    assert(s.tGame.bGrounded);
    /* Re-entry starts with no airflow and retains the first frame for 100 ms. */
    s.tGame.bGliding=true;__platformer_update_animation(&s);
    assert(s.u8AnimationFrame==0);
    clock_ms+=99;__platformer_update_animation(&s);assert(s.u8AnimationFrame==0);
    clock_ms++;__platformer_update_animation(&s);assert(s.u8AnimationFrame==1);
    s.tGame.bGliding=false;__platformer_update_animation(&s);
    /* Collect at rest to expose combo pop/bar/expiry damage without scrolling. */
    s.tGame.hwCombo=0;s.tGame.hwComboRemainingMs=0;
    s.tSpeedControl.speed_milli=0;tilt_deg10=0;
    memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
    s.tGame.lNextSectionX=100000;
    bg_refreshes++;tick(&s,10,false);draw(&s,NULL);
    for(unsigned n=0;n<12;n++) {
        s.tGame.tObjects[0]=(platformer_game_object_t){
            .lX=s.tGame.lXQ8/256+12,.iTop=160,.hwWidth=24,.chHeight=24,
            .chType=PLATFORMER_GAME_COOKIE,.bActive=true};
        tick(&s,10,false);draw(&s,NULL);
        assert(s.tGame.hwCombo==n+1);
        if(n==1)draw(&s,"combo2.jsonl");
        if(n==11)draw(&s,"combo12.jsonl");
        for(unsigned k=0;k<21;k++){tick(&s,10,false);draw(&s,NULL);}
    }
    assert(s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COMBO].suspended);
    for(unsigned n=0;n<PLATFORMER_GAME_COMBO_WINDOW_MS/10-22;n++){tick(&s,10,false);draw(&s,NULL);}
    assert(s.tGame.hwCombo==12 && s.tGame.hwComboRemainingMs==10);
    tick(&s,10,false);draw(&s,"combo_expired.jsonl");
    assert(s.tGame.hwCombo==0 && !s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COMBO].suspended);
    tick(&s,10,false);draw(&s,NULL);
    assert(s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COMBO].suspended);
    /* Three-digit atlas composition and 99->100 carry fit the compact region. */
    s.tGame.hwCombo=98;s.tGame.hwComboRemainingMs=2000;
    bg_refreshes++;tick(&s,10,false);draw(&s,NULL);
    for(unsigned value=99;value<=100;value++) {
        s.tGame.tObjects[0]=(platformer_game_object_t){
            .lX=s.tGame.lXQ8/256+12,.iTop=160,.hwWidth=24,.chHeight=24,
            .chType=PLATFORMER_GAME_COOKIE,.bActive=true};
        tick(&s,10,false);draw(&s,value==100 ? "combo100.jsonl" : NULL);
        assert(s.tGame.hwCombo==value);
    }
    s.tGame.hwCombo=999;
    bg_refreshes++;tick(&s,10,false);draw(&s,"combo999.jsonl");
    /* FPS is based on completed, actually drawn frames, never dry-run calls. */
    s.wFPSWindowMs=clock_ms;s.hwFrameCount=0;
    for(unsigned n=0;n<60;n++) {
        clock_ms+=n%3u==0 ? 16u : 17u;
        s.bFrameDrawn=true;__on_scene_platformer_frame_complete((arm_2d_scene_t *)&s);
    }
    assert(s.hwFPS==60);
    /* Hidden diagnostics must not invalidate the HUD when profile/FPS changes. */
    s.hwDrawnFPS=s.hwFPS;s.tDisplayProfile.sequence=UINT32_MAX;
    __on_scene_platformer_frame_start((arm_2d_scene_t *)&s);
    assert(s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FPS].suspended);
    __on_scene_platformer_frame_start((arm_2d_scene_t *)&s);
    assert(s.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FPS].suspended);
    /* Check actual generated silhouettes, including short and long landings. */
    for(unsigned wanted=0;wanted<8;wanted++) {
        __platformer_reset_player(&s);__platformer_reset_foreground(&s);
        bool found=false;
        for(unsigned seed=1;seed<=10000;seed++) {
            platformer_game_init(&s.tGame,198,90,clock_ms);
            memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
            s.tGame.wSectionIndex=3;s.tGame.wRandomState=seed;s.tGame.lNextSectionX=170;
            s.tGame.chLastRoute=UINT8_MAX;s.tGame.chPreviousRoute=UINT8_MAX;
            platformer_game_update(&s.tGame,clock_ms+10,0,false);
            if(s.tGame.chLastRoute==wanted){found=true;break;}
        }
        assert(found);
        s.tGame.lXQ8=(s.tGame.tObjects[0].lX-50)*256;
        s.tSpeedControl.speed_milli=0;s.tGame.lNextSectionX=100000;key_held=false;
        bg_refreshes++;tick(&s,10,false);
        char name[40];snprintf(name,sizeof(name),"route_variant_%u.jsonl",wanted);draw(&s,name);
    }
    /* Render a production-generated glide trail from its upper launch pad. */
    __platformer_reset_player(&s);__platformer_reset_foreground(&s);
    bool glide_route_found=false;
    for(unsigned seed=1;seed<=100;seed++) {
        platformer_game_init(&s.tGame,198,90,clock_ms);
        memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
        s.tGame.wSectionIndex=3;s.tGame.wRandomState=seed;s.tGame.lNextSectionX=170;
        platformer_game_update(&s.tGame,clock_ms+10,0,false);
        unsigned cookies=0;
        for(unsigned j=0;j<PLATFORMER_GAME_OBJECT_COUNT;j++)
            cookies+=s.tGame.tObjects[j].bActive && s.tGame.tObjects[j].chType==PLATFORMER_GAME_COOKIE;
        if(cookies==7){glide_route_found=true;break;}
    }
    assert(glide_route_found);
    s.tGame.lXQ8=(s.tGame.tObjects[1].lX+s.tGame.tObjects[1].hwWidth-32)*256;
    s.tGame.lFootYQ8=s.tGame.tObjects[1].iTop*256;s.tGame.lVelocityYQ8=0;s.tGame.bGrounded=true;
    s.tGame.lNextSectionX=100000;s.tSpeedControl.speed_milli=135000;
    key_held=false;tilt_deg10=0;
    bg_refreshes++;tick(&s,10,false);draw(&s,"glide_route_launch.jsonl");
    key_held=true;
    for(unsigned n=0;n<230;n++) {
        tick(&s,10,n==0);draw(&s,NULL);
        if(n==40)draw(&s,"glide_route_flight.jsonl");
    }
    assert(s.tGame.wCookies==6 && s.tGame.hwCombo==6);
    draw(&s,"glide_route_combo.jsonl");
    /* A debounced new edge must re-arm even if the released level was never
     * visible to the scene between frames. Exercise the actual scene handoff. */
    memset(s.tGame.tObjects,0,sizeof(s.tGame.tObjects));
    s.tGame.lNextSectionX=100000;s.tSpeedControl.speed_milli=0;
    s.tGame.lFootYQ8=198*256;s.tGame.lVelocityYQ8=0;
    s.tGame.bGrounded=true;s.tGame.bJumpArmed=false;s.tGame.hwBufferMs=0;
    s.tGame.hwGlideHoldMs=300;key_held=true;
    tick(&s,24,true);
    assert(!s.tGame.bGrounded && !s.tGame.bGliding && s.tGame.hwGlideHoldMs<=30);
    puts("PASS scene: fresh sampled edge re-arms across an unseen release");
    fclose(audit);
    printf("METRICS frames=%u dry_run_ops=%u score_clip_ops=%u\n",frames,dry_ops,clipped_ops);
    puts("PASS scene: level cruise and hidden diagnostic refresh");
    puts("PASS scene: jump/platform feet, child tiles, running collection, stationary collection dirty regions, six-digit carry, frame-completion FPS");
}

'''
(work / 'checks.c').write_text(code)
exe = work / 'checks.exe'
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I',str(root/'application'),
                str(work/'checks.c'),str(root/'application/platformer_game.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],cwd=work,check=True)

atlases = [Image.open(root/'assets/platformer'/file).convert('RGBA') for _,file in assets]
fonts = {h: ImageFont.truetype('C:/Windows/Fonts/consola.ttf',h) for h in [8,24]}
glyphs = {}


def render(operations):
    canvas=Image.new('RGB',(320,240),(128,203,238))
    for op,*args in operations:
        if op=='blit':
            i,sx,sy,w,h,x,y,masked=args
            img=atlases[i].crop((sx,sy,sx+w,sy+h))
            canvas.paste(img,(x,y),img if masked else None)
        elif op=='fill':
            x,y,w,h,c=args
            ImageDraw.Draw(canvas).rectangle((x,y,x+w-1,y+h-1),fill=((c>>16)&255,(c>>8)&255,c&255))
        else:
            x,y,w,h,cw,ch,c,center,txt=args
            if center:
                x+=(w-len(txt)*cw)//2
                y+=(h-ch)//2
            for row,text in enumerate(txt.splitlines()):
                for col,char in enumerate(text):
                    key=(cw,ch,c,char)
                    if key not in glyphs:
                        glyph=Image.new('RGBA',(cw,ch))
                        ImageDraw.Draw(glyph).text((0,-2),char,font=fonts[ch],
                            fill=((c>>16)&255,(c>>8)&255,c&255,255))
                        glyphs[key]=glyph
                    canvas.paste(glyphs[key],(x+col*cw,y+row*ch),glyphs[key])
    return canvas


for state in ['start','platform','cookies','count','glide_slow','glide_medium','glide_fast',
              'combo2','combo12','combo_expired','combo100','combo999',
              'glide_route_launch','glide_route_flight','glide_route_combo'] + [f'route_variant_{n}' for n in range(8)]:
    canvas=render([json.loads(line) for line in (work/f'{state}.jsonl').read_text().splitlines()])
    canvas.resize((960,720),Image.Resampling.NEAREST).save(work/f'{state}.png')
print('Saved offline scene layout previews (font rasterization approximated).')

previous = None
previous_refresh = -1
groups = []
for line in (work/'frames.jsonl').read_text().splitlines():
    item = json.loads(line)
    if item[0] == 'frame':
        groups.append([item, [], [], []])
        current_ops=groups[-1][2]
    elif item[0] == 'dirty':
        groups[-1][1].append(item[1:])
    elif item[0] == 'clip':
        current_ops=[]
        groups[-1][3].append((item[1:],current_ops))
    else:
        current_ops.append(item)
pixels = 0
hashes = []
import hashlib
for (_, frame, refresh), regions, operations, clips in groups:
    current = render(operations)
    hashes.append(hashlib.sha256(current.tobytes()).hexdigest())
    for (x,y,w,h), clip_ops in clips:
        bounds=(x,y,x+w,y+h)
        clipped=render(clip_ops).crop(bounds)
        assert ImageChops.difference(clipped,current.crop(bounds)).getbbox() is None, (frame,'clipped drawing differs',bounds)
    mask = Image.new('L', current.size)
    full = previous is None or refresh != previous_refresh
    if full:
        mask.paste(255, (0,0,320,240))
    else:
        for x,y,w,h in regions:
            if w>0 and h>0:
                # Same two-pixel X alignment as the display adapter.
                x0=x//2*2
                x1=(x+w+1)//2*2
                ImageDraw.Draw(mask).rectangle((x0,y,x1-1,y+h-1),fill=255)
        changed = ImageChops.difference(current, previous)
        uncovered = Image.composite(Image.new('RGB',current.size),changed,mask)
        assert uncovered.getbbox() is None, (frame, 'pixels changed outside dirty regions', uncovered.getbbox())
    if not full:
        pixels += mask.histogram()[255]
    previous, previous_refresh = current, refresh
(work/'frame_hashes.json').write_text(json.dumps(hashes))
print(f'PASS: {len(groups)} frames, every changed pixel covered, {len(groups)*3} clipped targets match; aligned dirty-union pixels={pixels}')
