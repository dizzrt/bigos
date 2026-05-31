#ifndef _BIG_KTL_QUEUE_H
#define _BIG_KTL_QUEUE_H

#include <bigos/types.h>

namespace ktl {
    template <typename _Tp>
    class queue {
    private:
        _Tp *storage_;
        uint32_t cap_;
        uint32_t size_;
        uint32_t head_;
        uint32_t tail_;

    public:
        queue(_Tp *__storage, uint32_t __capacity) noexcept
            : storage_(__storage), cap_(__capacity), size_(0), head_(0), tail_(0) {}
        queue(const queue &) = delete;
        queue &operator=(const queue &) = delete;

        bool valid() const noexcept {
            return storage_ != nullptr && cap_ != 0;
        }
        bool empty() const noexcept {
            return size_ == 0;
        }
        bool full() const noexcept {
            return size_ == cap_;
        }
        uint32_t size() const noexcept {
            return size_;
        }
        uint32_t capacity() const noexcept {
            return cap_;
        }

        bool push(const _Tp &__value) noexcept {
            if (!valid() || full())
                return false;
            storage_[tail_] = __value;
            tail_ = (tail_ + 1) % cap_;
            ++size_;
            return true;
        }

        bool pop(_Tp *__out = nullptr) noexcept {
            if (!valid() || empty())
                return false;
            if (__out != nullptr)
                *__out = storage_[head_];
            head_ = (head_ + 1) % cap_;
            --size_;
            return true;
        }

        _Tp *front() noexcept {
            return (!valid() || empty()) ? nullptr : &storage_[head_];
        }
        const _Tp *front() const noexcept {
            return (!valid() || empty()) ? nullptr : &storage_[head_];
        }
    };
}   // namespace ktl

#endif   // _BIG_KTL_QUEUE_H
