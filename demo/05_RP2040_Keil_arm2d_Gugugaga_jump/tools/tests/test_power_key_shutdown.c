/* Drive real sampling + game, including airborne and post-landing shutdown. */
#define main existing_key_test
#include "test_power_key_glide.c"
#undef main
#include "../../application/platformer_game.c"
int main(void)
{
    for(unsigned mode=0;mode<4;mode++) {
        s_state=POWER_KEY_START;s_press_pending=false;s_press_reported=false;
        s_continuous_low_ms=0;input_high=false;
        unsigned base=mode==3 ? UINT32_MAX-2000u : 0;
        power_key_service_poll(base);power_key_service_poll(base+1);
        input_high=true;
        for(unsigned t=2;t<=100;t++)power_key_service_poll(base+t);
        platformer_game_t g;platformer_game_init(&g,198,90,base+100);
        memset(g.tObjects,0,sizeof(g.tObjects));g.lNextSectionX=100000;
        /* High launch stays airborne past five seconds. */
        if(mode==1)g.lFootYQ8=-5000*256;
        unsigned cutoff=mode==2 ? 9014 : 5101, presses=0;
        bool saw_glide=false;
        for(unsigned t=101;t<=cutoff;t++) {
            input_high=(t>=600 && t<605) || (mode==2 && t>=4000 && t<4014);
            power_key_service_poll(base+t);
            if((t-100)%24==0) {
                bool p=power_key_service_consume_press();presses+=p;
                if(p)g.bJumpArmed=true;
                g.bGlideHeld=power_key_service_is_pressed();
                g.bGlideQualified=power_key_service_glide_ready();
                platformer_game_update(&g,base+t,0,p);
                saw_glide|=g.bGliding;
            }
            if(t<cutoff)assert(keep);
        }
        assert(!keep && !power_key_service_is_pressed());
        assert(!power_key_service_consume_press() && saw_glide);
        assert(presses==(mode==2 ? 2u : 1u));
        if(mode==1)assert(!g.bGrounded);else assert(g.bGrounded);
        input_high=true;power_key_service_poll(base+cutoff+100);
        assert(!keep); /* OFF remains latched when external power is present. */
    }
    puts("PASS: exact 5s shutdown after landing/in air, bounce tolerance, release resets timer, timestamp wrap, OFF latched");
}
