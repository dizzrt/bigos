#include <ktl/buffer.h>

#include <string.h>
#include <bigos/memory.h>

ktl::buffer::buffer(uint32_t __cap, ptr8_t __buffer)
    : cap_(__cap), size_(0), head(0), tail(0), buffer_(__buffer), owns_storage_(false) {
    if (cap_ == 0)
        return;

    if (buffer_ == nullptr) {
        buffer_ = (ptr8_t)bigos::kmalloc(__cap);
        owns_storage_ = buffer_ != nullptr;
    }
}

ktl::buffer::buffer() : buffer(8, nullptr) {}

ktl::buffer::~buffer() {
    if (owns_storage_ && buffer_ != nullptr)
        bigos::free(buffer_);
}

void ktl::buffer::clear() noexcept {
    head = 0;
    tail = 0;
    size_ = 0;
}

uint8_t ktl::buffer::read() noexcept {
    if (!valid() || size_ == 0)
        return 0;

    uint8_t ret = buffer_[head];
    head = (head + 1) % cap_;

    size_--;
    return ret;
}

uint32_t ktl::buffer::read(ptr8_t __buffer, uint32_t __len) noexcept {
    if (!valid() || __buffer == nullptr || __len == 0)
        return 0;

    uint32_t i = 0, ret = 0;
    while (__len-- && size_ > 0) {
        __buffer[i] = buffer_[head];
        head = (head + 1) % cap_;

        i++;
        ret++;
        size_--;
    }

    return ret;
}

uint32_t ktl::buffer::write(uint8_t __data) noexcept {
    if (!valid() || size_ >= cap_)
        return 0;

    buffer_[tail] = __data;
    tail = (tail + 1) % cap_;

    size_++;
    return 1;
}

uint32_t ktl::buffer::write(ptr8_t __data, uint32_t __len) noexcept {
    if (!valid() || __data == nullptr || __len == 0)
        return 0;

    uint32_t i = 0, ret = 0;
    while (__len-- && size_ < cap_) {
        buffer_[tail] = __data[i++];
        tail = (tail + 1) % cap_;

        ret++;
        size_++;
    }

    return ret;
}

uint32_t ktl::buffer::write(const char *__data) noexcept {
    if (__data == nullptr)
        return 0;

    uint32_t len = strlen(__data);
    return write(__data, len);
}

uint32_t ktl::buffer::write(const char *__data, uint32_t __len) noexcept {
    if (!valid() || __data == nullptr || __len == 0)
        return 0;

    uint32_t i = 0, ret = 0;
    while (__len-- && size_ < cap_) {
        buffer_[tail] = __data[i++];
        tail = (tail + 1) % cap_;

        ret++;
        size_++;
    }

    return ret;
}
