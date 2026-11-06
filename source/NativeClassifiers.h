#pragma once
#include <cstddef>
#include <cstdint>

inline bool UnsafeNewLeagueId(int id) {
    return id <= 0 || id > 65535 || (id >= 382 && id <= 389) || id == 2000 || id == 2028;
}
inline constexpr std::uint32_t Native0[] = {13,16,19,31,53};
inline constexpr std::uint32_t Native1[] = {67,308,10,50,4};
inline constexpr std::uint32_t Native2[] = {80,1,189,56,17,20,32,54,14,39,7,341};
inline constexpr std::uint32_t Native3[] = {60,61,65,66,41,351,83,385,386,387,388,389};
inline constexpr const std::uint32_t* NativeGroups[] = {Native0,Native1,Native2,Native3};
inline constexpr std::size_t NativeCounts[] = {5,5,12,12};
inline int NativeClassifierGroup(int id) {
    for (int g=0;g<4;++g) for (std::size_t i=0;i<NativeCounts[g];++i)
        if (NativeGroups[g][i]==static_cast<std::uint32_t>(id)) return g;
    return -1;
}
