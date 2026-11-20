#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <emmintrin.h>

#include "animation_channels.h"
#include "patch_set.h"

// Animation channels
//
// Problem: posing animated models costs ~2 ms a frame in a big battle, half of it in evaluating the compressed animation
// channels: every bone of every model drawn asks its rotation and position channels for the current frame, ~23,000 bones a frame,
// each through a chain of six to nine calls.
// Fix: the scalar and quaternion channels of the first stream encoding are evaluated in one function each, with the same
// floating point operations in the same order, so the poses are the same to the bit.
//
// A channel (vtable 0x00BECD40) is evaluated at a frame (float) through slot 0x0C (scalar, 0x005B20AA) or slot 0x14 (quaternion,
// 0x005B226E): the frame is split into whole and fraction, the decoder (0x005B19C0, 0x005B1C00) produces the values at the
// whole frame and the next one, the scalar ones are blended linearly and the quaternions with an Nlerp (0x00B2B720), which
// normalises through an x87 inverse square root (0x00441C56). With the caller's cache (*cursor: the frame, then both values) the
// decoder starts from the cached frame and writes both values back; without it, it starts from the channel's initial values.
// This does the same work in one function:
// * The decoder: each step adds a 4 bit delta times the packet's scale to the previous value as an SSE single (cvtsi2ss, mulss,
//   addss), and the values are copied out with plain moves, except that the quaternion decoder copies them inside a packet through
//   the x87 stack, which quiets a signalling NaN; both are reproduced. A decode whose cache already holds the asked frame, inside
//   the channel, writes back what is in the cache and is skipped (tools/analysis/parked/animation_decode).
// * The Nlerp is scalar SSE and is done with the same operations in the same order. Its inverse square root (a bit trick and two
//   Newton steps) runs on the x87 stack: under the game's control word (24 bit precision, round to nearest, set by 0x00440809)
//   each step rounds as an SSE single does, as long as every value stays a normal float. So the SSE version is used when the
//   control word and MXCSR are the game's and the squared length and the components lie well inside the float range; otherwise
//   the game's Nlerp runs. A frame of 0xFFFFFFFF without a cache leaves the game's locals unwritten, so that goes to the game's
//   evaluation.
// The first encoding's vector channels (slot 0x10) are not used by the battle's animations and are left alone; the second
// encoding (vtable 0x00BECD5C) computes its scale with x87 multiplies and is left alone as well.

using namespace patching;

namespace {

    constexpr uintptr_t CHANNEL_VTABLE = 0x00BECD40;
    constexpr uintptr_t SCALAR_SLOT = CHANNEL_VTABLE + 0x0C;
    constexpr uintptr_t QUATERNION_SLOT = CHANNEL_VTABLE + 0x14;
    constexpr uintptr_t SCALAR_EVALUATE = 0x005B20AA;
    constexpr uintptr_t QUATERNION_EVALUATE = 0x005B226E;
    constexpr uintptr_t NLERP = 0x00B2B720;
    constexpr uintptr_t DELTA_SCALES = 0x00D9BCE0;
    constexpr uintptr_t ONE = 0x00BD1908;

    struct Channel {
        void *vtable;
        uint32_t type;
        uint32_t pivot;
        uint32_t count;
        uint32_t components;
        uint32_t scale;
        uint32_t initial[4];
        const uint8_t *data;
    };
    static_assert(offsetof(Channel, count) == 0x0C && offsetof(Channel, scale) == 0x14 && offsetof(Channel, initial) == 0x18
                  && offsetof(Channel, data) == 0x28, "channel layout");

