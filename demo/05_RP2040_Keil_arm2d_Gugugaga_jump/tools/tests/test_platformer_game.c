/* Host regression checks compile the same production game module as Keil.
 * gcc -std=c11 -Wall -Wextra -Werror tools/tests/test_platformer_game.c
 *     -o _compile_check/test_platformer_game.exe
 */
#include <assert.h>
#include <stdio.h>
#include "../../application/platformer_game.c"

static platformer_game_t empty_game(void)
{
    platformer_game_t tGame;
    platformer_game_init(&tGame, 198, 90, 0);
    memset(tGame.tObjects, 0, sizeof(tGame.tObjects));
    tGame.lNextSectionX = 100000;
    return tGame;
}

static void advance(platformer_game_t *ptGame, unsigned nSteps,
                     int16_t iSpeed, bool bPress)
{
    for (unsigned n = 0; n < nSteps; n++) {
        (void)platformer_game_update(ptGame, ptGame->wLastUpdateMs + 10u,
                                     iSpeed, bPress && n == 0u);
        assert(platformer_game_jump_frame(ptGame) >= -1);
        assert(platformer_game_jump_frame(ptGame) <= 11);
    }
}

static void check_ballistics(void)
{
    platformer_game_t tGame = empty_game();
    int32_t lPeak = tGame.lFootYQ8;
    unsigned nAirSteps = 0;
    advance(&tGame, 1, 0, true);
    assert(!tGame.bGrounded && tGame.lVelocityYQ8 < 0);
    for (unsigned n = 0; n < 70; n++) {
        if (!tGame.bGrounded) {
            nAirSteps++;
        }
        if (tGame.lFootYQ8 < lPeak) {
            lPeak = tGame.lFootYQ8;
        }
        advance(&tGame, 1, 0, false);
    }
    assert(198 * 256 - lPeak >= 63 * 256 && 198 * 256 - lPeak <= 65 * 256);
    assert(nAirSteps >= 48u && nAirSteps <= 51u);
    assert(tGame.bGrounded && tGame.lFootYQ8 == 198 * 256);
    advance(&tGame, 40, 0, false);
    assert(platformer_game_jump_frame(&tGame) == -1);
    assert(platformer_game_score(&tGame) == 0u);
    puts("PASS: fixed-step gravity, ~64px apex, ~500ms flight, landing animation");
}

static void check_platforms(void)
{
    platformer_game_t tGame = empty_game();
    __game_add_object(&tGame, 70, 32, 160, 10, PLATFORMER_GAME_PLATFORM);
    __game_add_object(&tGame, 70, 48, 160, 10, PLATFORMER_GAME_PLATFORM);
    advance(&tGame, 12, 0, true);
    assert(!tGame.bGrounded && tGame.lFootYQ8 < 166 * 256);
    advance(&tGame, 50, 0, false);
    assert(tGame.bGrounded && tGame.lFootYQ8 == 150 * 256);
    /* Walk off: gravity starts; the continuous ground catches the character. */
    advance(&tGame, 180, 180, false);
    assert(tGame.bGrounded && tGame.lFootYQ8 == 198 * 256);
    tGame = empty_game();
    __game_add_object(&tGame, 70, 48, 160, 10, PLATFORMER_GAME_PLATFORM);
    tGame.lFootYQ8 = 170 * 256; tGame.bGrounded = false;
    tGame.hwCoyoteMs = 0; tGame.lVelocityYQ8 = 50 * 256;
    advance(&tGame, 60, 0, false);
    assert(tGame.lFootYQ8 == 198 * 256); /* No snapping up from below. */
    /* Maximum falling speed still crosses the platform top instead of tunneling. */
    tGame = empty_game();
    __game_add_object(&tGame, 70, 32, 160, 10, PLATFORMER_GAME_PLATFORM);
    tGame.lFootYQ8 = 165 * 256; tGame.bGrounded = false;
    tGame.hwCoyoteMs = 0; tGame.lVelocityYQ8 = 800 * 256;
    advance(&tGame, 1, 0, false);
    assert(tGame.bGrounded && tGame.lFootYQ8 == 166 * 256);
    puts("PASS: one-way platforms, highest descending contact, walk-off, no tunneling");
}

