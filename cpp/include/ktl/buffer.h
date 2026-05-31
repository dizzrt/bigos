#ifndef _BIG_BUFFER_H
#define _BIG_BUFFER_H

#include <bigos/types.h>

namespace ktl {
    class Buffer {
    private:
        uint32_t cap_;
        uint32_t size_;

        uint32_t head;
        uint32_t tail;

        ptr8_t buffer_;

    public:
        Buffer();
        Buffer(uint32_t __cap, ptr8_t __buffer = nullptr);

        void clear() noexcept;

        inline uint32_t size() noexcept { return size_; }
        inline uint32_t capacity() noexcept { return cap_; }
        inline bool empty() noexcept { return size_ == 0; }

        uint8_t read() noexcept;
        uint32_t read(ptr8_t __buffer, uint32_t __len) noexcept;

        uint32_t write(uint8_t __data);
        uint32_t write(ptr8_t __data, uint32_t __len);

        uint32_t write(const char *__data);
        uint32_t write(const char *__data, uint32_t __len);
    };
}   // namespace ktl
#endif   // _BIG_BUFFER_Hww