    __m128 asFloat(const uint32_t bits) {
        return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(bits)));
    }

    uint32_t asBits(const __m128 value) {
        return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(value)));
    }

    // fld / fstp of a float: a signalling NaN comes back quiet
    uint32_t throughX87(uint32_t bits) {
        if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0) {
            bits |= 0x00400000u;
        }
        return bits;
    }

    template<size_t N, bool X87_COPIES>
    void decode(const Channel *channel, const uint32_t *state, uint32_t from, const uint32_t frame, uint32_t *out0, uint32_t *out1) {
        const auto *scales = reinterpret_cast<const uint32_t *>(DELTA_SCALES);
        uint32_t last[N];
        const uint32_t *start = state;
        if (from > frame) {
            from = 0;
            start = channel->initial;
        }
        for (size_t c = 0; c < N; c++) {
            last[c] = start[c];
        }
        const uint8_t *packet = channel->data + (from >> 4) * (9 * N);
        while (from <= frame + 1) {
            if (from >= channel->count) {
                for (size_t c = 0; c < N; c++) {
                    if (out0 != nullptr) {
                        out0[c] = last[c];
                    }
                    out1[c] = last[c];
                }
                return;
            }
            const uint32_t first = from & 0xF;
            from &= ~0xFu;
            for (size_t c = 0; c < N; c++, packet += 9) {
                const __m128 filter = _mm_mul_ss(asFloat(scales[packet[0]]), asFloat(channel->scale));
                const uint8_t *delta = packet + 1 + (first >> 1);
                __m128 value = asFloat(last[c]);
                for (uint32_t step = first; step < 16; step++) {
                    const uint32_t current = from + step;
                    if (current == frame) {
                        out0[c] = X87_COPIES ? throughX87(asBits(value)) : asBits(value);
                    } else if (current == frame + 1) {
                        out1[c] = X87_COPIES ? throughX87(asBits(value)) : asBits(value);
                        break;
                    }
                    const uint32_t odd = step & 1;
                    const int factor = odd != 0 ? static_cast<int8_t>(*delta) >> 4 : static_cast<int8_t>(static_cast<uint8_t>(*delta << 4)) >> 4;
                    delta += odd;
                    value = _mm_add_ss(_mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(), factor), filter), value);
                }
                last[c] = asBits(value);
            }
            from += 16;
            if (from > frame) {
                out0 = nullptr;
            }
        }
    }

    // the whole frame (cvttss2si) and the fraction left
    uint32_t split(const uint32_t frameBits, __m128 &fraction) {
        const __m128 frame = asFloat(frameBits);
        const int whole = _mm_cvttss_si32(frame);
        fraction = _mm_sub_ss(frame, _mm_cvtsi32_ss(_mm_setzero_ps(), whole));
        return static_cast<uint32_t>(whole);
    }

    bool isRepeat(const Channel *channel, const uint32_t cachedFrame, const uint32_t frame) {
        return cachedFrame == frame && frame < channel->count;
    }

    bool isNaN(const uint32_t bits) {
        return (bits & 0x7FFFFFFFu) > 0x7F800000u;
    }

    // With a NaN among an operation's operands, their order decides which payload comes out, and the compiler may swap those of
    // an add or a multiply. Without a NaN among the inputs (the frame, the scale, the initial and the cached values the decode
    // starts from; the delta scale table holds none) every NaN that arises is the default one, whatever the order.
    template<size_t N>
    bool hasNaN(const Channel *channel, const uint32_t frameBits, const uint32_t frame, const uint32_t *cache) {
        bool nan = isNaN(frameBits) || isNaN(channel->scale);
        for (size_t c = 0; c < N; c++) {
            nan |= isNaN(channel->initial[c]);
        }
        if (cache != nullptr && cache[0] <= frame) {
            for (size_t c = 0; c < 2 * N; c++) {
                nan |= isNaN(cache[1 + c]);
            }
        }
        return nan;
    }

    __m128 lerp(const uint32_t from, const uint32_t to, const __m128 fraction) {
        const __m128 start = asFloat(from);
        return _mm_add_ss(_mm_mul_ss(_mm_sub_ss(asFloat(to), start), fraction), start);
    }

    using Evaluate = void (__thiscall *)(const Channel *, uint32_t, uint32_t *, uint8_t **);

    void __stdcall evaluateScalar(const Channel *channel, const uint32_t frameBits, uint32_t *out, uint8_t **cursor) {
        __m128 fraction;
        const uint32_t frame = split(frameBits, fraction);
        const uint32_t *cached = cursor != nullptr ? reinterpret_cast<const uint32_t *>(*cursor) : nullptr;
        if ((cursor == nullptr && frame == 0xFFFFFFFFu) || hasNaN<1>(channel, frameBits, frame, cached)) {
            reinterpret_cast<Evaluate>(SCALAR_EVALUATE)(channel, frameBits, out, cursor);
            return;
        }
        if (cursor == nullptr) {
            uint32_t values[2];
            decode<1, false>(channel, channel->initial, 0, frame, &values[0], &values[1]);
            *out = asBits(lerp(values[0], values[1], fraction));
            return;
        }
        auto *cache = reinterpret_cast<uint32_t *>(*cursor);
        uint32_t *values = cache + 1;
        if (!isRepeat(channel, cache[0], frame)) {
            decode<1, false>(channel, values, cache[0], frame, &values[0], &values[1]);
        }
        cache[0] = frame;
        *out = asBits(lerp(values[0], values[1], fraction));
        *cursor += channel->components * 8 + 4;
    }

    bool insideNormalRange(const uint32_t bits) {
        const uint32_t exponent = (bits >> 23) & 0xFF;
        return exponent >= 127 - 60 && exponent <= 127 + 60;
    }

    void nlerp(uint32_t out[4], const uint32_t q0[4], const uint32_t q1[4], const __m128 t) {
        const uint32_t control = _mm_getcsr();
        uint16_t x87Control;
        __asm fnstcw x87Control
        if ((x87Control & 0x0F00) != 0 || (control & 0xE040) != 0) {
            reinterpret_cast<void (__cdecl *)(uint32_t *, const uint32_t *, const uint32_t *, uint32_t)>(NLERP)(out, q0, q1, asBits(t));
            return;
        }
        __m128 dot = _mm_mul_ss(asFloat(q0[0]), asFloat(q1[0]));
        dot = _mm_add_ss(dot, _mm_mul_ss(asFloat(q1[2]), asFloat(q0[2])));
        dot = _mm_add_ss(dot, _mm_mul_ss(asFloat(q1[1]), asFloat(q0[1])));
        dot = _mm_add_ss(dot, _mm_mul_ss(asFloat(q0[3]), asFloat(q1[3])));
        const __m128 rest = _mm_sub_ss(asFloat(*reinterpret_cast<const uint32_t *>(ONE)), t);
        // comiss 0, dot / jbe: the subtracting blend only when dot < 0 (ordered)
        const bool opposite = _mm_comigt_ss(_mm_setzero_ps(), dot) != 0;
        __m128 blended[4];
        for (int i = 0; i < 4; i++) {
            const __m128 a = _mm_mul_ss(asFloat(q0[i]), rest);
            const __m128 b = _mm_mul_ss(asFloat(q1[i]), t);
            blended[i] = opposite ? _mm_sub_ss(a, b) : _mm_add_ss(a, b);
        }
        __m128 length = _mm_add_ss(_mm_mul_ss(blended[0], blended[0]), _mm_mul_ss(blended[1], blended[1]));
        length = _mm_add_ss(length, _mm_mul_ss(blended[2], blended[2]));
        length = _mm_add_ss(length, _mm_mul_ss(blended[3], blended[3]));
        const uint32_t lengthBits = asBits(length);
        if ((lengthBits & 0x7FFFFFFFu) == 0) {
            for (int i = 0; i < 4; i++) {
                out[i] = asBits(blended[i]);
            }
            return;
        }
        bool inRange = (lengthBits & 0x80000000u) == 0 && insideNormalRange(lengthBits);
        for (int i = 0; i < 4 && inRange; i++) {
            const uint32_t bits = asBits(blended[i]);
            inRange = (bits & 0x7FFFFFFFu) == 0 || insideNormalRange(bits);
        }
        if (!inRange) {
            reinterpret_cast<void (__cdecl *)(uint32_t *, const uint32_t *, const uint32_t *, uint32_t)>(NLERP)(out, q0, q1, asBits(t));
            return;
        }
        // 0x00441C56: y0 from the bit trick, two Newton steps on the x87 stack, in the same order
        const __m128 half = asFloat(lengthBits - 0x00800000u);
        const __m128 y0 = asFloat((0xBE6EB508u - lengthBits) >> 1);
        const __m128 threeHalves = asFloat(0x3FC00000u);
        const __m128 b = _mm_mul_ss(_mm_mul_ss(y0, y0), half);
        const __m128 k1 = _mm_sub_ss(threeHalves, b);
        const __m128 c1 = _mm_mul_ss(_mm_mul_ss(b, k1), k1);
        const __m128 y1 = _mm_mul_ss(y0, k1);
        const __m128 k2 = _mm_sub_ss(threeHalves, c1);
        const __m128 c2 = _mm_mul_ss(_mm_mul_ss(c1, k2), k2);
        const __m128 y2 = _mm_mul_ss(y1, k2);
        const __m128 k3 = _mm_sub_ss(threeHalves, c2);
        const __m128 inverse = _mm_mul_ss(y2, k3);
        for (int i = 0; i < 4; i++) {
            out[i] = asBits(_mm_mul_ss(inverse, blended[i]));
        }
    }

    void __stdcall evaluateQuaternion(const Channel *channel, const uint32_t frameBits, uint32_t *out, uint8_t **cursor) {
        __m128 fraction;
        const uint32_t frame = split(frameBits, fraction);
        const uint32_t *cached = cursor != nullptr ? reinterpret_cast<const uint32_t *>(*cursor) : nullptr;
        if ((cursor == nullptr && frame == 0xFFFFFFFFu) || hasNaN<4>(channel, frameBits, frame, cached)) {
            reinterpret_cast<Evaluate>(QUATERNION_EVALUATE)(channel, frameBits, out, cursor);
            return;
        }
        if (cursor == nullptr) {
            uint32_t values[8];
            decode<4, true>(channel, channel->initial, 0, frame, &values[0], &values[4]);
            nlerp(out, &values[0], &values[4], fraction);
            return;
        }
        auto *cache = reinterpret_cast<uint32_t *>(*cursor);
        uint32_t *values = cache + 1;
        if (!isRepeat(channel, cache[0], frame)) {
            decode<4, true>(channel, values, cache[0], frame, &values[0], &values[4]);
        }
        cache[0] = frame;
        nlerp(out, &values[0], &values[4], fraction);
        *cursor += channel->components * 8 + 4;
    }

    // thiscall (this, frame, out, cursor), ret 0xC; the game's chains leave xmm3-xmm7 (scalar) and xmm6-xmm7 (quaternion) alone,
    // so this keeps xmm3-xmm7 for both
    uintptr_t emitEntry(Cave &cave, const uintptr_t evaluate) {
        const uintptr_t entry = cave.here();
        const BYTE reserve[3] = {0x83, 0xEC, 0x50}; // sub esp, 0x50
        cave.emit(reserve);
        for (BYTE xmm = 3; xmm < 8; xmm++) {
            const BYTE save[5] = {0x0F, 0x11, static_cast<BYTE>(0x44 | (xmm << 3)), 0x24, static_cast<BYTE>((xmm - 3) * 16)}; // movups [esp + n], xmm
            cave.emit(save);
        }
        const BYTE pushArgument[4] = {0xFF, 0x74, 0x24, 0x5C}; // push [esp + 0x5C]: cursor, then out, then frame
        cave.emit(pushArgument);
        cave.emit(pushArgument);
        cave.emit(pushArgument);
        cave.emit8(0x51); // push ecx: the channel
        cave.call(evaluate);
        for (BYTE xmm = 3; xmm < 8; xmm++) {
            const BYTE load[5] = {0x0F, 0x10, static_cast<BYTE>(0x44 | (xmm << 3)), 0x24, static_cast<BYTE>((xmm - 3) * 16)}; // movups xmm, [esp + n]
            cave.emit(load);
        }
        const BYTE release[3] = {0x83, 0xC4, 0x50}; // add esp, 0x50
        cave.emit(release);
        const BYTE ret[3] = {0xC2, 0x0C, 0x00};
        cave.emit(ret);
        return entry;
    }
}