static void check_buffer_and_coyote(void)
{
    platformer_game_t tGame = empty_game();
    advance(&tGame, 10, 0, true);
    int32_t lVelocity = tGame.lVelocityYQ8;
    advance(&tGame, 1, 0, true);
    assert(tGame.lVelocityYQ8 > lVelocity); /* Midair press does not reset velocity. */
    advance(&tGame, 50, 0, false);
    assert(tGame.bGrounded); /* Early midair press expired. */
    tGame = empty_game();
    advance(&tGame, 43, 0, true);
    assert(!tGame.bGrounded && tGame.lVelocityYQ8 > 0);
    advance(&tGame, 10, 0, true);
    assert(!tGame.bGrounded && tGame.lVelocityYQ8 < 0); /* Buffered before landing. */
    for (unsigned wait = 7; wait <= 9; wait += 2) {
        tGame = empty_game();
        __game_add_object(&tGame, 0, 32, 100, 10, PLATFORMER_GAME_PLATFORM);
        tGame.lXQ8 = 91 * 256; /* Left edge of hitbox just past the platform. */
        tGame.lFootYQ8 = 166 * 256;
        advance(&tGame, wait, 0, false);
        advance(&tGame, 1, 0, true);
        if (wait == 7u) {
            assert(tGame.lVelocityYQ8 < 0);
            advance(&tGame, 1, 0, true);
            assert(tGame.hwJumpAgeMs == 20u); /* Coyote jump is consumed once. */
        } else {
            assert(tGame.lVelocityYQ8 > 0);
        }
    }
    puts("PASS: 100ms jump buffer, expiry, 80ms coyote time, no double jump");
}

static void check_collection_and_timing(void)
{
    platformer_game_t tGame = empty_game();
    __game_add_object(&tGame, 150, 24, 24, 24, PLATFORMER_GAME_COOKIE);
    advance(&tGame, 100, 180, false);
    assert(platformer_game_score(&tGame) == 1u && !tGame.tObjects[0].bActive);
    int32_t lStop = tGame.lXQ8;
    advance(&tGame, 100, 180, false);
    assert(tGame.lXQ8 > lStop && platformer_game_score(&tGame) == 1u);
    /* Collect multiple overlapping cookies exactly once; count saturates. */
    tGame = empty_game();
    __game_add_object(&tGame, 100, 24, 24, 24, PLATFORMER_GAME_COOKIE);
    __game_add_object(&tGame, 100, 24, 24, 24, PLATFORMER_GAME_COOKIE);
    tGame.wCookies=999998u;
    advance(&tGame, 1, 0, false);
    assert(tGame.wCookies==999999u);
    assert(!tGame.tObjects[0].bActive && !tGame.tObjects[1].bActive);
    /* A high cookie must remain until the body reaches it during a jump. */
    tGame = empty_game();
    __game_add_object(&tGame, 100, 104, 24, 24, PLATFORMER_GAME_COOKIE);
    advance(&tGame, 20, 0, false);
    assert(tGame.wCookies==0 && tGame.tObjects[0].bActive);
    advance(&tGame, 20, 0, true);
    assert(tGame.wCookies==1 && !tGame.tObjects[0].bActive);

    platformer_game_t a = empty_game(), b = empty_game();
    advance(&a, 60, 180, true);
    (void)platformer_game_update(&b, 0, 180, true);
    for (uint32_t time = 30; time <= 600; time += 30) {
        (void)platformer_game_update(&b, time, 180, false);
    }
    assert(a.lXQ8 == b.lXQ8 && a.lFootYQ8 == b.lFootYQ8);
    assert(a.hwLandingAgeMs == b.hwLandingAgeMs);
    b = empty_game(); b.wLastUpdateMs = UINT32_MAX - 99u;
    (void)platformer_game_update(&b, UINT32_MAX - 99u, 180, true);
    for (uint32_t n = 1; n <= 60; n++) {
        (void)platformer_game_update(&b, UINT32_MAX - 99u + n * 10u, 180, false);
    }
    assert(a.lXQ8 == b.lXQ8 && a.lFootYQ8 == b.lFootYQ8);
    b = empty_game();
    (void)platformer_game_update(&b, 100000, 1000, false);
    assert(b.lXQ8 - 90 * 256 <= 18 * 256); /* Catchup and speed are bounded. */
    lStop = b.lXQ8; advance(&b, 20, -100, false); assert(b.lXQ8 == lStop);
    uint32_t cookies = b.wCookies;
    int32_t next = b.lNextSectionX;
    platformer_game_rebase(&b, 10);
    assert(b.wCookies == cookies && b.lNextSectionX == next - 10);
    puts("PASS: single/multiple/airborne collection, no freeze, counter limit, cadence equality, time wrap, bounded catchup");
}



