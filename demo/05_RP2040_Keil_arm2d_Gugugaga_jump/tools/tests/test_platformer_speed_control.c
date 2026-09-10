#include <assert.h>
#include <stdio.h>
#include "../../application/platformer_speed_control.h"
static int32_t run(unsigned cadence, int angle, unsigned duration) {
    platformer_speed_control_t c;platformer_speed_control_init(&c);
    for(unsigned t=0;t<duration;) {
        unsigned dt=duration-t<cadence ? duration-t : cadence;
        platformer_speed_control_update(&c,(int16_t)angle,true,dt);t+=dt;
    }
    return c.speed_milli;
}
int main(void) {
    platformer_speed_control_t c;platformer_speed_control_init(&c);
    for(unsigned n=0;n<6000;n++) {
        int angle=(int)(n%81)-40;
        assert(platformer_speed_control_update(&c,(int16_t)angle,true,10)==90);
    }
    assert(run(10,200,1000)==180000);
    assert(run(24,200,1000)==180000 && run(41,200,1000)==180000);
    assert(run(10,120,1000)==135000 && run(41,120,1000)==135000);
    assert(run(24,-200,1000)==0);
    c.speed_milli=135000;
    for(unsigned n=0;n<500;n++)assert(platformer_speed_control_update(&c,0,true,10)==135);
    assert(platformer_speed_control_update(&c,200,false,100)==135);
    platformer_speed_control_update(&c,-32768,true,10000);
    assert(c.speed_milli==121000); /* large angle and frame stall capped */
    c.speed_milli=0;
    for(unsigned n=0;n<20;n++)assert(platformer_speed_control_update(&c,-200,true,100)==0);
    assert(platformer_speed_control_update(&c,0,true,100)==0);
    assert(platformer_speed_control_update(&c,200,true,100)==9);
    c.speed_milli=180000;
    assert(platformer_speed_control_update(&c,32767,true,100)==180);
    puts("PASS speed: 90pps cruise, neutral deadzone/hold, proportional acceleration/braking, cadence, stop/restart, sensor unavailable, caps");
}
