#ifndef _BIG_KTL_ALGORITHM_H
#define _BIG_KTL_ALGORITHM_H

namespace ktl {
    template <typename _Tp>
    struct less {
        constexpr bool operator()(const _Tp &__a, const _Tp &__b) const {
            return __a < __b;
        }
    };

    template <typename _Tp>
    inline void swap(_Tp &__a, _Tp &__b) {
        _Tp __tmp = __a;
        __a = __b;
        __b = __tmp;
    }

    template <typename _Tp>
    constexpr const _Tp &min(const _Tp &__a, const _Tp &__b) {
        return (__b < __a) ? __b : __a;
    }

    template <typename _Tp>
    constexpr const _Tp &max(const _Tp &__a, const _Tp &__b) {
        return (__a < __b) ? __b : __a;
    }
}   // namespace ktl

#endif   // _BIG_KTL_ALGORITHM_H
