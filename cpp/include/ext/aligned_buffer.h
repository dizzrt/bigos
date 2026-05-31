#ifndef _BIG_ALIGNED_BUFFER_H
#define _BIG_ALIGNED_BUFFER_H

#pragma GCC system_header

#include <type_traits>
#include <bigos/types.h>

namespace __gnu_cxx {
    template <typename _Tp>
    struct __aligned_membuf {
        struct _Tp2 {
            _Tp _t;
        };

        alignas(__alignof__(_Tp2::_t)) unsigned char _storage[sizeof(_Tp)];

        __aligned_membuf() = default;
        __aligned_membuf(nullptr_t) {}

        void *addr() noexcept {
            return static_cast<void *>(&_storage);
        }

        const void *addr() const noexcept {
            return static_cast<const void *>(&_storage);
        }

        _Tp *ptr() noexcept {
            return static_cast<_Tp *>(addr());
        }

        const _Tp *ptr() const noexcept {
            return static_cast<const _Tp *>(addr());
        }
    };

    template <typename _Tp>
    struct __aligned_buffer : std::aligned_storage<sizeof(_Tp), __alignof__(_Tp)> {
        typename std::aligned_storage<sizeof(_Tp), __alignof__(_Tp)>::type _storage;

        __aligned_buffer() = default;
        __aligned_buffer(nullptr_t) {}

        void *addr() noexcept {
            return static_cast<void *>(&_storage);
        }

        const void *addr() const noexcept {
            return static_cast<const void *>(&_storage);
        }

        _Tp *ptr() noexcept {
            return static_cast<_Tp *>(addr());
        }

        const _Tp *ptr() const noexcept {
            return static_cast<const _Tp *>(addr());
        }
    };

}   // namespace __gnu_cxx

#endif   // _BIG_ALIGNED_BUFFER_H
