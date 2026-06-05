#ifndef BIGOS_KEYBOARD_H
#define BIGOS_KEYBOARD_H

#include <bigos/types.h>

namespace bigos::input {
    struct KeyboardDecodeStats {
        uint64_t unsupported;
    };

    void init_keyboard_decoder() noexcept;

    // Decodes one PS/2 set-1 scancode. Returns true only when a character is produced.
    bool decode_ps2_set1(uint8_t scancode, char *out) noexcept;

    // IRQ-context handoff helper: decode one scancode and enqueue supported input.
    void handle_keyboard_scancode(uint8_t scancode) noexcept;

    KeyboardDecodeStats keyboard_decode_stats() noexcept;
}   // namespace bigos::input

#endif   // BIGOS_KEYBOARD_H
