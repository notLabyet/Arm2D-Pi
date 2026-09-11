/* Stable hardware regression; the old 250 ms pulse bridging is gone. */
#define main existing_key_test
#include "test_power_key_glide.c"
#undef main
#include "../../application/platformer_game.c"
static unsigned now, presses, jumps, glides;
static void sample(unsigned end, bool high)
{
    input_high=high;
    while(now<end)power_key_service_poll(++now);
}
static void frame(platformer_game_t *g)
{
    bool before=g->bGrounded, p=power_key_service_consume_press();
    presses+=p;
    g->bGlideHeld=power_key_service_is_pressed();
    g->bGlideQualified=power_key_service_glide_ready();
    /* Same debounced-edge handoff as the production scene. */
    if(p)g->bJumpArmed=true;
    platformer_game_update(g,now,0,p);
    power_key_service_set_airborne(!g->bGrounded);
    jumps+=before&&!g->bGrounded;
    glides+=g->bGliding;
}
int main(void)
{
    power_key_service_poll(0);sample(1,false);sample(100,true);
    /* Five ms of contact noise cannot trigger a press. */
    sample(105,false);sample(130,true);
    assert(!power_key_service_consume_press());
    platformer_game_t g;platformer_game_init(&g,198,90,130);
    memset(g.tObjects,0,sizeof(g.tObjects));g.lNextSectionX=100000;
    /* An 8 ms tap is latched between 24 ms frames. */
    sample(138,false);sample(154,true);frame(&g);
    assert(presses==1 && jumps==1 && !g.bGlideHeld);
    while(now<850){sample(now+24,true);frame(&g);assert(!g.bGliding);}
    assert(g.bGrounded);
    /* Continuous hold through apex and landing creates one jump only. */
    unsigned start=now;
    while(now<start+4000){sample(now+24,false);frame(&g);}
    assert(presses==2 && jumps==2 && glides>20 && g.bGrounded && keep);
    assert(!g.bJumpArmed);
    /* Release and fresh press BOTH fit between adjacent frames. */
    sample(now+14,true);
    assert(!power_key_service_is_pressed() && !power_key_service_glide_ready());
    sample(now+10,false);frame(&g);
    assert(presses==3 && jumps==3 && !g.bGrounded);
    assert(g.hwGlideHoldMs<=30 && !g.bGliding);
    start=now;
    while(now<start+360){sample(now+24,false);frame(&g);}
    assert(g.bGliding);
    sample(now+13,true);frame(&g);
    assert(!g.bGliding && !g.bGlideHeld);
    /* Separate 250 ms presses must no longer merge into a synthetic hold. */
    unsigned before=presses;
    for(unsigned n=0;n<3;n++){
        for(unsigned i=0;i<10;i++){sample(now+25,true);frame(&g);}
        for(unsigned i=0;i<10;i++){sample(now+25,false);frame(&g);}
    }
    assert(presses==before+3);
    puts("PASS: noise rejected, 8ms tap, single held jump, 24ms release/repress, prompt glide exit, no 250ms bridging");
}
