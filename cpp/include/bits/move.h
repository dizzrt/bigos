#ifndef _BIG_MOVE_H
#define _BIG_MOVE_H

namespace std {
    template <typename _Tp>
    inline void swap(_Tp& __a, _Tp& __b) {
        _Tp __tmp = __a;
        __a = __b;
        __b = __tmp;
    }
}   // namespace std

#endif   // _BIG_MOVE_H
