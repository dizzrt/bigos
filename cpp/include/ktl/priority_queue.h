#ifndef _BIG_KTL_PRIORITY_QUEUE_H
#define _BIG_KTL_PRIORITY_QUEUE_H

#include <ktl/algorithm.h>
#include <bigos/types.h>

namespace ktl {
    template <typename _Tp, typename _Compare = ktl::less<_Tp>>
    class priority_queue {
    private:
        _Tp *storage_;
        uint32_t cap_;
        uint32_t size_;
        _Compare comp_;

        bool higher(uint32_t __lhs, uint32_t __rhs) const {
            return comp_(storage_[__rhs], storage_[__lhs]);
        }

        void sift_up(uint32_t __index) {
            while (__index > 0) {
                uint32_t parent = (__index - 1) / 2;
                if (!higher(__index, parent))
                    break;
                ktl::swap(storage_[__index], storage_[parent]);
                __index = parent;
            }
        }

        void sift_down(uint32_t __index) {
            for (;;) {
                uint32_t left = __index * 2 + 1;
                uint32_t right = left + 1;
                uint32_t best = __index;
                if (left < size_ && higher(left, best))
                    best = left;
                if (right < size_ && higher(right, best))
                    best = right;
                if (best == __index)
                    break;
                ktl::swap(storage_[__index], storage_[best]);
                __index = best;
            }
        }

    public:
        priority_queue(_Tp *__storage, uint32_t __capacity, _Compare __comp = _Compare()) noexcept
            : storage_(__storage), cap_(__capacity), size_(0), comp_(__comp) {}
        priority_queue(const priority_queue &) = delete;
        priority_queue &operator=(const priority_queue &) = delete;

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
            storage_[size_] = __value;
            sift_up(size_);
            ++size_;
            return true;
        }

        bool pop(_Tp *__out = nullptr) noexcept {
            if (!valid() || empty())
                return false;
            if (__out != nullptr)
                *__out = storage_[0];
            --size_;
            if (size_ != 0) {
                storage_[0] = storage_[size_];
                sift_down(0);
            }
            return true;
        }

        _Tp *top() noexcept {
            return (!valid() || empty()) ? nullptr : &storage_[0];
        }
        const _Tp *top() const noexcept {
            return (!valid() || empty()) ? nullptr : &storage_[0];
        }
    };
}   // namespace ktl

#endif   // _BIG_KTL_PRIORITY_QUEUE_H
