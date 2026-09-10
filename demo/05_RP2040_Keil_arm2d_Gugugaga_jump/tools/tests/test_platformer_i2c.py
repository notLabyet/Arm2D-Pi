"""Host fault-injection checks for the actual QMI transport/init functions."""
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[2]
work=root/'_compile_check/platformer_i2c_host'
work.mkdir(parents=True,exist_ok=True)
driver=(root/'deivers/drv_QMI8658.c').read_text(encoding='utf-8')
wrapper=(root/'application/qmi8658c_task.c').read_text(encoding='utf-8')
header=(root/'deivers/drv_QMI8658.h').read_text(encoding='utf-8')

def function(source,name):
    m=re.search(r'^[^;{}\n]*\b'+name+r'\([^;{}]*\)\s*\{',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]+'\n'

code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define I2C_PORT 0
#define I2C_SDA 0
#define I2C_SCL 1
#define GPIO_FUNC_I2C 3
#define bi_decl(...) ((void)0)
#define A_XYZ 0x35
static unsigned baud, write_timeout, read_timeout, init_calls, transport_calls;
static int write_result=999, read_result=999;
static uint8_t init_first=1, init_second=1;
static unsigned i2c_set_baudrate(unsigned p,unsigned b) { (void)p;baud=b;return b; }
static void i2c_init(unsigned p,unsigned b) { (void)i2c_set_baudrate(p,b); }
static int i2c_write_timeout_us(unsigned p,uint8_t a,const uint8_t *v,unsigned n,int repeat,unsigned timeout) {
    (void)p;(void)a;(void)v;(void)repeat;write_timeout=timeout;transport_calls++;
    return write_result==999?(int)n:write_result;
}
static int i2c_read_timeout_us(unsigned p,uint8_t a,uint8_t *v,unsigned n,int repeat,unsigned timeout) {
    (void)p;(void)a;(void)repeat;read_timeout=timeout;transport_calls++;
    for(unsigned i=0;i<n;i++)v[i]=(uint8_t)(i+1);
    return read_result==999?(int)n:read_result;
}
static void gpio_set_function(unsigned p,unsigned f) { (void)p;(void)f; }
static void gpio_pull_up(unsigned p) { (void)p; }
static unsigned get_absolute_time(void) { return 0; }
static unsigned to_ms_since_boot(unsigned t) { return t; }
static uint8_t QMI8658A_Init(void) { return init_calls++==0?init_first:init_second; }
static uint8_t QMI8658A_GetActiveAddress(void) { return 0x6B; }
static uint8_t s_chQMI8658Address=0x6B;
'''
for name in ['QMI8658_I2C_BAUD_HZ','QMI8658_I2C_FALLBACK_BAUD_HZ']:
    code+=re.search(r'^#define '+name+r'\s+\d+u',header,re.M)[0]+'\n'
for name in ['iic0_read_bytes','iic0_write_bytes','i2creads','QMI8658A_ReadData']:
    code+=function(driver,name)
code+=function(wrapper,'qmi8658c_init')
code+=r'''
int main(void) {
    uint8_t bytes[16]={0};int16_t sample[6]={0};
    assert(qmi8658c_init()==1 && baud==100000u && init_calls==1);
    init_calls=0;init_first=0;
    assert(qmi8658c_init()==1 && baud==40000u && init_calls==2);
    init_calls=0;init_second=0;
    assert(qmi8658c_init()==0 && init_calls==2);
    baud=100000u;
    assert(QMI8658A_ReadData(sample)==1 && sample[0]==0x0201 && sample[5]==0x0C0B);
    assert(baud==100000u && read_timeout>=3375u && read_timeout<=5000u);
    unsigned calls=transport_calls;
    assert(QMI8658A_ReadData(NULL)==0 && transport_calls==calls);
    read_result=-1;
    memset(sample,0x55,sizeof(sample));
    assert(QMI8658A_ReadData(sample)==0 && baud==40000u && sample[0]==0x5555);
    read_result=11;
    assert(iic0_read_bytes(0x6B,0x35,bytes,12)==0); /* Reject partial reads. */
    read_result=999;write_result=-1;calls=transport_calls;
    assert(iic0_read_bytes(0x6B,0x35,bytes,12)==0 && transport_calls==calls+1);
    assert(write_timeout==1500u);
    write_result=0;
    assert(iic0_read_bytes(0x6B,0x35,bytes,12)==0);
    write_result=999;
    assert(iic0_write_bytes(0x51,0,bytes,7)==1);
    calls=transport_calls;
    assert(iic0_write_bytes(0x51,0,bytes,16)==0 && transport_calls==calls);
    write_result=3;
    assert(iic0_write_bytes(0x51,0,bytes,7)==0);
    puts("PASS I2C: 100 kHz init, 40 kHz init/read fallback, bounded timeouts, partial/failed transfers, RTC-size writes");
}
'''
(work/'checks.c').write_text(code)
exe=work/'checks.exe'
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(work/'checks.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
