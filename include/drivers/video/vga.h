#ifndef _BIG_VGA_H
#define _BIG_VGA_H

#include <bigos/types.h>

// vga data register ports
#define VGA_ATT_W  0x3c0   // attribute controller data write register
#define VGA_MIS_W  0x3c2   // miscellaneous output write register
#define VGA_MIS_R  0x3cc   // miscellaneous output read register
#define VGA_CRT_DC 0x3d5   // CRT controller data register - color emulation

// vga index register ports
#define VGA_ATT_IW 0x3c0   // attribute controller index write register
#define VGA_SEQ_I  0x3c4   // sequencer index
#define VGA_CRT_IC 0x3d4   // CRT controller index - color emulation

// vga text mode foreground colors
#define VT_FGC_BLACK       0x00
#define VT_FGC_BLUE        0x01
#define VT_FGC_GREEN       0x02
#define VT_FGC_CYAN        0x03
#define VT_FGC_RED         0x04
#define VT_FGC_MAGENTA     0x05
#define VT_FGC_BROWN       0x06
#define VT_FGC_LIGHT_GRAY  0x07
#define VT_FGC_DARK_GRAY   0x08
#define VT_FGC_LIGHT_BLUE  0x09
#define VT_FGC_LIGHT_GREEN 0x0a
#define VT_FGC_LIGHT_CYAN  0x0b
#define VT_FGC_LIGHT_RED   0x0c
#define VT_FGC_PINK        0x0d
#define VT_FGC_YELLOW      0x0e
#define VT_FGC_WHITE       0x0f

// vga text mode background colors
#define VT_BLINK          0x80
#define VT_BGC_BLACK      0x00
#define VT_BGC_BLUE       0x10
#define VT_BGC_GREEN      0x20
#define VT_BGC_CYAN       0x30
#define VT_BGC_RED        0x40
#define VT_BGC_MAGENTA    0x50
#define VT_BGC_BROWN      0x60
#define VT_BGC_LIGHT_GREY 0x70

// vga text mode colors
#define VT_COLOR_NORMAL   0x0f
#define VT_COLOR_WARNNING 0x0e
#define VT_COLOR_ERROR    0x0c
#define VT_COLOR_SUCCESS  0x0a

NAMESPACE_DRIVER_BEG
namespace video {
    namespace vga {
        struct DisplayMode {
            uint32_t width;
            uint32_t height;
            bool isText;
            uint8_t aligned[3];
            // uint8_t color_bytes;
        };

        // text mode functions
        // set the cursor position
        void set_cursor(uint8_t __x, uint8_t __y);

        // if __offset is positive move forward else move backward
        void move_cursor(int16_t __offset = 1);

        void write(char __ch, uint8_t __color = VT_COLOR_NORMAL);
        void write(char __ch, uint8_t __x, uint8_t __y, uint8_t __color = VT_COLOR_NORMAL);

        void write(const char *__s, uint8_t __color = VT_COLOR_NORMAL);
        void write(const char *__s, uint8_t __x, uint8_t __y, uint8_t __color = VT_COLOR_NORMAL);

        void clear_screen();
        void fill_cell(uint8_t __x, uint8_t __y, char __ch, uint8_t __color = VT_COLOR_NORMAL);

        // if __offset is positive scroll up else scroll down
        // TODO void scroll_screen(int16_t __offset = 1);

        // TODO graphic mode functions

        // TODO common functions

    }   // namespace vga
}   // namespace video
NAMESPACE_DRIVER_END
#endif   // _BIG_VGA_H
