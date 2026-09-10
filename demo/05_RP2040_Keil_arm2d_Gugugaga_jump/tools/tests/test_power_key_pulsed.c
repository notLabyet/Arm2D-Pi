#define main existing_key_test
#include "test_power_key_glide.c"
#undef main
#include "../../application/platformer_game.c"
int main(void) {
    power_key_service_poll(0);power_key_service_poll(1);
    input_high=true;power_key_service_poll(10);power_key_service_poll(100);
    /* User-reported 107 ms tap must release at 30 ms, with no glide. */
    input_high=false;
    for(unsigned t=101;t<=207;t++)power_key_service_poll(t);
    assert(power_key_service_consume_press());
    assert(!power_key_service_glide_ready());
    input_high=true;
    for(unsigned t=208;t<=237;t++)power_key_service_poll(t);
    assert(power_key_service_is_pressed());
    power_key_service_poll(238);
    assert(!power_key_service_is_pressed());
    platformer_game_t g;platformer_game_init(&g,198,90,300);
    memset(g.tObjects,0,sizeof(g.tObjects));g.lNextSectionX=100000;
    for(unsigned t=239;t<=300;t++)power_key_service_poll(t);
    s_max_sample_gap_ms=0;
    unsigned presses=0,jumps=0,glides=0;
    for(unsigned ms=301;ms<=5300;ms++) {
        input_high=((ms-301)%500)>=250;
        power_key_service_poll(ms);
        if((ms-300)%24==0) {
            bool before=g.bGrounded,p=power_key_service_consume_press();
            presses+=p;g.bGlideHeld=power_key_service_is_pressed();
            g.bGlideQualified=power_key_service_glide_ready();
            platformer_game_update(&g,ms,0,p);
            power_key_service_set_airborne(!g.bGrounded);
            jumps+=before&&!g.bGrounded;glides+=g.bGliding;
        }
    }
    assert(presses==1 && jumps==1 && glides>40 && g.bGrounded);
    printf("Synthetic 250ms low/250ms high, 1ms sampling: presses=%u jumps=%u glide_frames=%u Q=%u\n",presses,jumps,glides,power_key_service_max_sample_gap_ms());
}