/* A host-only player reacts to visible reward heights and platform edges.
 * Sweep its reaction distances to require forgiving windows, not a single
 * pre-recorded perfect takeoff. Production gameplay does not use this policy. */
static bool route_press(const platformer_game_t *g, int lead, int reward, int edge)
{
    if (!g->bGrounded) return false;
    int x=g->lXQ8/256, foot=g->lFootYQ8/256;
    const platformer_game_object_t *support=NULL;
    for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++) {
        const platformer_game_object_t *o=&g->tObjects[n];
        if(o->bActive && o->chType==PLATFORMER_GAME_PLATFORM && o->iTop==foot
        && __game_overlaps_x(g,o))support=o;
    }
    if(support) {
        for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++) {
            const platformer_game_object_t *o=&g->tObjects[n];
            if(o->bActive && o->chType==PLATFORMER_GAME_COOKIE
            && o->iTop+o->chHeight<=foot-PLATFORMER_GAME_BODY_HEIGHT
            && o->lX+o->hwWidth>x+PLATFORMER_GAME_BODY_OFFSET_X
            && o->lX-x<=reward)return true;
        }
        for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++) {
            const platformer_game_object_t *o=&g->tObjects[n];
            if(o->bActive && o->chType==PLATFORMER_GAME_PLATFORM
            && o->lX>support->lX && o->lX<=support->lX+support->hwWidth+40
            && x>=support->lX+support->hwWidth-edge)return true;
        }
    } else {
        for(unsigned n=0;n<PLATFORMER_GAME_OBJECT_COUNT;n++) {
            const platformer_game_object_t *o=&g->tObjects[n];
            if(o->bActive && o->chType==PLATFORMER_GAME_PLATFORM
            && g->iGroundY-o->iTop<=48 && o->lX-x<=lead
            && o->lX+o->hwWidth>x+PLATFORMER_GAME_BODY_OFFSET_X)return true;
        }
    }
    return false;
}

