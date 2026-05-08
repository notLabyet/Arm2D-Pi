/****************************************************************************
*  Copyright 2025 Gorgon Meducer (Email:embedded_zhuoran@hotmail.com)       *
*                                                                           *
*  Licensed under the Apache License, Version 2.0 (the "License");          *
*  you may not use this file except in compliance with the License.         *
*  You may obtain a copy of the License at                                  *
*                                                                           *
*     http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                           *
*  Unless required by applicable law or agreed to in writing, software      *
*  distributed under the License is distributed on an "AS IS" BASIS,        *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
*  See the License for the specific language governing permissions and      *
*  limitations under the License.                                           *
*                                                                           *
****************************************************************************/

/*============================ INCLUDES ======================================*/
#include "st7789_simple.h"

#include <stddef.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "st77xx_parallel_byte.pio.h"
#include "st77xx_parallel_stream.pio.h"

#include "platform.h"


/*============================ MACROS ========================================*/


#ifndef ST7789_PIN_D0
#   define ST7789_PIN_D0   14
#endif
#ifndef ST7789_PIN_WR
#   define ST7789_PIN_WR   12
#endif
#ifndef ST7789_PIN_DC
#   define ST7789_PIN_DC   11
#endif
#ifndef ST7789_PIN_CS
#   define ST7789_PIN_CS   10
#endif
#ifndef ST7789_PIN_RD
#   define ST7789_PIN_RD   13
#endif
#ifndef ST7789_PIN_RST
#   define ST7789_PIN_RST  -1
#endif
#ifndef ST7789_PIN_BL
#   define ST7789_PIN_BL   25 
#endif

#ifndef ST7789_PIO_INSTANCE
#   define ST7789_PIO_INSTANCE  1  // 0 => pio0, 1 => pio1
#endif

#ifndef ST7789_PIO_CLKDIV
#   define ST7789_PIO_CLKDIV    3
#endif

#define DATA_PIN_MASK   (0xFFu << ST7789_PIN_D0)

#if ST7789_PIO_INSTANCE == 0
#   define ST7789_PIO   pio0
#else
#   define ST7789_PIO   pio1
#endif

/*============================ MACROFIED FUNCTIONS ===========================*/


#define write_cmd_with_data(__CMD, ...)                                         \
            do {                                                                \
                uint8_t chData[] = {                                            \
                    __VA_ARGS__,                                                \
                };                                                              \
                __write_cmd_with_data(__CMD, chData, sizeof(chData));           \
            } while(0)

#define write_cmd_with_obj(__CMD, __OBJ)                                        \
        do {                                                                    \
            __write_cmd_with_data(__CMD, (uint8_t *)&(__OBJ), sizeof(__OBJ));   \
        } while(0)


/*============================ TYPES =========================================*/
enum {
    SWRESET   = 0x01,
    TEOFF     = 0x34,
    TEON      = 0x35,
    MADCTL    = 0x36,
    COLMOD    = 0x3A,
    RAMCTRL   = 0xB0,
    GCTRL     = 0xB7,
    VCOMS     = 0xBB,
    LCMCTRL   = 0xC0,
    VDVVRHEN  = 0xC2,
    VRHS      = 0xC3,
    VDVS      = 0xC4,
    FRCTRL2   = 0xC6,
    PWCTRL1   = 0xD0,
    PORCTRL   = 0xB2,
    GMCTRP1   = 0xE0,
    GMCTRN1   = 0xE1,
    INVOFF    = 0x20,
    SLPOUT    = 0x11,
    DISPON    = 0x29,
    GAMSET    = 0x26,
    DISPOFF   = 0x28,
    RAMWR     = 0x2C,
    INVON     = 0x21,
    CASET     = 0x2A,
    RASET     = 0x2B,
    PWMFRSEL  = 0xCC
};

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/
static uint32_t s_sm;
static PIO s_pio;
static uint32_t dma_chan = 0xff;

/*============================ PROTOTYPES ====================================*/
/*============================ IMPLEMENTATION ================================*/
__STATIC_INLINE 
void dc_command(void) 
{ 
    gpio_put(ST7789_PIN_DC, 0); 
}

__STATIC_INLINE 
void dc_data(void)    
{ 
    gpio_put(ST7789_PIN_DC, 1); 
}

__STATIC_INLINE 
void cs_select(void)  
{ 
    if (ST7789_PIN_CS >= 0) { 
        gpio_put(ST7789_PIN_CS, 0); 
    } 
}

