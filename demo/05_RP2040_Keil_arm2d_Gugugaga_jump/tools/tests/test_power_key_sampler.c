/* Drive the registered production timer callback while the game is idle. */
#define main original_key_tests
#include "test_power_key_glide.c"
#undef main
#include "../../application/platformer_game.c"

static void tick_to(unsigned end_ms, bool high)
{
    input_high=high;
    while(host_time_us/1000<end_ms) {
        host_time_us+=1000;
        assert(host_alarm_target<=host_time_us);
        host_alarm_callback(0);
    }
}
static void game_frame(platformer_game_t *g, unsigned ms, bool high)
{
    tick_to(ms,high);
    g->bGlideHeld=power_key_service_is_pressed();
    g->bGlideQualified=power_key_service_glide_ready();
    platformer_game_update(g,ms,0,power_key_service_consume_press());
    power_key_service_set_airborne(!g->bGrounded);
}
int main(void)
{
    input_high=false;
    assert(power_key_service_start_sampling() && host_alarm_callback);
    tick_to(1,false);tick_to(100,true);
    platformer_game_t g;
    platformer_game_init(&g,198,90,100);
    memset(g.tObjects,0,sizeof(g.tObjects));g.lNextSectionX=100000;
    /* A 12 ms tap occurs entirely between game frames. */
    tick_to(112,false);tick_to(220,true);
    game_frame(&g,220,true);
    assert(!g.bGrounded && !g.bGlideQualified);
    for(unsigned ms=230;ms<=1000;ms+=10) {
        game_frame(&g,ms,true);
        assert(!g.bGliding);
    }
    assert(g.bGrounded);
    /* A fresh, continuous long hold must qualify after the apex. */
    for(unsigned ms=1010;ms<=1450;ms+=10) {
        game_frame(&g,ms,false);
        if(ms<=1290)assert(!g.bGliding);
    }
    assert(g.bGliding && g.lVelocityYQ8<=40*256);
    game_frame(&g,1460,true);
    assert(g.bGliding); /* bridge the measured 250 ms high phase */
    game_frame(&g,1731,true);
    assert(!g.bGliding); /* confirmed release ends glide */
    tick_to(1800,true);
    assert(power_key_service_max_sample_gap_ms()==1);
    /* A delayed IRQ preserves an established hold without crediting the gap. */
    tick_to(2150,false);
    assert(power_key_service_glide_ready());
    host_time_us+=250000;host_alarm_callback(0);
    assert(power_key_service_glide_ready());
    assert(power_key_service_max_sample_gap_ms()==250);
    tick_to(2681,true);
    assert(!power_key_service_is_pressed());
    tick_to(2690,false);
    assert(power_key_service_consume_press());
    assert(!power_key_service_glide_ready());
    host_time_us+=250000;host_alarm_callback(0);
    assert(!power_key_service_glide_ready()); /* gap cannot qualify a fresh press */
    for(unsigned i=0;i<230;i++) {
        host_time_us+=6000;host_alarm_callback(0);
    }
    assert(power_key_service_glide_ready()); /* recurring >5ms gaps cannot starve hold */
    puts("PASS: timer captures 12ms tap during game stall, no short-hop glide, 300ms game hold, 40px/s cap, 280ms held release, short-tap release stays 30ms, delayed-IRQ accumulation");
}
