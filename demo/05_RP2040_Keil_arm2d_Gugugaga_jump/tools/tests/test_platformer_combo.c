#define main existing_game_tests
#include "test_platformer_game.c"
#undef main

static void cookie(platformer_game_t *g, unsigned slot)
{
    g->tObjects[slot]=(platformer_game_object_t){
        .lX=g->lXQ8/256+12,.iTop=160,.hwWidth=24,.chHeight=24,
        .chType=PLATFORMER_GAME_COOKIE,.bActive=true};
}

int main(void)
{
    platformer_game_t g=empty_game();
    cookie(&g,0);advance(&g,1,0,false);
    assert(g.wCookies==1 && g.hwCombo==1 && g.hwComboRemainingMs==2000);
    advance(&g,198,0,false);
    cookie(&g,0);advance(&g,1,0,false);
    assert(g.wCookies==2 && g.hwCombo==2 && g.hwComboRemainingMs==2000);
    /* Two overlapping cookies in one simulation step both contribute. */
    cookie(&g,0);cookie(&g,1);advance(&g,1,0,false);
    assert(g.wCookies==4 && g.hwCombo==4);
    advance(&g,199,0,false);
    assert(g.hwCombo==4 && g.hwComboRemainingMs==10);
    /* At the timeout boundary this is a fresh chain, total score persists. */
    cookie(&g,0);advance(&g,1,0,false);
    assert(g.hwCombo==1 && g.wCookies==5);
    advance(&g,200,0,false);
    assert(!g.hwCombo && !g.hwComboRemainingMs && g.wCookies==5);
    /* Bounded display count and saturated total still acknowledge collection. */
    g.wCookies=999999;g.hwCombo=999;g.hwComboRemainingMs=100;
    cookie(&g,0);advance(&g,1,0,false);
    assert(g.wCookies==999999 && g.hwCombo==999 && g.hwComboRemainingMs==2000);
    platformer_game_init(&g,198,90,0);
    assert(!g.hwCombo && !g.hwComboRemainingMs);
    puts("PASS combo: chain, same-step pickups, timeout boundary, reset, saturation");
}