bool applyAnimationChannels() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);
    static PatchSet set;
    set.reset();

    const uintptr_t scalarEntry = emitEntry(cave, reinterpret_cast<uintptr_t>(&evaluateScalar));
    const uintptr_t quaternionEntry = emitEntry(cave, reinterpret_cast<uintptr_t>(&evaluateQuaternion));
    set.replaceSlot(SCALAR_SLOT, SCALAR_EVALUATE, scalarEntry);
    set.replaceSlot(QUATERNION_SLOT, QUATERNION_EVALUATE, quaternionEntry);

    // the evaluations' cached calls: push out1 / push out0 / push frame / push [cache frame] / push state (= out0) / call decoder
    const BYTE scalarCall[14] = {0x52, 0x57, 0x50, 0xFF, 0x36, 0x89, 0x55, 0x10, 0x57, 0xE8, 0xD5, 0xF8, 0xFF, 0xFF};
    set.expect(0x005B20DD, scalarCall);
    const BYTE quaternionCall[11] = {0x50, 0x53, 0x52, 0xFF, 0x36, 0x53, 0xE8, 0x53, 0xF9, 0xFF, 0xFF};
    set.expect(0x005B22A2, quaternionCall);
    // the quaternion evaluation's Nlerp call (cdecl: out, q0, q1, fraction)
    const BYTE nlerpCall[5] = {0xE8, 0x5A, 0x94, 0x57, 0x00};
    set.expect(0x005B22C1, nlerpCall);
    // the decoders' delta step: cvtsi2ss / mulss / addss (scalar 0x005B1A6A, quaternion 0x005B1CDC)
    const BYTE scalarStep[12] = {0xF3, 0x0F, 0x2A, 0xD2, 0xF3, 0x0F, 0x59, 0xD0, 0xF3, 0x0F, 0x58, 0xD1};
    set.expect(0x005B1A6A, scalarStep);
    const BYTE quaternionStep[12] = {0xF3, 0x0F, 0x2A, 0xCB, 0xF3, 0x0F, 0x59, 0xC8, 0xF3, 0x0F, 0x58, 0x0A};
    set.expect(0x005B1CDC, quaternionStep);
    // the inverse square root's bit trick and its 1.5
    const BYTE inverseSqrt[13] = {0xB8, 0x08, 0xB5, 0x6E, 0xBE, 0xC7, 0x44, 0x24, 0xF4, 0x00, 0x00, 0xC0, 0x3F};
    set.expect(0x00441C56, inverseSqrt);
    // the Nlerp's call of it
    const BYTE inverseSqrtCall[5] = {0xE8, 0xDB, 0x63, 0x91, 0xFF};
    set.expect(0x00B2B876, inverseSqrtCall);
    // the delta scale table (powers of ten) and the Nlerp's 1.0
    const BYTE scales[8] = {0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x20, 0x41};
    set.expect(DELTA_SCALES + 8 * 4, scales);
    const BYTE one[4] = {0x00, 0x00, 0x80, 0x3F};
    set.expect(ONE, one);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
