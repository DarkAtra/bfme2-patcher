#pragma once

#include <cstdint>

namespace patching {

    template<typename T>
    T read(const uintptr_t address) {
        return *reinterpret_cast<const T *>(address);
    }

    template<typename T>
    T field(const void *base, const uint32_t offset) {
        return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
    }
}
