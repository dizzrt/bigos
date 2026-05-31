#include <ktl/bitset.h>
#include <bigos/memory.h>
#include <string.h>

namespace ktl {
    bitset::bitset(uint32_t _nr_bits, ptr8_t _heap_ptr)
        : heap_ptr_(_heap_ptr), nr_bits_(_nr_bits), nr_avl_bits_(0), owns_storage_(false) {
        uint32_t heap_size = (_nr_bits + bits_per_byte_ - 1) / bits_per_byte_;
        if (_nr_bits == 0) {
            heap_ptr_ = nullptr;
            return;
        }

        if (heap_ptr_ == nullptr) {
            heap_ptr_ = static_cast<ptr8_t>(bigos::kmalloc(heap_size));
            owns_storage_ = heap_ptr_ != nullptr;
        }

        if (heap_ptr_ != nullptr) {
            memset(heap_ptr_, 0, heap_size);
            nr_avl_bits_ = nr_bits_;
        }
    }

    bitset::bitset(uint32_t _nr_bits) : bitset(_nr_bits, nullptr) {}

    bitset::bitset() : bitset(8, nullptr) {}

    bitset::~bitset() {
        if (owns_storage_ && heap_ptr_ != nullptr)
            bigos::free(heap_ptr_);
    }

    // ret => how many bits are set
    uint32_t bitset::set(uint32_t _pos, uint32_t _len) noexcept {
        if (heap_ptr_ == nullptr || _len == 0 || _pos >= nr_bits_)
            return 0;

        if (_len > nr_bits_ - _pos)
            _len = nr_bits_ - _pos;

        uint32_t ret = 0;
        for (uint32_t i = 0; i < _len; ++i) {
            uint32_t bit = _pos + i;
            ptr8_t bp = heap_ptr_ + bit / bits_per_byte_;
            uint8_t mask = static_cast<uint8_t>(0x80u >> (bit % bits_per_byte_));
            if ((*bp & mask) == 0) {
                *bp |= mask;
                ret++;
            }
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
    uint32_t bitset::reset(uint32_t _pos, uint32_t _len) noexcept {
        if (heap_ptr_ == nullptr || _len == 0 || _pos >= nr_bits_)
            return 0;

        if (_len > nr_bits_ - _pos)
            _len = nr_bits_ - _pos;

        uint32_t ret = 0;
        for (uint32_t i = 0; i < _len; ++i) {
            uint32_t bit = _pos + i;
            ptr8_t bp = heap_ptr_ + bit / bits_per_byte_;
            uint8_t mask = static_cast<uint8_t>(0x80u >> (bit % bits_per_byte_));
            if ((*bp & mask) != 0) {
                *bp = static_cast<uint8_t>(*bp & ~mask);
                ret++;
            }
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
        if (heap_ptr_ == nullptr || _len == 0 || _len > nr_avl_bits_ || _len > nr_bits_)
            return npos;

        uint32_t run_start = npos;
        uint32_t run_len = 0;
        for (uint32_t i = 0; i < nr_bits_; ++i) {
            if (!test(i)) {
                if (run_len == 0)
                    run_start = i;
                if (++run_len == _len)
                    return run_start;
            } else {
                run_len = 0;
                run_start = npos;
            }
        }

        return npos;
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
        if (heap_ptr_ == nullptr || _pos >= nr_bits_)
            return false;
        return (heap_ptr_[_pos / bits_per_byte_] >> (bits_per_byte_ - _pos % bits_per_byte_ - 1)) & 1;
    }

    // bool bitset::test() noexcept {
    //     return test(0, nr_bits_);
    // }
}   // namespace ktl