__STATIC_INLINE 
void cs_deselect(void)
{ 
    while(!pio_sm_is_tx_fifo_empty(ST7789_PIO, s_sm)) { 
        tight_loop_contents();
    };
    /* wait extra 4 + 1 cycles for the final data */
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    pio_sm_set_enabled(s_pio, s_sm, false);

    if (ST7789_PIN_CS >= 0) {
        gpio_put(ST7789_PIN_CS, 1);
    }
}

__STATIC_INLINE 
void rst_assert(void) 
{ 
    if (ST7789_PIN_RST >= 0) { 
        gpio_put(ST7789_PIN_RST, 0); 
    } 
}

__STATIC_INLINE 
void rst_deassert(void)
{   
    if (ST7789_PIN_RST >= 0) {
        gpio_put(ST7789_PIN_RST, 1);
    }
}

__STATIC_INLINE 
void bl_on(void) 
{   
    if (ST7789_PIN_BL  >= 0) {
        gpio_put(ST7789_PIN_BL, 1); 
    }
}

void irq_clear_pending(uint num) {
    check_irq_param(num);
    *((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET)) = 1u << num;
}



__WEAK 
__attribute__((noinline))
void st7789_insert_async_flush_cpl_evt_handler(void)
{
}

static
void st7789_pio_stream_dma_irq(void)
{
    if (dma_channel_get_irq0_status(dma_chan)) {
        
        dma_channel_acknowledge_irq0(dma_chan);
        dma_channel_set_irq0_enabled(dma_chan, false);
        cs_deselect();
        

        st7789_insert_async_flush_cpl_evt_handler();
    }
}

