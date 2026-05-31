#include <ktl/bitset.h>

namespace ktl {
    bitset::bitset(uint32_t _nr_bits, ptr8_t _heap_ptr)
        : heap_ptr_(_heap_ptr), nr_bits_(_nr_bits), nr_avl_bits_(_nr_bits) {
        if (heap_ptr_ == nullptr) {
            uint8_t heap_size = (_nr_bits + 7) / 8;

            // TODO memory alloc
        }
    }

    bitset::bitset(uint32_t _nr_bits) : bitset(_nr_bits, nullptr) {}

    bitset::bitset() : bitset(8, nullptr) {}

    // ret => how many bits are set
    uint32_t bitset::set(uint32_t _pos, uint32_t _len) noexcept {
        if (_pos + _len > nr_bits_)
            _len = nr_bits_ - _pos;

        uint32_t ret = 0;
        ptr8_t bp = heap_ptr_ + _pos / 8;

        uint8_t temp = _pos % 8;
        uint8_t mask = 0x80 >> temp;

        temp = 8 - temp;
        if (_len <= temp)
            temp = _len;
        _len -= temp;

        // byte alignment
        while (temp--) {
            if (!(*bp & mask)) {
                *bp |= mask;
                ret++;
            }
            mask >>= 1;
        }
        bp++;

        temp = _len / 8;
        _len = _len - temp * 8;
        while (temp--) {
            uint8_t __x = *bp;
            while (__x) {
                if (__x & 1)
                    ret++;
                __x >>= 1;
            }

            *bp = 0xff;
            bp++;
        }

        mask = 0x80;
        while (_len--) {
            if (!(*bp & mask)) {
                *bp |= mask;
                ret++;
            }
            mask >>= 1;
        }

        nr_avl_bits_ -= ret;
        return ret;
    }

    uint32_t bitset::set(uint32_t _pos) noexcept {
        return set(_pos, 1);
    }

    uint32_t bitset::set() noexcept {
        return set(0, nr_bits_);
    }

    // ret => how many bits are reset
    uint32_t bitset::reset(uint32_t _pos, uint32_t _len) {
        if (_pos + _len > nr_bits_)
            _len = nr_bits_ - _pos;

        uint32_t ret = 0;
        ptr8_t bp = heap_ptr_ + _pos / 8;

        uint8_t temp = _pos % 8;
        uint8_t mask = 0x80 >> temp;

        temp = 8 - temp;
        if (_len <= temp)
            temp = _len;
        _len -= temp;

        // byte alignment
        while (temp--) {
            if (*bp & mask) {
                *bp &= ~mask;
                ret++;
            }
            mask >>= 1;
        }
        bp++;

        temp = _len / 8;
        _len = _len - temp * 8;
        while (temp--) {
            uint8_t __x = *bp;
            uint32_t sub_ret = 8;
            while (__x) {
                if (__x & 1)
                    sub_ret--;
                __x >>= 1;
            }
            ret += sub_ret;

            *bp = 0;
            bp++;
        }

        mask = 0x80;
        while (_len--) {
            if (*bp & mask) {
                *bp &= ~mask;
                ret++;
            }
            mask >>= 1;
        }

        nr_avl_bits_ += ret;
        return ret;
    }

    uint32_t bitset::reset(uint32_t _pos) noexcept {
        return reset(_pos, 1);
    }

    uint32_t bitset::reset() noexcept {
        return reset(0, nr_bits_);
    }

    uint32_t bitset::scan(uint32_t _len) noexcept {
        ptr16_t ptr_16;
        ptr32_t ptr_32;
        ptr64_t ptr_64;

        for (uint32_t i = 0; i < nr_bits_; i++) {
            ptr_64 = (ptr64_t)(heap_ptr_ + i);
            if (i + 64 < nr_bits_ && *ptr_64 == 0xffffffffffffffff)
                i += 63;
            else {
                ptr_32 = (ptr32_t)(heap_ptr_ + i);
                if (i + 32 < nr_bits_ && *ptr_32 == 0xffffffff)
                    i += 31;
                else {
                    ptr_16 = (ptr16_t)(heap_ptr_ + i);
                    if (i + 16 < nr_bits_ && *ptr_16 == 0xffff)
                        i += 15;
                    else {
                        if (heap_ptr_[i / 8] == 0xff) {
                            i += 7;
                            continue;
                        }

                        if (test(i)) {
                            uint32_t cnt = 0, j = i;
                            while (test(j) && cnt < _len && j < nr_bits_)
                                j++, cnt++;

                            if (cnt == _len)
                                return i;
                            i = --j;
                        }
                    }
                }
            }
        }

        return -1;
    }

    // ret => how many bits are fliped
    // uint32_t bitset::flip(uint32_t _pos, uint32_t _len) noexcept {}

    // uint32_t bitset::flip(uint32_t _pos) noexcept {
    //     return flip(_pos, 1);
    // }

    // uint32_t bitset::flip() noexcept {
    //     return flip(0, nr_bits_);
    // }

    // return true if all target bits are set, else return false
    // bool bitset::test(uint32_t _pos, uint32_t _len) noexcept {}

    bool bitset::test(uint32_t _pos) noexcept {
        return !(heap_ptr_[_pos / 8] >> (8 - _pos % 8 - 1) & 1);
    }

    // bool bitset::test() noexcept {
    //     return test(0, nr_bits_);
    // }
}   // namespace ktl
