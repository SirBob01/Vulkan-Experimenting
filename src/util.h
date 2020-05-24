#include <algorithm>

template <typename T>
T clamp(T x, T min, T max) {
    return std::min(max, std::max(x, min));
}
