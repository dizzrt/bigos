#ifndef _BIG_KTL_ALLOCATOR_H
#define _BIG_KTL_ALLOCATOR_H

#include <bigos/types.h>
#include <memory.h>
#include <new>   // IWYU pragma: keep

namespace ktl {
    template <typename _Tp>
    class allocator {
    public:
        typedef _Tp value_type;

        allocator() = default;

        _Tp *allocate(size_t __n = 1, gfm_t __gfm = 0) noexcept {
            if (__n == 0)
                return nullptr;
            return static_cast<_Tp *>(bigos::kmalloc(sizeof(_Tp) * __n, __gfm));
        }

        void deallocate(_Tp *__p, size_t = 1) noexcept {
            if (__p != nullptr)
                bigos::free(__p);
        }

        template <typename... _Args>
        bool construct(_Tp *__p, _Args &&...__args) noexcept {
            if (__p == nullptr)
                return false;
            new (__p) _Tp(static_cast<_Args &&>(__args)...);
            return true;
        }

        void destroy(_Tp *__p) noexcept {
            if (__p != nullptr)
                __p->~_Tp();
        }
    };
}   // namespace ktl

#endif   // _BIG_KTL_ALLOCATOR_H
