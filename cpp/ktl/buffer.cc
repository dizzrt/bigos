#include <ktl/buffer.h>

#include <string.h>
#include <memory.h>

ktl::Buffer::Buffer(uint32_t __cap, ptr8_t __buffer) : cap_(__cap), size_(0), head(0), tail(0), buffer_(__buffer) {
    if (buffer_ == nullptr)
        buffer_ = (ptr8_t)bigos::kmalloc(__cap);
}

ktl::Buffer::Buffer() : Buffer(8, nullptr) {}

void ktl::Buffer::clear() {
    head = 0;
    tail = 0;
    size_ = 0;
}

uint8_t ktl::Buffer::read() {
    if (size_ == 0)
        return 0;

    uint8_t ret = buffer_[head];
    head = (head + 1) % cap_;

    size_--;
    return ret;
}

uint32_t ktl::Buffer::read(ptr8_t __buffer, uint32_t __len) {
    uint32_t i = 0, ret = 0;
    while (__len-- && size_--) {
        __buffer[i] = buffer_[head];
        head = (head + 1) % cap_;

        i++;
        ret++;
    }

    return ret;
}

uint32_t ktl::Buffer::write(uint8_t __data) {
    if (size_ >= cap_)
        return 0;

    buffer_[tail] = __data;
    tail = (tail + 1) % cap_;

    size_++;
    return 1;
}

uint32_t ktl::Buffer::write(ptr8_t __data, uint32_t __len) {
    uint32_t i = 0, ret = 0;
    while (__len-- && size_ < cap_) {
        buffer_[tail] = __data[i++];
        tail = (tail + 1) % cap_;

        ret++;
        size_++;
    }

    return ret;
}

uint32_t ktl::Buffer::write(const char *__data) {
    uint32_t len = strlen(__data);
    return write(__data, len);
}

uint32_t ktl::Buffer::write(const char *__data, uint32_t __len) {
    uint32_t i = 0, ret = 0;
    while (__len-- && size_ < cap_) {
        buffer_[tail] = __data[i++];
        tail = (tail + 1) % cap_;

        ret++;
        size_++;
    }

    return ret;
}
