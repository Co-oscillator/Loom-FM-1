/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Minimal ST7789 driver + 5x7 text for the update loader. RAM-only, polled DMA,
 * no framebuffer: the panel keeps its own GRAM, so a full-screen fill and a few
 * glyphs are enough for the update states (red = unsafe, green = safe,
 * flashing red = reconnect). Only the hal/ register layer is touched here. */
#include "fm1_lcd_hw.h"

#define LDR_LCD_RED   0xF800u
#define LDR_LCD_GREEN 0x07E0u
#define LDR_LCD_BLACK 0x0000u
#define LDR_LCD_WHITE 0xFFFFu

static uint8_t  ldr_lcd_small[64];
static uint8_t  ldr_lcd_busy;           /* a ldr_lcd_data DMA may still run */
static uint16_t ldr_lcd_fillbuf[240];   /* one row of the panel */

static void ldr_lcd_sync(void)
{
    if (!ldr_lcd_busy)
        return;
    fm1_lcd_wait();
    fm1_lcd_deselect();
    ldr_lcd_busy = 0;
}

static void ldr_lcd_cmd(uint8_t c)
{
    ldr_lcd_sync();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void ldr_lcd_data(const void *p, uint32_t n)
{
    if (!n)
        return;                        /* SPI_CNT = 0 never completes */
    ldr_lcd_sync();
    fm1_lcd_send_data(p, n);
    ldr_lcd_busy = 1;
}

static void ldr_lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    ldr_lcd_sync();
    ldr_lcd_small[0] = (uint8_t)(x0 >> 8);
    ldr_lcd_small[1] = (uint8_t)x0;
    ldr_lcd_small[2] = (uint8_t)(x1 >> 8);
    ldr_lcd_small[3] = (uint8_t)x1;
    ldr_lcd_cmd(0x2A);
    ldr_lcd_data(ldr_lcd_small, 4);
    ldr_lcd_small[4] = (uint8_t)(y0 >> 8);   /* own bytes: the x transfer may still read [0..3] */
    ldr_lcd_small[5] = (uint8_t)y0;
    ldr_lcd_small[6] = (uint8_t)(y1 >> 8);
    ldr_lcd_small[7] = (uint8_t)y1;
    ldr_lcd_cmd(0x2B);
    ldr_lcd_data(ldr_lcd_small + 4, 4);
    ldr_lcd_cmd(0x2C);
}

static void ldr_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, sw = (uint16_t)((c >> 8) | (c << 8));
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    ldr_lcd_sync();
    for (i = 0; i < w; i++)
        ldr_lcd_fillbuf[i] = (uint16_t)sw;
    ldr_lcd_window(x, y, x + w - 1u, y + h - 1u);
    while (h--)
        ldr_lcd_data(ldr_lcd_fillbuf, w * 2u);
}

/* 5x7 font, one byte per column, bit 0 = top row. Subset the loader messages need:
 * space, A..Z, 0..9, and a solid box for anything else. */
