#include <bigos/keyboard.h>
#include <bigos/io.h>
#include <bigos/tty.h>

namespace bigos::input {
    namespace {
        using terminal::TerminalControl;
        using terminal::TerminalInputKind;
        using terminal::TerminalInputRecord;

        constexpr uint8_t SCANCODE_RELEASE_BIT = 0x80;
        constexpr uint8_t SCANCODE_EXTENDED_E0 = 0xe0;
        constexpr uint8_t SCANCODE_EXTENDED_E1 = 0xe1;

        constexpr uint8_t SCANCODE_LEFT_SHIFT = 0x2a;
        constexpr uint8_t SCANCODE_RIGHT_SHIFT = 0x36;
        constexpr uint8_t SCANCODE_CTRL = 0x1d;
        constexpr uint8_t SCANCODE_ALT = 0x38;
        constexpr uint8_t SCANCODE_HOME = 0x47;
        constexpr uint8_t SCANCODE_ARROW_UP = 0x48;
        constexpr uint8_t SCANCODE_PAGE_UP = 0x49;
        constexpr uint8_t SCANCODE_ARROW_LEFT = 0x4b;
        constexpr uint8_t SCANCODE_ARROW_RIGHT = 0x4d;
        constexpr uint8_t SCANCODE_END = 0x4f;
        constexpr uint8_t SCANCODE_ARROW_DOWN = 0x50;
        constexpr uint8_t SCANCODE_PAGE_DOWN = 0x51;
        constexpr uint8_t SCANCODE_DELETE = 0x53;

        struct KeyMapEntry {
            char normal;
            char shifted;
            char ctrl;
        };

        struct KeyboardState {
            bool shift;
            bool ctrl;
            bool alt;
            bool extended;
            uint64_t unsupported;
        };

        KeyboardState g_state;

        constexpr KeyMapEntry key(uint8_t scancode, char normal, char shifted = 0, char ctrl = 0) noexcept {
            (void)scancode;
            return {normal, shifted == 0 ? normal : shifted, ctrl};
        }

        constexpr KeyMapEntry g_set1_keymap[128] = {
            {},
            key(0x01, '\x1b'),
            key(0x02, '1', '!'),
            key(0x03, '2', '@'),
            key(0x04, '3', '#'),
            key(0x05, '4', '$'),
            key(0x06, '5', '%'),
            key(0x07, '6', '^'),
            key(0x08, '7', '&'),
            key(0x09, '8', '*'),
            key(0x0a, '9', '('),
            key(0x0b, '0', ')'),
            key(0x0c, '-', '_'),
            key(0x0d, '=', '+'),
            key(0x0e, '\b'),
            key(0x0f, '\t'),
            key(0x10, 'q', 'Q', 0x11),
            key(0x11, 'w', 'W', 0x17),
            key(0x12, 'e', 'E', 0x05),
            key(0x13, 'r', 'R', 0x12),
            key(0x14, 't', 'T', 0x14),
            key(0x15, 'y', 'Y', 0x19),
            key(0x16, 'u', 'U', 0x15),
            key(0x17, 'i', 'I', 0x09),
            key(0x18, 'o', 'O', 0x0f),
            key(0x19, 'p', 'P', 0x10),
            key(0x1a, '[', '{', 0x1b),
            key(0x1b, ']', '}', 0x1d),
            key(0x1c, '\n'),
            {},
            key(0x1e, 'a', 'A', 0x01),
            key(0x1f, 's', 'S', 0x13),
            key(0x20, 'd', 'D', 0x04),
            key(0x21, 'f', 'F', 0x06),
            key(0x22, 'g', 'G', 0x07),
            key(0x23, 'h', 'H', 0x08),
            key(0x24, 'j', 'J', 0x0a),
            key(0x25, 'k', 'K', 0x0b),
            key(0x26, 'l', 'L', 0x0c),
            key(0x27, ';', ':'),
            key(0x28, '\'', '"'),
            key(0x29, '`', '~'),
            {},
            key(0x2b, '\\', '|', 0x1c),
            key(0x2c, 'z', 'Z', 0x1a),
            key(0x2d, 'x', 'X', 0x18),
            key(0x2e, 'c', 'C', 0x03),
            key(0x2f, 'v', 'V', 0x16),
            key(0x30, 'b', 'B', 0x02),
            key(0x31, 'n', 'N', 0x0e),
            key(0x32, 'm', 'M', 0x0d),
            key(0x33, ',', '<'),
            key(0x34, '.', '>'),
            key(0x35, '/', '?'),
            {},
            key(0x37, '*'),
            {},
            key(0x39, ' '),
        };