static void check_course(void)
{
    unsigned patterns[5]={0}, min_wins=10000;
    const int speeds[]={90,135,180};
    for(unsigned section=0;section<64;section++) {
        platformer_game_t course=empty_game();
        unsigned authored=section%4;
        for(unsigned n=0;n<c_chRouteObjectCounts[authored];n++) {
            const game_route_object_t *o=&c_tRoutes[authored][n];
            __game_add_object(&course,500+o->iX,o->chRise,o->chWidth,
                o->chType==PLATFORMER_GAME_PLATFORM?10:24,o->chType);
        }
        int base=course.tObjects[0].lX;
        unsigned cookies=0, platforms=0, high=0;
        for(unsigned j=0;j<PLATFORMER_GAME_OBJECT_COUNT;j++) {
            const platformer_game_object_t *o=&course.tObjects[j];
            if(!o->bActive)continue;
            if(o->chType==PLATFORMER_GAME_PLATFORM) {
                platforms++;
                assert(198-o->iTop>=36 && 198-o->iTop<=72);
                assert(o->hwWidth>=72 && o->hwWidth<=112);
                continue;
            }
            cookies++;
            assert(o->hwWidth==24 && o->chHeight==24);
            assert(o->iTop>=38 && o->iTop+24<=126); /* Clear HUD, and floor reach. */
            if(198-o->iTop>=148)high++;
            for(unsigned k=0;k<PLATFORMER_GAME_OBJECT_COUNT;k++) {
                const platformer_game_object_t *p=&course.tObjects[k];
                if(p->bActive && p->chType==PLATFORMER_GAME_PLATFORM)
                    assert(o->lX+24<=p->lX || o->lX>=p->lX+p->hwWidth
                        || o->iTop+24<=p->iTop || o->iTop>=p->iTop+p->chHeight);
            }
        }
        assert(high>=1 && cookies+platforms<=9);
        unsigned pattern=cookies==7?4:platforms==1?0:platforms==3?2:course.tObjects[0].hwWidth==88?1:3;
        patterns[pattern]++;
        if(pattern==4)continue; /* Dedicated held-glide route checks below. */
        for(unsigned v=0;v<sizeof(speeds)/sizeof(speeds[0]);v++) {
            unsigned wins=0, best=0;
            for(int lead=24;lead<=48;lead+=8)
            for(int reward=24;reward<=56;reward+=8)
            for(int edge=20;edge<=36;edge+=8) {
                platformer_game_t trial=course;
                trial.lNextSectionX=100000;trial.lXQ8=(base-80)*256;
                unsigned jumps=0;
                while(trial.lXQ8/256<base+360) {
                    bool press=route_press(&trial,lead,reward,edge);
                    if(press)jumps++;
                    advance(&trial,1,(int16_t)speeds[v],press);
                }
                if(trial.wCookies>best)best=trial.wCookies;
                if(trial.wCookies==cookies) { assert(jumps>=2);wins++; }
            }
            if(wins<min_wins)min_wins=wins;
            if(wins<3)fprintf(stderr,"route section=%u pattern=%u speed=%d best=%u/%u wins=%u\n",
                              section,pattern,speeds[v],best,cookies,wins);
            assert(wins>=3); /* Multiple complete routes at every tested speed. */
        }
        /* Removing the platforms makes each high reward unreachable from the
         * floor, even when sweeping 111 distinct ground-jump starting points. */
        for(unsigned j=0;j<PLATFORMER_GAME_OBJECT_COUNT;j++) {
            const platformer_game_object_t *o=&course.tObjects[j];
            if(!o->bActive || o->chType!=PLATFORMER_GAME_COOKIE || 198-o->iTop<148)continue;
            for(int lead=0;lead<=110;lead++) {
                platformer_game_t trial=course;
                trial.lNextSectionX=100000;trial.lXQ8=(o->lX-lead)*256;
                for(unsigned k=0;k<PLATFORMER_GAME_OBJECT_COUNT;k++)
                    if(trial.tObjects[k].chType==PLATFORMER_GAME_PLATFORM)trial.tObjects[k].bActive=false;
                advance(&trial,80,180,true);
                assert(trial.tObjects[j].bActive);
            }
        }
    }
    for(unsigned n=0;n<4;n++)assert(patterns[n]>0);
    printf("PASS: 64 seeded sections, all 4 layouts, full collection at 90/135/180 px/s, minimum %u control windows; high rewards require platforms\n",min_wins);

    platformer_game_t run, walk;
    platformer_game_init(&run,198,90,0);walk=run;
    unsigned rebases=0, max_active=0, max_visible=0;
    for(unsigned n=0;n<120000u;n++) {
        uint32_t count=run.wCookies;
        advance(&run,1,180,route_press(&run,40,40,28));
        advance(&walk,1,180,false);
        assert(run.wCookies>=count && walk.wCookies==0);
        unsigned active=0,visible=0;
        for(unsigned j=0;j<PLATFORMER_GAME_OBJECT_COUNT;j++) {
            const platformer_game_object_t *o=&walk.tObjects[j];
            if(!o->bActive)continue;
            active++;
            if(o->lX+o->hwWidth>walk.lXQ8/256-120 && o->lX<walk.lXQ8/256+200)visible++;
        }
        if(active>max_active)max_active=active;
        if(visible>max_visible)max_visible=visible;
        if(run.lXQ8/256>=12120) { platformer_game_rebase(&run,12000);platformer_game_rebase(&walk,12000);rebases++; }
        assert(run.lNextSectionX>run.lXQ8/256+GAME_LOOKAHEAD);
        assert(walk.lNextSectionX>walk.lXQ8/256+GAME_LOOKAHEAD);
    }
    printf("Long run sections=%lu cookies=%lu rebases=%u active=%u visible=%u\n",
        (unsigned long)run.wSectionIndex,(unsigned long)run.wCookies,rebases,max_active,max_visible);
    /* A glide section is 240 px longer than the original routes. */
    assert(rebases>=16 && run.wCookies>500 && run.wSectionIndex>300);
    assert(max_active<=16 && max_visible<=8);
    printf("PASS: 20-minute jump/walk comparison: %lu vs 0 cookies, %u rebases, pool peak %u/16, visible peak %u\n",
           (unsigned long)run.wCookies,rebases,max_active,max_visible);
}

int main(void)
{
    check_ballistics();
    check_platforms();
    check_buffer_and_coyote();
    check_collection_and_timing();
    check_course();
    printf("Game state: %u bytes; object slots: %u\n", (unsigned)sizeof(platformer_game_t),
           PLATFORMER_GAME_OBJECT_COUNT);
    return 0;
}
