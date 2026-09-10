#define main existing_game_tests
#include "test_platformer_game.c"
#undef main

int main(void)
{
    unsigned failures=0,min_wins=999;
    for(unsigned route=0;route<8;route++)
    for(int stretch=92;stretch<=108;stretch+=8)
    for(int lower=0;lower<=8;lower+=4)
    for(int speed=90;speed<=180;speed+=45) {
        platformer_game_t course=empty_game();
        unsigned total=0,wins=0,best=0;
        for(unsigned n=0;n<c_chRouteObjectCounts[route];n++) {
            const game_route_object_t *o=&c_tRoutes[route][n];
            __game_add_object(&course,170+o->iX*stretch/100,o->chRise-lower,
                o->chType==PLATFORMER_GAME_PLATFORM?o->chWidth*stretch/100:24,
                o->chType==PLATFORMER_GAME_PLATFORM?10:24,o->chType);
            total+=o->chType==PLATFORMER_GAME_COOKIE;
        }
        for(int lead=24;lead<=48;lead+=8)
        for(int reward=24;reward<=56;reward+=8)
        for(int edge=20;edge<=36;edge+=8) {
            platformer_game_t g=course;
            while(g.lXQ8/256<800) {
                bool press=route_press(&g,lead,reward,edge);
                if(g.bGrounded)g.bGlideHeld=route==4 && press && g.lFootYQ8/256<=138;
                g.bGlideQualified=g.bGlideHeld;
                advance(&g,1,(int16_t)speed,press);
            }
            if(g.wCookies>best)best=g.wCookies;
            if(g.wCookies==total)wins++;
        }
        if(wins<min_wins)min_wins=wins;
        if(wins<3){failures++;printf("FAIL route=%u stretch=%d lower=%d speed=%d wins=%u best=%u/%u\n",route,stretch,lower,speed,wins,best,total);}
    }
    printf("Geometry sweep: failures=%u minimum windows=%u\n",failures,min_wins);
    if(failures)return 1;
    platformer_game_t g=empty_game();
    g.chLastRoute=UINT8_MAX;g.chPreviousRoute=UINT8_MAX;
    unsigned counts[8]={0},variants[8]={0};
    for(unsigned n=0;n<4096;n++) {
        memset(g.tObjects,0,sizeof(g.tObjects));g.lNextSectionX=500;g.lXQ8=100*256;
        unsigned last=g.chLastRoute,prev=g.chPreviousRoute;
        __game_generate_course(&g);
        unsigned current=g.chLastRoute;assert(current<8);counts[current]++;
        int base_width=c_tRoutes[current][0].chWidth;
        unsigned stretch=g.tObjects[0].hwWidth<base_width?0:g.tObjects[0].hwWidth==base_width?1:2;
        unsigned lower=(c_tRoutes[current][0].chRise-(g.iGroundY-g.tObjects[0].iTop))/4;
        variants[current]|=1u<<(stretch*3+lower);
        if(n>=3){
            assert(c_chRouteFamilies[current]!=c_chRouteFamilies[last]);
            assert(c_chRouteFamilies[current]!=c_chRouteFamilies[prev]);
        }
    }
    for(unsigned n=0;n<8;n++){assert(counts[n]>0 && variants[n]==511u);printf("route %u: %u sections, all 9 variations\n",n,counts[n]);}
    puts("PASS: reachable variations; no family repeats in the previous two sections");
}