        bool update_modifier(uint8_t base_scancode, bool pressed) noexcept {
            switch (base_scancode) {
                case SCANCODE_LEFT_SHIFT:
                case SCANCODE_RIGHT_SHIFT:
                    g_state.shift = pressed;
                    return true;
                case SCANCODE_CTRL:
                    g_state.ctrl = pressed;
                    return true;
                case SCANCODE_ALT:
                    g_state.alt = pressed;
                    return true;
                default:
                    return false;
            }
        }

        void record_unsupported() noexcept {
            ++g_state.unsupported;
        }

        bool make_navigation_record(uint8_t base_scancode, TerminalInputRecord *out) noexcept {
            if (out == nullptr)
                return false;

            TerminalControl control = TerminalControl::Unsupported;
            switch (base_scancode) {
                case SCANCODE_HOME:
                    control = TerminalControl::NavigateHome;
                    break;
                case SCANCODE_ARROW_UP:
                    control = TerminalControl::NavigateUp;
                    break;
                case SCANCODE_ARROW_LEFT:
                    control = TerminalControl::NavigateLeft;
                    break;
                case SCANCODE_ARROW_RIGHT:
                    control = TerminalControl::NavigateRight;
                    break;
                case SCANCODE_END:
                    control = TerminalControl::NavigateEnd;
                    break;
                case SCANCODE_ARROW_DOWN:
                    control = TerminalControl::NavigateDown;
                    break;
                case SCANCODE_PAGE_UP:
                    control = g_state.shift ? TerminalControl::KernelScrollPageUp : TerminalControl::NavigatePageUp;
                    break;
                case SCANCODE_PAGE_DOWN:
                    control =
                        g_state.shift ? TerminalControl::KernelScrollPageDown : TerminalControl::NavigatePageDown;
                    break;
                case SCANCODE_DELETE:
                    control = TerminalControl::NavigateDelete;
                    break;
                default:
                    return false;
            }

            *out = {TerminalInputKind::Control, 0, control};
            return true;
        }

        bool decode_ps2_set1_record(uint8_t scancode, TerminalInputRecord *out) noexcept {
            if (out == nullptr)
                return false;

            if (scancode == SCANCODE_EXTENDED_E0 || scancode == SCANCODE_EXTENDED_E1) {
                g_state.extended = true;
                return false;
            }

            const bool released = (scancode & SCANCODE_RELEASE_BIT) != 0;
            const uint8_t base_scancode = scancode & (uint8_t)~SCANCODE_RELEASE_BIT;

            if (g_state.extended) {
                g_state.extended = false;
                if (released)
                    return false;
                if (make_navigation_record(base_scancode, out))
                    return true;
                record_unsupported();
                return false;
            }

            if (base_scancode >= 128) {
                record_unsupported();
                return false;
            }

            if (update_modifier(base_scancode, !released))
                return false;

            if (released)
                return false;

            const KeyMapEntry entry = g_set1_keymap[base_scancode];
            char translated = 0;
            if (g_state.ctrl && entry.ctrl != 0)
                translated = entry.ctrl;
            else if (g_state.shift)
                translated = entry.shifted;
            else
                translated = entry.normal;

            if (translated == 0) {
                record_unsupported();
                return false;
            }

            *out = {TerminalInputKind::Character, translated, TerminalControl::None};
            return true;
        }
    }   // namespace

    void init_keyboard_decoder() noexcept {
        g_state = {};
    }

    bool decode_ps2_set1(uint8_t scancode, char *out) noexcept {
        if (out == nullptr)
            return false;

        TerminalInputRecord record{};
        if (!decode_ps2_set1_record(scancode, &record))
            return false;

        if (record.kind != TerminalInputKind::Character)
            return false;

        *out = record.ch;
        return true;
    }

    void handle_keyboard_scancode(uint8_t scancode) noexcept {
        TerminalInputRecord record{};
        if (decode_ps2_set1_record(scancode, &record)) {
            (void)terminal::enqueue_input_record(record);
        }
    }

    KeyboardDecodeStats keyboard_decode_stats() noexcept {
        return {g_state.unsupported};
    }
}   // namespace bigos::input
