/* Audit what the polling diagnostic can and cannot measure. */
#define main original_power_key_tests
#include "test_power_key_glide.c"
#undef main

static void sample(unsigned ms)
{
    /* Actual released/high pulse is only 10 ms wide. */
    input_high=ms>=349 && ms<359;
    power_key_service_poll(ms);
}

int main(void)
{
    input_high=false;power_key_service_poll(0);power_key_service_poll(1);
    input_high=true;power_key_service_poll(10);power_key_service_poll(100);
    sample(101);sample(350);sample(600);
    uint16_t low,high;
    power_key_service_get_pulse_ms(&low,&high);
    assert(low==249 && high==250);
    puts("CONFIRMED limitation: actual high pulse 10 ms, sampled U=250 ms when polling has 250 ms gaps");
    /* A stable held level creates no new edges, even after a long delay.
     * Last-completed durations remain stale rather than counting live age. */
    sample(1000);sample(5000);
    power_key_service_get_pulse_ms(&low,&high);
    assert(low==249 && high==250);
    assert(power_key_service_is_raw_pressed());
    puts("CONFIRMED: U/L retain old intervals; long polling gaps alone do not toggle a stable input");
}
