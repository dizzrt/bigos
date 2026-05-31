#ifndef _BIG_PAIR_H
#define _BIG_PAIR_H

#include <stddef.h>

namespace ktl {
    template <typename _Tp1, typename _Tp2>
    struct pair {
        typedef _Tp1 first_type;
        typedef _Tp2 second_type;

        _Tp1 first;
        _Tp2 second;

        pair(const _Tp1& _first, const _Tp2& _second) : first(_first), second(_second) {}
        pair(const pair<_Tp1, _Tp2>& _pair) : pair(_pair.first, _pair.second) {}
        pair() : pair(_Tp1(), _Tp2()) {}
    };

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator==(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return x.first == y.first && x.second == y.second;
    }

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator!=(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return !(x == y);
    }

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator<(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return x.first < y.first || (!(y.first < x.first) && x.second < y.second);
    }

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator>(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return y < x;
    }

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator<=(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return !(y < x);
    }

    template <typename _Tp1, typename _Tp2>
    inline constexpr bool operator>=(const pair<_Tp1, _Tp2>& x, const pair<_Tp1, _Tp2>& y) {
        return !(x < y);
    }

    template <typename _Tp1, typename _Tp2>
    inline pair<_Tp1, _Tp2> make_pair(const _Tp1& x, const _Tp2& y) {
        return pair<_Tp1, _Tp2>(x, y);
    }
}   // namespace ktl

#endif   // _BIG_PAIR_H
