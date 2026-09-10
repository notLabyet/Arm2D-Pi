/* Compile with -Itools/tests/stubs. Drive the production GPIO service and game
 * together, including short high-level gaps during one physical hold. */
#define main power_key_tests
#include "test_power_key_glide.c"
#undef main
#include "../../application/platformer_game.c"

static void run_hold(unsigned frame_ms, unsigned gap_ms)
{
    unsigned period_ms=gap_ms>=80 ? 500 : 150;
    s_state=0;s_changed_ms=0;s_raw_pressed=false;s_pressed=false;
    s_press_pending=false;s_press_reported=false;
    s_airborne=false;s_hold_used_in_air=false;
    input_high=false;
    power_key_service_poll(0);power_key_service_poll(1);
    input_high=true;power_key_service_poll(10);power_key_service_poll(100);
    platformer_game_t game;
    platformer_game_init(&game,198,90,100);
    memset(game.tObjects,0,sizeof(game.tObjects));game.lNextSectionX=100000;
    unsigned presses=0,glide_frames=0;
    for(unsigned ms=101;ms<=3100;ms++) {
        unsigned age=ms-101;
        input_high=gap_ms && age>=period_ms && age%period_ms<gap_ms;
        power_key_service_poll(ms);
        if ((ms-100)%frame_ms==0) {
            bool press=power_key_service_consume_press();
            presses+=press;
            game.bGlideHeld=power_key_service_is_pressed();
            game.bGlideQualified=power_key_service_glide_ready();
            platformer_game_update(&game,ms,0,press);
            power_key_service_set_airborne(!game.bGrounded);
            glide_frames+=game.bGliding;
        }
        assert(keep);
    }
    printf("frame=%u gap=%u presses=%u glide_frames=%u\n",frame_ms,gap_ms,presses,glide_frames);
    fflush(stdout);
    assert(presses==1 && game.bGrounded);
    if (!gap_ms)assert(glide_frames>=1000/frame_ms);
    if (gap_ms)assert(glide_frames>0);
    if (gap_ms) {
        uint16_t low_ms,high_ms;
        power_key_service_get_pulse_ms(&low_ms,&high_ms);
        assert(high_ms==gap_ms && low_ms==period_ms-gap_ms);
    }
    input_high=true;
    for(unsigned ms=3101;ms<=3500;ms++)power_key_service_poll(ms);
    assert(!power_key_service_is_pressed());
    input_high=false;power_key_service_poll(3501);power_key_service_poll(3521);
    assert(power_key_service_consume_press());
}
int main(void)
{
    unsigned cadences[]={10,24,41};
    for(unsigned n=0;n<3;n++) {
        run_hold(cadences[n],0);
        run_hold(cadences[n],5);
        run_hold(cadences[n],20);
        run_hold(cadences[n],250);
    }
    puts("PASS: stable/noisy hold yields one jump, apex glide, landing without repeat, release/repress");
}