static
void st7789_pio_stream_init(void)
{
    s_sm  = pio_claim_unused_sm(ST7789_PIO, true);

    uint offset = pio_add_program(s_pio, &st77xx_parallel_stream_auto_program);

    pio_sm_config c = st77xx_parallel_stream_auto_program_get_default_config(offset);
    sm_config_set_out_pins      (&c, ST7789_PIN_D0, 8);
    sm_config_set_sideset_pins  (&c, ST7789_PIN_WR);
    sm_config_set_out_shift     (&c, false, true, 8);  
    sm_config_set_fifo_join     (&c, PIO_FIFO_JOIN_TX); 

    pio_gpio_init(s_pio, ST7789_PIN_WR);
    for (int i = 0; i < 8; ++i) {
        pio_gpio_init(s_pio, ST7789_PIN_D0 + i);
    }

    pio_sm_set_consecutive_pindirs( ST7789_PIO, 
                                    s_sm, 
                                    ST7789_PIN_D0, 
                                    8, 
                                    true);
    pio_sm_set_consecutive_pindirs( ST7789_PIO, 
                                    s_sm, 
                                    ST7789_PIN_WR, 
                                    1, 
                                    true);

    float div = (float)clock_get_hz(clk_sys) / (62.5e6f);
    if (div < 1.0f) {
        div = 1.0f;
    }
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(s_pio, s_sm, offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, false);

    dma_chan = dma_claim_unused_channel(true);

    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_bswap(&cfg, false);
    channel_config_set_read_increment  (&cfg, true);
    channel_config_set_write_increment (&cfg, false);
    channel_config_set_dreq            (&cfg, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_irq_quiet       (&cfg, false);

    dma_channel_configure(dma_chan, &cfg, &s_pio->txf[s_sm], NULL, 0, false);

    irq_set_exclusive_handler(DMA_IRQ_0, st7789_pio_stream_dma_irq);
    __IRQ_SAFE {
        dma_channel_set_irq0_enabled(dma_chan, false);
        irq_clear_pending(DMA_IRQ_0);
        irq_set_enabled(DMA_IRQ_0, true);
    }
}

static
void st7789_pio_stream_send(const uint8_t *src, size_t len)
{
    while (dma_channel_is_busy(dma_chan)) { 
        tight_loop_contents(); 
    }

    dma_channel_set_read_addr (dma_chan, src, false);
    dma_channel_set_trans_count(dma_chan, len, false);
    __IRQ_SAFE {
        dma_channel_set_irq0_enabled(dma_chan, false);
        irq_clear_pending(DMA_IRQ_0);
    }
    pio_sm_set_enabled(s_pio, s_sm, true);
    dma_channel_start(dma_chan);

    dma_channel_wait_for_finish_blocking(dma_chan);
    
    dma_channel_acknowledge_irq0(dma_chan);
    irq_clear_pending(DMA_IRQ_0);
}

static
void st7789_pio_stream_send_async(const uint8_t *src, size_t len)
{
    while (dma_channel_is_busy(dma_chan)) { 
        tight_loop_contents(); 
    }

    dma_channel_set_read_addr (dma_chan, src, false);
    dma_channel_set_trans_count(dma_chan, len, false);

    __IRQ_SAFE {
        irq_clear_pending(DMA_IRQ_0);
        dma_channel_set_irq0_enabled(dma_chan, true);
    }

    pio_sm_set_enabled(s_pio, s_sm, true);
    dma_channel_start(dma_chan);
}


#if 0
__STATIC_INLINE 
void pio_write_u8(uint8_t v) 
{
    pio_sm_put_blocking(s_pio, s_sm, (uint32_t)v);
}

static void write_cmd(uint8_t cmd) 
{
    dc_command();
    cs_select();
    pio_write_u8(cmd);
    cs_deselect();
}

static 
__attribute__((noinline))
void __write_cmd_with_data(uint8_t cmd, uint8_t *pchData, size_t tSize)
{
    dc_command();
    cs_select();
    pio_write_u8(cmd);
    cs_deselect();

    dc_data();
    cs_select();
    do {
        pio_write_u8(*pchData++);
    } while(--tSize);
    cs_deselect();
}
#else

static void write_cmd(uint8_t cmd) 
{
    dc_command();
    cs_select();
    st7789_pio_stream_send(&cmd, 1);
    cs_deselect();
}

static 
__attribute__((noinline))
void __write_cmd_with_data(uint8_t cmd, uint8_t *pchData, size_t tSize)
{
    dc_command();
    cs_select();
    st7789_pio_stream_send(&cmd, 1);
    cs_deselect();

    dc_data();
    cs_select();
	st7789_pio_stream_send(pchData, tSize);

    cs_deselect();
}

static 
__attribute__((noinline))
void __write_cmd_with_data_async(uint8_t cmd, uint8_t *pchData, size_t tSize)
{
    dc_command();
    cs_select();
    st7789_pio_stream_send(&cmd, 1);
    cs_deselect();

    dc_data();
    cs_select();
    st7789_pio_stream_send_async(pchData, tSize);

}


#endif


static void set_addr_window(int16_t x, int16_t y, int16_t w, int16_t h)
{
    int16_t x0 = x;
    int16_t x1 = (x + w - 1);
    int16_t y0 = y;
    int16_t y1 = (y + h - 1);

    write_cmd_with_data(CASET, (x0 >> 8), (x0 & 0xFF), (x1 >> 8), (x1 & 0xFF));
    write_cmd_with_data(RASET, (y0 >> 8), (y0 & 0xFF), (y1 >> 8), (y1 & 0xFF));
}


void st7789_init(void)
{
	
    s_pio = ST7789_PIO;
    gpio_init(ST7789_PIN_DC);  
    gpio_set_function(ST7789_PIN_DC, GPIO_FUNC_SIO); 
    gpio_set_dir(ST7789_PIN_DC, GPIO_OUT);

    if (ST7789_PIN_CS  >= 0) {
        gpio_init(ST7789_PIN_CS);  
        gpio_set_function(ST7789_PIN_CS, GPIO_FUNC_SIO); 
        gpio_set_dir(ST7789_PIN_CS, GPIO_OUT);
        gpio_put(ST7789_PIN_CS, 1); 
    }

    if (ST7789_PIN_RST >= 0) {
        gpio_init(ST7789_PIN_RST); 
        gpio_set_function(ST7789_PIN_RST, GPIO_FUNC_SIO); 
        gpio_set_dir(ST7789_PIN_RST,GPIO_OUT); 
        gpio_put(ST7789_PIN_RST,1); 
    }

    if (ST7789_PIN_BL  >= 0) {
        gpio_init(ST7789_PIN_BL);
        gpio_set_function(ST7789_PIN_BL, GPIO_FUNC_SIO); 
        gpio_set_dir(ST7789_PIN_BL, GPIO_OUT); 
        gpio_put(ST7789_PIN_BL, 0); 
    }
    for (int i = 0; i < 8; ++i) {
        gpio_init(ST7789_PIN_D0 + i);
        gpio_set_function(  ST7789_PIN_D0 + i, 
                            (   (ST7789_PIO == pio0) 
                            ?   GPIO_FUNC_PIO0 
                            :   GPIO_FUNC_PIO1));
    }

    gpio_set_function(  ST7789_PIN_WR, 
                        (   (ST7789_PIO == pio0) 
                        ?   GPIO_FUNC_PIO0 
                        :   GPIO_FUNC_PIO1));

    gpio_set_function(ST7789_PIN_RD,  GPIO_FUNC_SIO);
    gpio_set_dir(ST7789_PIN_RD, GPIO_OUT);
    gpio_put(ST7789_PIN_RD, 1);

#if 0
    do {
        uint32_t prog_offset = pio_add_program( ST7789_PIO, 
                                                &st77xx_parallel_byte_program);
        pio_sm_config c 
            = st77xx_parallel_byte_program_get_default_config(prog_offset);
        sm_config_set_out_pins(&c, ST7789_PIN_D0, 8);
        sm_config_set_sideset_pins(&c, ST7789_PIN_WR);
        sm_config_set_out_shift(&c, 
                                true /*shift_right*/, 
                                false /*autopull*/, 
                                0 /*threshold unused*/);
        sm_config_set_clkdiv(&c, ST7789_PIO_CLKDIV);

        s_sm  = pio_claim_unused_sm(ST7789_PIO, true);
        pio_sm_init(ST7789_PIO, s_sm, prog_offset, &c);
        pio_sm_set_consecutive_pindirs( ST7789_PIO, 
                                        s_sm, 
                                        ST7789_PIN_D0, 
                                        8, 
                                        true);
        pio_sm_set_consecutive_pindirs( ST7789_PIO, 
                                        s_sm, 
                                        ST7789_PIN_WR, 
                                        1, 
                                        true);
        pio_sm_set_enabled(ST7789_PIO, s_sm, true);
    } while(0);
#else
    st7789_pio_stream_init();
#endif

    if (ST7789_PIN_RST >= 0) {
        rst_assert();  
        sleep_ms(20);
        rst_deassert();
        sleep_ms(120);
    } else {
//        write_cmd(SWRESET);
        sleep_ms(150);
    }
    
    /*---------------------------------------------------
        ILI9342 INITIALIZATION
    ---------------------------------------------------*/

    write_cmd_with_data(0x3A,0x05);   // RGB565
	write_cmd_with_data(0xC5,0x1A);
    write_cmd_with_data(0x36,0x48);   // Memory Access Control 0xc8
                                     // MX, BGR
	write_cmd_with_data(0xB2,0x05,0x05,0x00,0x33,0x33); 
    write_cmd_with_data(0xB7,0x05);
	write_cmd_with_data(0xBB,0x3F);
	// Power control
	write_cmd_with_data(0xC0, 0x2C);

	// VDV and VRH Command Enable
	write_cmd_with_data(0xC2, 0x01);

	// VRH Set
	write_cmd_with_data(0xC3, 0x0F);

	// VDV Set
	write_cmd_with_data(0xC4, 0x20);

	// Frame Rate Control in Normal Mode
	write_cmd_with_data(0xC6, 0x01);

	// Power Control 1
	write_cmd_with_data(0xD0, 0xA4, 0xA1);

	// Power Control 1 (Note: 0xE8 appears to be a different power control register)
	write_cmd_with_data(0xE8, 0x03);

	// Equalize time control
	write_cmd_with_data(0xE9, 0x09, 0x09, 0x08);

	// ILI9342 gamma setting - Positive Gamma Correction
	write_cmd_with_data(0xE0, 0xD0, 0x05, 0x09, 0x09, 0x08, 0x14, 0x28, 0x33, 0x3F, 0x07, 0x13, 0x14, 0x28, 0x30);

	// ILI9342 gamma setting - Negative Gamma Correction
	write_cmd_with_data(0xE1, 0xD0, 0x05, 0x09, 0x09, 0x08, 0x03, 0x24, 0x32, 0x32, 0x3B, 0x14, 0x13, 0x28, 0x2F);

	// Display Inversion On (∑¥œ‘)
	write_cmd(0x20);
	write_cmd(0x11);
	sleep_ms(120);
	write_cmd(0x29);

    sleep_ms(20);

    bl_on();
}


void st7789_draw_bitmap(int16_t x,
                        int16_t y,
                        int16_t width,
                        int16_t height,
                        const uint8_t *pchBitmap) 
{
//    assert( ((uintptr_t)pchBitmap & 0x03) == 0);

    set_addr_window(x, y, width, height);
    size_t total = (size_t)width * (size_t)height * sizeof(uint16_t);
    
    __write_cmd_with_data(RAMWR, (uint8_t *)pchBitmap, total);
}

void st7789_draw_bitmap_async(  int16_t x,
                                int16_t y,
                                int16_t width,
                                int16_t height,
                                const uint8_t *pchBitmap) 
{
    assert( ((uintptr_t)pchBitmap & 0x03) == 0);

    set_addr_window(x, y, width, height);
    size_t total = (size_t)width * (size_t)height * sizeof(uint16_t);
    
    __write_cmd_with_data_async(RAMWR, (uint8_t *)pchBitmap, total);
}