static void ldr_lcd_glyph(uint8_t c, uint8_t g[5])
{
    static const uint8_t FONT[38][5] = {
        {0x00,0x00,0x00,0x00,0x00},   /* space */
        {0x7E,0x11,0x11,0x11,0x7E},   /* A */
        {0x7F,0x49,0x49,0x49,0x36},   /* B */
        {0x3E,0x41,0x41,0x41,0x22},   /* C */
        {0x7F,0x41,0x41,0x22,0x1C},   /* D */
        {0x7F,0x49,0x49,0x49,0x41},   /* E */
        {0x7F,0x09,0x09,0x09,0x01},   /* F */
        {0x3E,0x41,0x49,0x49,0x7A},   /* G */
        {0x7F,0x08,0x08,0x08,0x7F},   /* H */
        {0x00,0x41,0x7F,0x41,0x00},   /* I */
        {0x20,0x40,0x41,0x3F,0x01},   /* J */
        {0x7F,0x08,0x14,0x22,0x41},   /* K */
        {0x7F,0x40,0x40,0x40,0x40},   /* L */
        {0x7F,0x02,0x0C,0x02,0x7F},   /* M */
        {0x7F,0x04,0x08,0x10,0x7F},   /* N */
        {0x3E,0x41,0x41,0x41,0x3E},   /* O */
        {0x7F,0x09,0x09,0x09,0x06},   /* P */
        {0x3E,0x41,0x51,0x21,0x5E},   /* Q */
        {0x7F,0x09,0x19,0x29,0x46},   /* R */
        {0x46,0x49,0x49,0x49,0x31},   /* S */
        {0x01,0x01,0x7F,0x01,0x01},   /* T */
        {0x3F,0x40,0x40,0x40,0x3F},   /* U */
        {0x1F,0x20,0x40,0x20,0x1F},   /* V */
        {0x3F,0x40,0x38,0x40,0x3F},   /* W */
        {0x63,0x14,0x08,0x14,0x63},   /* X */
        {0x07,0x08,0x70,0x08,0x07},   /* Y */
        {0x61,0x51,0x49,0x45,0x43},   /* Z */
        {0x3E,0x51,0x49,0x45,0x3E},   /* 0 */
        {0x00,0x42,0x7F,0x40,0x00},   /* 1 */
        {0x42,0x61,0x51,0x49,0x46},   /* 2 */
        {0x21,0x41,0x45,0x4B,0x31},   /* 3 */
        {0x18,0x14,0x12,0x7F,0x10},   /* 4 */
        {0x27,0x45,0x45,0x45,0x39},   /* 5 */
        {0x3C,0x4A,0x49,0x49,0x30},   /* 6 */
        {0x01,0x71,0x09,0x05,0x03},   /* 7 */
        {0x36,0x49,0x49,0x49,0x36},   /* 8 */
        {0x06,0x49,0x49,0x29,0x1E},   /* 9 */
        {0x7F,0x7F,0x7F,0x7F,0x7F},   /* box (unknown) */
    };
    uint32_t i, idx = 37u;
    if (c == ' ')
        idx = 0;
    else if (c >= 'A' && c <= 'Z')
        idx = 1u + (uint32_t)(c - 'A');
    else if (c >= '0' && c <= '9')
        idx = 27u + (uint32_t)(c - '0');
    for (i = 0; i < 5u; i++)
        g[i] = FONT[idx][i];
}

/* draw a line of 2x-scaled 5x7 glyphs (12 x 14 px each) onto the current fill */
static void ldr_lcd_text(uint32_t x0, uint32_t y0, const char *s, uint16_t fg)
{
    uint8_t g[5];
    uint32_t i, col, row;
    for (i = 0; s[i]; i++) {
        ldr_lcd_glyph((uint8_t)s[i], g);
        for (col = 0; col < 5u; col++)
            for (row = 0; row < 7u; row++)
                if (g[col] & (1u << row))
                    ldr_lcd_fill(x0 + i * 12u + col * 2u, y0 + row * 2u, 2u, 2u, fg);
    }
}

/* ST7789V init: reset, RGB565, top-left origin, IPS inversion, display on. */
static const uint8_t LDR_LCD_SEQ[] = {
    0x01, 0,                     /* SWRESET */
    0x00, 1, 150,
    0x11, 0,                     /* SLPOUT */
    0x00, 1, 120,
    0x3A, 1, 0x55,               /* COLMOD: RGB565 */
    0x36, 1, 0x00,               /* MADCTL: top-left origin, RGB order */
    0x21, 0,                     /* INVON: IPS panel */
    0x13, 0,                     /* NORON */
};

static void ldr_lcd_init(void)
{
    uint32_t r, x;
    fm1_lcd_hw_init();
    for (r = 0; r < 2000000u; r++)
        ;
    for (r = 0; r < sizeof LDR_LCD_SEQ; r += 2u + LDR_LCD_SEQ[r + 1u]) {
        if (LDR_LCD_SEQ[r] == 0x00u) {                   /* pseudo command: wait */
            for (x = LDR_LCD_SEQ[r + 2u] * 25000u; x; x--)
                ;
            continue;
        }
        ldr_lcd_cmd(LDR_LCD_SEQ[r]);
        for (x = 0; x < LDR_LCD_SEQ[r + 1u]; x++)
            ldr_lcd_small[x] = LDR_LCD_SEQ[r + 2u + x];
        if (LDR_LCD_SEQ[r + 1u])
            ldr_lcd_data(ldr_lcd_small, LDR_LCD_SEQ[r + 1u]);
    }
    fm1_lcd_baud(4u);                                    /* 12 MHz */
    ldr_lcd_fill(0, 0, 240, 240, LDR_LCD_BLACK);
    ldr_lcd_cmd(0x29);                                   /* DISPON */
}
