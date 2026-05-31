#ifndef _BIG_BITSET_H
#define _BIG_BITSET_H

#include <bigos/types.h>

namespace ktl {
    class bitset {
    protected:
        static constexpr uint32_t bits_per_byte_ = 8;
        // bits heap pointer
        ptr8_t heap_ptr_;

        uint32_t nr_bits_;
        // how many bits are not set
        uint32_t nr_avl_bits_;
        bool owns_storage_;

    public:
        static constexpr uint32_t npos = static_cast<uint32_t>(-1);

        // constructor
        bitset(uint32_t _nr_bits, ptr8_t _heap_ptr);
        bitset(uint32_t _nr_bits);
        bitset();

        virtual ~bitset();

        inline uint32_t size() const noexcept {
            return nr_bits_;
        }
        inline uint32_t reset_size() const noexcept {
            return nr_avl_bits_;
        }
        inline uint32_t set_size() const noexcept {
            return nr_bits_ - nr_avl_bits_;
        }
        inline bool valid() const noexcept {
            return heap_ptr_ != nullptr || nr_bits_ == 0;
        }

        // ret => how many bits are set
        uint32_t set(uint32_t _pos, uint32_t _len) noexcept;
        uint32_t set(uint32_t _pos) noexcept;
        uint32_t set() noexcept;

        // ret => how many bits are reset
        uint32_t reset(uint32_t _pos, uint32_t _len) noexcept;
        uint32_t reset(uint32_t _pos) noexcept;
        uint32_t reset() noexcept;

        uint32_t scan(uint32_t _len) noexcept;

        // ret => how many bits are fliped
        // uint32_t flip(uint32_t _pos, uint32_t _len) noexcept;
        // uint32_t flip(uint32_t _pos) noexcept;
        // uint32_t flip() noexcept;

        // return true if the target bit is set, else return false
        // bool test(uint32_t _pos, uint32_t _len) noexcept;
        bool test(uint32_t _pos) noexcept;
        // bool test() noexcept;
    };

}   // namespace ktl

#endif   // _BIG_BITSET_H
