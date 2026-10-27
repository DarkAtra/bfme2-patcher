#pragma once

#include <cstdint>

bool applyBroadPhaseTable();

namespace broad_phase_table {

    // the size of the game's broad phase object, and its first pair table (11131 buckets) right in front of it
    constexpr uint32_t OBJECT_SIZE = 0xC070;
    constexpr uint32_t ORIGINAL_TABLE = 0x18;
    constexpr uint32_t ORIGINAL_BUCKETS = 0x2B7B;

    // the bigger table the patch puts behind the object (a prime: the game hashes with a remainder), and the factor of the hash
    // `idLow * MULTIPLIER + idHigh` that replaces the game's `idLow * 0x10000 + idHigh`: 0x10000 is nearly a quarter of this table's
    // size (4 * 0x10000 = BUCKETS + 5), so the game's hash would put whole families of pairs into the same bucket; the multiplier
    // is the table size over the golden ratio, which spreads them evenly
    constexpr uint32_t BUCKETS = 262139;
    constexpr uint32_t MULTIPLIER = 162011;
    constexpr uint32_t TABLE = OBJECT_SIZE;
}
