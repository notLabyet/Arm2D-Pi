#define main old_key_test
#include "test_power_key_glide.c"
#undef main
static void sample_at(unsigned ms, bool high) {
    host_time_us=(uint64_t)ms*1000;
    sio_hw->gpio_in=high ? 1u<<9 : 0;
    iobank0_hw->io[9].status=high ? 1u<<17 : 0;
    power_key_audit_sample();
}
int main(void) {
    power_key_audit_t a;
    iobank0_hw->io[9].ctrl=5;padsbank0_hw->io[9]=0x48;
    sample_at(0,false);
    for(unsigned ms=1;ms<=3000;ms++)sample_at(ms,false);
    power_key_service_get_audit(&a);
    assert(a.edges==0 && a.age_ms==3000 && a.levels==0 && a.faults==0);
    for(unsigned ms=3001;ms<=5000;ms++)sample_at(ms,((ms-3000)/250)%2);
    power_key_service_get_audit(&a);
    assert(a.edges==8 && a.age_ms==0 && a.faults==0);
    assert(s_key_trace_count==9 && s_key_trace[8].time_us==5000000);
    sample_at(5107,true);power_key_service_get_audit(&a);
    assert(a.levels==3 && a.edges==9);
    sample_at(8107,true);power_key_service_get_audit(&a);
    assert(a.edges==9 && a.age_ms==3000); /* old durations cannot fake new edges */
    iobank0_hw->io[9].ctrl=7|(1u<<16);sio_hw->gpio_oe=1u<<9;
    padsbank0_hw->io[9]=0;sample_at(8108,true);
    power_key_service_get_audit(&a);assert(a.faults==15);
    iobank0_hw->io[9].ctrl=5;sio_hw->gpio_oe=0;padsbank0_hw->io[9]=0x48;
    sample_at(8109,true);power_key_service_get_audit(&a);assert(a.faults==15);
    sample_at(4294967,false);sample_at(4294977,false);
    power_key_service_get_audit(&a);assert(a.age_ms==10);
    /* A 1Hz snapshot can always show high while each window has four edges. */
    sample_at(4295000,true);power_key_service_get_audit(&a);
    for(unsigned t=1;t<=3000;t++) {
        sample_at(4295000+t,(t/250)%2==0);
        if(t%1000==0) {
            power_key_service_get_audit(&a);
            assert(a.levels==3 && a.sio_changes==4 && a.pad_changes==4);
            assert(a.sio_seen==3 && a.pad_seen==3 && a.window_ms==1000);
        }
    }
    /* Distinguish SIO changes from a stable high pad, regardless of snapshot phase. */
    for(unsigned t=1;t<=1000;t++) {
        host_time_us=(uint64_t)(4298000+t)*1000;
        sio_hw->gpio_in=(t/250)%2==0 ? 1u<<9 : 0;
        iobank0_hw->io[9].status=1u<<17;
        power_key_audit_sample();
    }
    power_key_service_get_audit(&a);
    assert(a.sio_changes==4 && a.pad_changes==0 && a.pad_seen==2);
    puts("PASS window: 1Hz alias reproduced, both paths distinguished, window reset correct");
    puts("PASS audit: stable input, 250ms edges/timestamps, live age, config faults latched, timer wrap");
}
