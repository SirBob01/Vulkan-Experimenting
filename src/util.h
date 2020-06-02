#ifndef UTIL_H_
#define UTIL_H_
#include <algorithm>

template <typename T>
T clamp(T x, T min, T max) {
    return std::min(max, std::max(x, min));
}

inline int round_up(int val, int multiple) {
    if(multiple == 0) {
        return val;
    }
    int rem = val % multiple;
    if(rem == 0) {
        return val;
    }
    return val + (multiple - rem);
}

#endif