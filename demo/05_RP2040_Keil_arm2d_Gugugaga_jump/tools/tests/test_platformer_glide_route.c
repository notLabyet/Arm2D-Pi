#define main existing_game_tests
#include "test_platformer_game.c"
#undef main

int main(void)
{
    /* A nine-object route is deferred intact when only eight slots are free. */
    platformer_game_t pending=empty_game();
    pending.lNextSectionX=500;pending.lXQ8=100*256;pending.wSectionIndex=3;
    for(unsigned seed=1;;seed++) {
        uint32_t next=seed*UINT32_C(1664525)+UINT32_C(1013904223);
        if(1u+(next>>16)%4u==4u){pending.wRandomState=seed;break;}
    }
    for(unsigned n=0;n<8;n++)
        pending.tObjects[n]=(platformer_game_object_t){.lX=900,.hwWidth=24,.bActive=true};
    uint32_t random=pending.wRandomState;
    __game_generate_course(&pending);
    assert(pending.wSectionIndex==3 && pending.lNextSectionX==500 && pending.wRandomState==random);
    pending.tObjects[0].bActive=false;
    __game_generate_course(&pending);
    assert(pending.wSectionIndex==4 && pending.lNextSectionX>=1220);
    unsigned active=0;
    for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++)active+=pending.tObjects[n].bActive;
    assert(active==16);
    const int speeds[]={90,135,180};
    for(unsigned v=0;v<3;v++) {
        unsigned wins=0,best_normal=0,min_chain=999;
        for(int lead=24;lead<=48;lead+=8)
        for(int reward=24;reward<=56;reward+=8)
        for(int edge=20;edge<=36;edge+=8) {
            for(unsigned held=0;held<=1;held++) {
                platformer_game_t g=empty_game();
                const int base=170;
                for(unsigned n=0;n<c_chRouteObjectCounts[4];n++) {
                    const game_route_object_t *o=&c_tRoutes[4][n];
                    __game_add_object(&g,base+o->iX,o->chRise,o->chWidth,
                        o->chType==PLATFORMER_GAME_PLATFORM?10:24,o->chType);
                }
                unsigned chain=0,glide_frames=0;
                while(g.lXQ8/256<base+560) {
                    bool press=route_press(&g,lead,reward,edge);
                    if(g.bGrounded)g.bGlideHeld=held && press && g.lFootYQ8/256<=130;
                    g.bGlideQualified=g.bGlideHeld;
                    advance(&g,1,(int16_t)speeds[v],press);
                    if(g.hwCombo>chain)chain=g.hwCombo;
                    glide_frames+=g.bGliding;
                }
                if(held && g.wCookies==7) {
                    wins++;assert(glide_frames>0);
                    if(chain<min_chain)min_chain=chain;
                }
                if(!held && g.wCookies>best_normal)best_normal=g.wCookies;
            }
        }
        printf("Glide route %d px/s: full-collection windows=%u/60, normal best=%u/7, min chain=%u\n",
            speeds[v],wins,best_normal,min_chain);
        assert(wins>=3 && best_normal<7 && min_chain>=6);
    }
}
