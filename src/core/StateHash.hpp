#pragma once
#include <cstdint>
#include <cstring>

class PlayLayer;

namespace pf {

inline uint64_t hashMix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

template <class T>
inline uint64_t bitsOf(T v) {
    uint64_t r = 0;
    std::memcpy(&r, &v, sizeof(T) < sizeof(r) ? sizeof(T) : sizeof(r));
    return r;
}

// Hash of everything that decides the future of the run (tick, input, player physics state).
uint64_t hashGameState(PlayLayer* pl, bool held);

} // namespace pf
