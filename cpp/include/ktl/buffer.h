#ifndef _BIG_BUFFER_H
#define _BIG_BUFFER_H

#include <bigos/types.h>

namespace ktl {
    class buffer {
    private:
        uint32_t cap_;
        uint32_t size_;

        uint32_t head;
        uint32_t tail;

        ptr8_t buffer_;
        bool owns_storage_;

    public:
        buffer();
        buffer(uint32_t __cap, ptr8_t __buffer = nullptr);
        buffer(const buffer &) = delete;
        buffer &operator=(const buffer &) = delete;
        ~buffer();

        void clear() noexcept;

        inline uint32_t size() noexcept {
            return size_;
        }
        inline uint32_t capacity() noexcept {
            return cap_;
        }
        inline bool empty() noexcept {
            return size_ == 0;
        }
        inline bool valid() const noexcept {
            return buffer_ != nullptr && cap_ != 0;
        }

        uint8_t read() noexcept;
        uint32_t read(ptr8_t __buffer, uint32_t __len) noexcept;

        uint32_t write(uint8_t __data) noexcept;
        uint32_t write(ptr8_t __data, uint32_t __len) noexcept;

        uint32_t write(const char *__data) noexcept;
        uint32_t write(const char *__data, uint32_t __len) noexcept;
    };
}   // namespace ktl
#endif   // _BIG_BUFFER_H
