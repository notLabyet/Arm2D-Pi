"""Host checks for the production LCD transfer path and encoded PIO program.

Hardware boundaries are substitutes; this does not measure LCD scanout or FPS.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "_compile_check" / "display_test"
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / "platform/st7789_simple.c").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"void " + name + r"\([^)]*\)\s*\{", source)
    assert match, name
    start = match.start()
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# The actual driver must drain after DMA completion, before CS or PFB release.
idle = function("st7789_pio_stream_wait_idle")
assert "s_pio->fdebug = wStall" in idle
assert idle.index("while (!(s_pio->fdebug & wStall))") < idle.index("pio_sm_set_enabled")
irq = function("st7789_pio_stream_dma_irq")
assert irq.index("st7789_pio_stream_wait_idle") < irq.index("cs_deselect") < irq.index("st7789_insert_async_flush_cpl_evt_handler")

stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define __IRQ_SAFE
#define ST7789_PIN_CS 10
#define DMA_IRQ_0 0
typedef struct { unsigned threshold; } pio_sm_config;
typedef struct { unsigned size; } dma_channel_config;
static struct { uint32_t txf[4]; } pio;
static void *s_pio_unused;
#define s_pio (&pio)
static unsigned s_sm, dma_chan, s_uStreamOffset = 5;
static pio_sm_config s_tStreamConfig;
static dma_channel_config s_tByteDMAConfig={1}, s_tWordDMAConfig={4};
static unsigned dma_size, dma_count, dma_starts, released, pending, busy, enabled;
static bool irq_enabled, irq_pending;
static const uint8_t *dma_src;
static uint8_t queued[40000], output[40000];
static size_t qlen, olen;
static void tight_loop_contents(void) { assert(!busy); }
static bool dma_channel_is_busy(unsigned ch) { (void)ch; return busy; }
static void pio_sm_set_enabled(void *p,unsigned sm,bool value) {
    (void)p;(void)sm;enabled=value;
}
static void sm_config_set_out_shift(pio_sm_config *c,bool right,bool automatic,unsigned threshold) {
    assert(right && !automatic); c->threshold=threshold;
}
static void pio_sm_set_config(void *p,unsigned sm,const pio_sm_config *c) {
    (void)p;(void)sm;(void)c;assert(!enabled && !pending && !qlen);
}
static void pio_sm_restart(void *p,unsigned sm) { (void)p;(void)sm;assert(!enabled); }
static unsigned pio_encode_jmp(unsigned pc) { return pc; }
static void pio_sm_exec(void *p,unsigned sm,unsigned pc) { (void)p;(void)sm;assert(pc==5); }
static void pio_sm_put_blocking(void *p,unsigned sm,uint32_t value) {
    (void)p;(void)sm;assert(!enabled && s_tStreamConfig.threshold==8);
    assert(qlen<8);queued[qlen++]=(uint8_t)value;
}
static void dma_channel_set_irq0_enabled(unsigned ch,bool value) { (void)ch;irq_enabled=value; }
static void dma_channel_acknowledge_irq0(unsigned ch) { (void)ch;irq_pending=false; }
static bool dma_channel_get_irq0_status(unsigned ch) { (void)ch;return irq_pending && irq_enabled; }
static void dma_channel_configure(unsigned ch,const dma_channel_config *c,void *dst,
                                  const void *src,size_t count,bool start) {
    (void)ch;(void)dst;assert(!start);dma_size=c->size;dma_src=src;dma_count=count;
    assert(s_tStreamConfig.threshold==dma_size*8);
    if(dma_size==4)assert(((uintptr_t)src & 3)==0);
}
static void dma_channel_start(unsigned ch) {
    (void)ch;assert(enabled);dma_starts++;busy=1;pending=1;
    qlen=dma_size*dma_count;assert(qlen<=sizeof(queued));memcpy(queued,dma_src,qlen);
}
static void dma_channel_wait_for_finish_blocking(unsigned ch) { (void)ch;busy=0;irq_pending=true; }
static void st7789_pio_stream_wait_idle(void) {
    assert(!busy && enabled);memcpy(output+olen,queued,qlen);olen+=qlen;
    pending=0;qlen=0;enabled=0;
}
static void gpio_put(unsigned pin,unsigned high) { assert(pin==10 && high && !pending && !enabled); }
static void st7789_insert_async_flush_cpl_evt_handler(void) { assert(!pending && !enabled);released++; }
'''
functions = "\n".join(function(n) for n in (
    "cs_deselect", "st7789_pio_stream_prepare", "st7789_pio_stream_send",
    "st7789_pio_stream_send_async", "st7789_pio_stream_dma_irq"))
cases = r'''
int main(void) {
    (void)s_pio_unused;
    static uint32_t storage[9601];
    uint8_t *bytes=(uint8_t *)storage;
    for(unsigned n=0;n<sizeof(storage);n++)bytes[n]=(uint8_t)(n*37+11);
    for(unsigned len=0;len<=8;len++) {
        olen=0;unsigned starts=dma_starts;
        st7789_pio_stream_send(bytes,len);
        assert(olen==len && memcmp(output,bytes,len)==0 && dma_starts==starts);
    }
    const unsigned lengths[]={9,10,12,62,64,640,38400};
    for(unsigned offset=0;offset<4;offset++)for(unsigned n=0;n<7;n++) {
        unsigned len=lengths[n];olen=0;
        st7789_pio_stream_send(bytes+offset,len);
        assert(olen==len && memcmp(output,bytes+offset,len)==0);
        assert(dma_size==((offset==0 && len%4==0)?4u:1u));
        olen=0;unsigned old_released=released;
        st7789_pio_stream_send_async(bytes+offset,len);
        assert(pending && released==old_released && olen==0);
        dma_channel_wait_for_finish_blocking(dma_chan);
        st7789_pio_stream_dma_irq();
        assert(released==old_released+1 && !pending && !irq_enabled);
        assert(olen==len && memcmp(output,bytes+offset,len)==0);
        st7789_pio_stream_dma_irq();assert(released==old_released+1);
    }
    puts("PASS: production transfers, CPU short commands, aligned word DMA, byte fallback, tail drain before one completion callback");
}
'''
cfile = OUT / "driver.c"
cfile.write_text(stub + functions + cases, encoding="utf-8")
exe = OUT / "driver.exe"
subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(cfile), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)

# Decode the pioasm output, including side-set during a stalled instruction.
header = (ROOT / "platform/st77xx_parallel_stream.pio.h").read_text()
instructions = [int(x,16) for x in re.findall(r"^\s*(0x[0-9a-f]+),", header, re.M)]
assert instructions == [0x98e0, 0x7008, 0xb942]
for bits in (8,32):
    for count in (1,2,8,16,9600):
        payload = bytes((n*37+11)&255 for n in range(count*(bits//8)))
        fifo = [int.from_bytes(payload[n:n+bits//8], "little") for n in range(0,len(payload),bits//8)]
        pos=pc=cycles=0
        shifted=bits
        osr=0
        wr=1
        latched=[]
        rises=[]
        while True:
            ins=instructions[pc]
            side=(ins>>11)&1
            if side and not wr:
                latched.append(data)
                rises.append(cycles)
            wr=side
            if pc==0:
                if shifted>=bits:
                    if pos==len(fifo):
                        assert wr==1
                        break
                    osr=fifo[pos];pos+=1;shifted=0
            elif pc==1:
                data=osr&255;osr>>=8;shifted+=8
            cycles+=1+((ins>>8)&7)
            pc=(pc+1)%3
        assert bytes(latched)==payload
        assert all(b-a==4 for a,b in zip(rises,rises[1:]))
for hz in (125000000,250000000):
    div256=max(256,(hz*256+59999999)//60000000)
    cycle_ns=div256/hz/256*1e9
    assert cycle_ns>=15 and 4*cycle_ns>=66
print("PASS: encoded PIO byte order, complete last word, WR-high empty stall, >=66 ns write cycle")

# Exercise the actual scheduler fragment, including the 32-bit clock wrap.
main = (ROOT / "main.c").read_text(encoding="utf-8")
sched = re.search(r"        uint32_t wNowUs = time_us_32\(\);.*?\n        }\n    }\n}", main, re.S).group()
sched = sched.rsplit("\n    }\n}",1)[0]
scheduler = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t now, finish, duration, starts[1000];
static unsigned count;
static bool running;
#define arm_fsm_rt_cpl 0
typedef int arm_fsm_rt_t;
static void platform_display_profile_frame_begin(uint32_t t) { (void)t; }
static void platform_display_profile_task_time(uint32_t t) { (void)t; }
static void platform_display_profile_frame_end(uint32_t t) { (void)t; }
static uint32_t time_us_32(void) { return now; }
static int __disp_adapter0_task(void) {
    if(!running) { running=true; finish=now+duration;starts[count++]=now; }
    if((int32_t)(now-finish)>=0) { running=false;return 0; }
    return 1;
}
int main(void) {
    const uint32_t costs[]={2000,20000};
    for(unsigned c=0;c<2;c++) {
        bool bDisplayBusy=false;
        uint32_t wNextFrameUs=UINT32_MAX-100000;
        now=wNextFrameUs;duration=costs[c];count=0;running=false;
        for(unsigned step=0;step<1000000;step++) {
'''+sched+r'''
            now++;
        }
        assert(count>40 && count<=60);
        for(unsigned n=1;n<count;n++)assert(starts[n]-starts[n-1]==(c?20001u:16667u));
    }
    puts("PASS: production frame scheduler, 60 Hz cap, slow-frame no extra wait, timer wrap");
}
'''
cfile.write_text(scheduler, encoding="utf-8")
subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(cfile), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
