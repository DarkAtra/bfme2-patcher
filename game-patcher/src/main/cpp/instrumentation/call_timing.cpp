#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "instrumentation/call_timing.h"
#include "instrumentation/stats.h"
#include "patch_set.h"

// Call timers (debug mode): times what a frame of the game is made of: the calls the engine frame, the logic update (0x0062E4E8)
// and the client update (0x0064849E) make, one entry per call site and per vtable (so the update modules the logic update calls through one site
// are told apart by their class), and the game's own render scopes (PerfScope, constructor 0x00517690, destructor 0x00517740),
// one entry per place that opens one. A timed call site is replaced by a jump to a cave that runs the instructions it displaced,
// notes the time, makes the call itself and notes the time again; the call sees the registers and the stack it would have seen.
// The game thread's file reads (msvcr71 imports) and the Direct3D device's resource creation are timed as well, so that a slow
// frame shows whether it loaded something.
//
// The Direct3D device and D3DX effect vtables only exist once the game renders, so their slots are replaced during play when the
// device and the effect call sites first see them (hookDevice, hookEffect), not through a PatchSet: the offline test does not see
// them and the crash dump's patch list does not name them.

using namespace instrumentation;
using namespace patching;

namespace {

    constexpr size_t MAX_DEPTH = 64;
    constexpr size_t MAX_SITES = 512;
    constexpr size_t INDEX_SLOTS = 4096;
    constexpr uint32_t NO_ENTRY = 0xFFFFFFFF;

    struct Site {
        uint32_t address;
        uint32_t group;
        const char *label;
        // keyed by the innermost open render scope instead of the vtable or target, to tell the render passes apart
        bool byScope;
        // keyed by the vtable, with bit 0 set inside the UpdateShadowMap scope (vtables are 4-byte aligned)
        bool vtableByShadow;
        // keyed by the vtable of the State in ecx and named with its id (State + 4), for direct calls too
        bool byState;
    };

    struct Open {
        uint32_t entry;
        // the site number + 1 for a timed call, the PerfScope object for a render scope
        uint32_t tag;
        uint64_t start;
    };

    Site sites[MAX_SITES];
    uint32_t siteCount = 0;
    Open opened[MAX_DEPTH];
    size_t depth = 0;
    uint32_t mainThread = 0;
    // entry + 1 by hash of (site, key)
    uint16_t index[INDEX_SLOTS];

    void copyName(char (&name)[CALL_NAME], const char *source) {
        size_t i = 0;
        __try {
            for (; source != nullptr && i < CALL_NAME - 1 && source[i] != 0; i++) {
                name[i] = source[i];
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        name[i] = 0;
    }

    uint32_t find(CallTimes &times, const uint32_t site, const uint32_t key, const uint32_t target, const uint32_t group, const char *name) {
        size_t slot = ((site * 2654435761u) ^ (key * 40503u)) & (INDEX_SLOTS - 1);
        for (size_t probe = 0; probe < INDEX_SLOTS; probe++, slot = (slot + 1) & (INDEX_SLOTS - 1)) {
            if (index[slot] == 0) {
                if (times.count == CALL_ENTRIES) {
                    return NO_ENTRY;
                }
                const uint32_t entry = times.count;
                CallStat &stat = times.entries[entry];
                stat.site = site;
                stat.key = key;
                stat.target = target;
                stat.group = group;
                copyName(stat.name, name);
                index[slot] = static_cast<uint16_t>(entry + 1);
                times.count = entry + 1;
                return entry;
            }
            const CallStat &stat = times.entries[index[slot] - 1];
            if (stat.site == site && stat.key == key) {
                return index[slot] - 1;
            }
        }
        return NO_ENTRY;
    }

    void add(uint32_t &low, uint32_t &high, const uint64_t cycles) {
        const uint64_t total = ((static_cast<uint64_t>(high) << 32) | low) + cycles;
        low = static_cast<uint32_t>(total);
        high = static_cast<uint32_t>(total >> 32);
    }

    void open(const uint32_t site, const uint32_t key, const uint32_t target, const uint32_t group, const char *name, const uint32_t tag) {
        Stats *statistics = stats();
        if (statistics == nullptr || __readfsdword(0x24) != mainThread) {
            return;
        }
        if (depth == MAX_DEPTH) {
            statistics->callTimes.lost++;
            return;
        }
        const uint32_t entry = find(statistics->callTimes, site, key, target, group, name);
        if (entry == NO_ENTRY) {
            statistics->callTimes.lost++;
        }
        opened[depth++] = {entry, tag, __rdtsc()};
    }

    // Closes the innermost open entry with the tag and returns its time (0 if nothing was open). Entries above it were left by a call
    // that never returned normally (an exception unwound through it) and are dropped.
    uint64_t close(const uint32_t tag) {
        const uint64_t end = __rdtsc();
        Stats *statistics = stats();
        if (statistics == nullptr || __readfsdword(0x24) != mainThread) {
            return 0;
        }
        size_t at = depth;
        while (at > 0 && opened[at - 1].tag != tag) {
            at--;
        }
        if (at == 0) {
            return 0;
        }
        depth = at - 1;
        const Open &closed = opened[depth];
        if (closed.entry == NO_ENTRY) {
            return 0;
        }
        const uint64_t cycles = end - closed.start;
        CallStat &stat = statistics->callTimes.entries[closed.entry];
        stat.calls++;
        add(stat.cyclesLow, stat.cyclesHigh, cycles);
        const uint32_t narrow = cycles > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(cycles);
        if (narrow > stat.maxCycles) {
            stat.maxCycles = narrow;
        }
        if (depth > 0 && opened[depth - 1].entry != NO_ENTRY) {
            CallStat &parent = statistics->callTimes.entries[opened[depth - 1].entry];
            add(parent.childLow, parent.childHigh, cycles);
        }
        return cycles;
    }

    constexpr uintptr_t PRESENT_SITE = 0x00522649;
    uintptr_t presentSite = PRESENT_SITE;
    uint64_t loadedAt = 0;

    void logPresentStall(const uint64_t cycles) {
        Stats *statistics = stats();
        if (cycles < static_cast<uint64_t>(statistics->tscKHz) * 30) {
            return;
        }
        PresentStall &stall = statistics->presentStalls.stalls[statistics->presentStalls.count % PRESENT_STALLS];
        const uint64_t end = __rdtsc() - loadedAt;
        stall.endLow = static_cast<uint32_t>(end);
        stall.endHigh = static_cast<uint32_t>(end >> 32);
        stall.cycles = cycles > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(cycles);
        statistics->presentStalls.count++;
    }

    void hookDevice(uint32_t vtable);
    void hookEffect(uint32_t vtable);
    extern uintptr_t deviceSite;
    extern uintptr_t effectSite;

    // the place that opened the innermost render scope that is open, 0 if none
    uint32_t innermostScope() {
        const Stats *statistics = stats();
        for (size_t at = depth; at > 0; at--) {
            const uint32_t entry = opened[at - 1].entry;
            if (entry != NO_ENTRY && statistics->callTimes.entries[entry].group == CALL_RENDER_SCOPE) {
                return statistics->callTimes.entries[entry].site;
            }
        }
        return 0;
    }

    bool inShadowScope() {
        const Stats *statistics = stats();
        for (size_t at = depth; at > 0; at--) {
            const uint32_t entry = opened[at - 1].entry;
            if (entry != NO_ENTRY && statistics->callTimes.entries[entry].group == CALL_RENDER_SCOPE) {
                return strcmp(statistics->callTimes.entries[entry].name, "UpdateShadowMap") == 0;
            }
        }
        return false;
    }

    bool readState(const uint32_t object, uint32_t &vtable, uint32_t &id) {
        __try {
            vtable = reinterpret_cast<const uint32_t *>(object)[0];
            id = reinterpret_cast<const uint32_t *>(object)[1];
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // "<label> id <id>", cut to the entry's name length
    void stateName(char (&name)[CALL_NAME], const char *label, uint32_t id) {
        size_t at = 0;
        for (; label[at] != 0 && at < CALL_NAME - 15; at++) {
            name[at] = label[at];
        }
        const char infix[] = " id ";
        for (size_t i = 0; infix[i] != 0; i++) {
            name[at++] = infix[i];
        }
        char digits[10];
        size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + id % 10);
            id /= 10;
        } while (id != 0);
        while (count > 0) {
            name[at++] = digits[--count];
        }
        name[at] = 0;
    }

    void __cdecl callBegin(const uint32_t site, const uint32_t key, const uint32_t target, const uint32_t object) {
        if (sites[site].address == deviceSite) {
            hookDevice(key);
        } else if (sites[site].address == effectSite) {
            hookEffect(key);
        }
        uint32_t entryKey = key;
        const char *label = sites[site].label;
        char name[CALL_NAME];
        if (__readfsdword(0x24) == mainThread) {
            uint32_t vtable;
            uint32_t id;
            if (sites[site].byScope) {
                entryKey = innermostScope();
            } else if (sites[site].vtableByShadow && inShadowScope()) {
                entryKey = key | 1;
            } else if (sites[site].byState && readState(object, vtable, id)) {
                entryKey = vtable;
                stateName(name, label, id);
                label = name;
            }
        }
        open(sites[site].address, entryKey, target, sites[site].group, label, site + 1);
    }

    void __cdecl callEnd(const uint32_t site) {
        const uint64_t cycles = close(site + 1);
        if (sites[site].address == presentSite && cycles != 0) {
            logPresentStall(cycles);
        }
    }

    void __cdecl scopeBegin(const uint32_t scope, const uint32_t returnAddress, const char *name) {
        open(returnAddress, 0, 0, CALL_RENDER_SCOPE, name, scope);
    }

    void __cdecl scopeEnd(const uint32_t scope) {
        close(scope);
    }

    // ---- files and Direct3D resources: wrappers that open an entry keyed by the import slot or the vtable slot. Their tags are
    // above every stack address and site number.

    constexpr uint32_t RESOURCE_TAG = 0xF0000000;

    struct Timed {
        const uint32_t tag;

        Timed(const uint32_t site, const char *name, const uint32_t index) : tag(RESOURCE_TAG + index) {
            open(site, 0, 0, CALL_RESOURCE, name, tag);
        }

        ~Timed() {
            close(tag);
        }
    };

    using ReadFunction = int (__cdecl *)(int, void *, unsigned);
    using SeekFunction = long (__cdecl *)(int, long, int);
    using OpenFunction = int (__cdecl *)(const char *, int, int);
    using WideOpenFunction = int (__cdecl *)(const wchar_t *, int, int);

    constexpr uintptr_t READ_SLOT = 0x00BD0414;
    constexpr uintptr_t SEEK_SLOT = 0x00BD0418;
    constexpr uintptr_t OPEN_SLOT = 0x00BD0404;
    constexpr uintptr_t WIDE_OPEN_SLOT = 0x00BD0410;

    ReadFunction originalRead;
    SeekFunction originalSeek;
    OpenFunction originalOpen;
    WideOpenFunction originalWideOpen;

    int __cdecl timedRead(const int descriptor, void *buffer, const unsigned size) {
        Timed timed(READ_SLOT, "file _read", 0);
        return originalRead(descriptor, buffer, size);
    }

    long __cdecl timedSeek(const int descriptor, const long offset, const int origin) {
        Timed timed(SEEK_SLOT, "file _lseek", 1);
        return originalSeek(descriptor, offset, origin);
    }

    int __cdecl timedOpen(const char *name, const int flags, const int mode) {
        Timed timed(OPEN_SLOT, "file _open", 2);
        return originalOpen(name, flags, mode);
    }

    int __cdecl timedWideOpen(const wchar_t *name, const int flags, const int mode) {
        Timed timed(WIDE_OPEN_SLOT, "file _wopen", 3);
        return originalWideOpen(name, flags, mode);
    }

    // IDirect3DDevice9 vtable slots
    constexpr size_t CREATE_TEXTURE = 23;
    constexpr size_t CREATE_VERTEX_BUFFER = 26;
    constexpr size_t CREATE_INDEX_BUFFER = 27;
    constexpr size_t UPDATE_TEXTURE = 31;

    using CreateTextureFunction = HRESULT (__stdcall *)(void *, UINT, UINT, UINT, DWORD, DWORD, DWORD, void **, void *);
    using CreateBufferFunction = HRESULT (__stdcall *)(void *, UINT, DWORD, DWORD, DWORD, void **, void *);
    using UpdateTextureFunction = HRESULT (__stdcall *)(void *, void *, void *);

    CreateTextureFunction originalCreateTexture;
    CreateBufferFunction originalCreateVertexBuffer;
    CreateBufferFunction originalCreateIndexBuffer;
    UpdateTextureFunction originalUpdateTexture;
    uint32_t hookedDeviceVtable = 0;

    HRESULT __stdcall timedCreateTexture(void *device, const UINT width, const UINT height, const UINT levels, const DWORD usage, const DWORD format,
                                         const DWORD pool, void **texture, void *shared) {
        Timed timed(hookedDeviceVtable + 4 * CREATE_TEXTURE, "d3d CreateTexture", 4);
        return originalCreateTexture(device, width, height, levels, usage, format, pool, texture, shared);
    }

    HRESULT __stdcall timedCreateVertexBuffer(void *device, const UINT length, const DWORD usage, const DWORD format, const DWORD pool, void **buffer,
                                              void *shared) {
        Timed timed(hookedDeviceVtable + 4 * CREATE_VERTEX_BUFFER, "d3d CreateVertexBuffer", 5);
        return originalCreateVertexBuffer(device, length, usage, format, pool, buffer, shared);
    }

    HRESULT __stdcall timedCreateIndexBuffer(void *device, const UINT length, const DWORD usage, const DWORD format, const DWORD pool, void **buffer,
                                             void *shared) {
        Timed timed(hookedDeviceVtable + 4 * CREATE_INDEX_BUFFER, "d3d CreateIndexBuffer", 6);
        return originalCreateIndexBuffer(device, length, usage, format, pool, buffer, shared);
    }

    HRESULT __stdcall timedUpdateTexture(void *device, void *source, void *destination) {
        Timed timed(hookedDeviceVtable + 4 * UPDATE_TEXTURE, "d3d UpdateTexture", 7);
        return originalUpdateTexture(device, source, destination);
    }

    bool replaceSlot(uint32_t *slot, void *wrapper, void *original) {
        DWORD previous;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) {
            return false;
        }
        *static_cast<uint32_t *>(original) = *slot;
        *slot = reinterpret_cast<uint32_t>(wrapper);
        DWORD ignored;
        VirtualProtect(slot, sizeof(*slot), previous, &ignored);
        return true;
    }

    // ---- the device's shader and shader constant writes, counted by the D3DX effect call they happen in (effectContext: 0 none,
    // 1 Begin, 2 BeginPass, 3 CommitChanges, 4 EndPass, 5 End; set by the effect wrappers further down)

    constexpr size_t DEVICE_WRITE_SLOTS[DEVICE_WRITES] = {65, 92, 94, 96, 98, 107, 109, 111, 113};
    uint32_t originalWrites[DEVICE_WRITES];
    uint32_t effectContext = 0;

    void noteWrite(const size_t write, const uint32_t returnAddress) {
        Stats *statistics = stats();
        if (statistics == nullptr || __readfsdword(0x24) != mainThread) {
            return;
        }
        DeviceWrites &writes = statistics->deviceWrites;
        writes.calls[write][effectContext]++;
        if (effectContext != 0) {
            return;
        }
        for (DeviceCaller &caller : writes.callers) {
            if (caller.address == returnAddress && caller.write == write) {
                caller.calls++;
                return;
            }
            if (caller.address == 0) {
                caller = {returnAddress, static_cast<uint32_t>(write), 1};
                return;
            }
        }
        writes.otherCallers++;
    }

    template<size_t WRITE>
    HRESULT __stdcall timedWrite2(void *device, void *a) {
        noteWrite(WRITE, reinterpret_cast<uint32_t>(_ReturnAddress()));
        return reinterpret_cast<HRESULT (__stdcall *)(void *, void *)>(originalWrites[WRITE])(device, a);
    }

    template<size_t WRITE>
    HRESULT __stdcall timedWrite3(void *device, uint32_t a, void *b) {
        noteWrite(WRITE, reinterpret_cast<uint32_t>(_ReturnAddress()));
        return reinterpret_cast<HRESULT (__stdcall *)(void *, uint32_t, void *)>(originalWrites[WRITE])(device, a, b);
    }

    template<size_t WRITE>
    HRESULT __stdcall timedWrite4(void *device, uint32_t a, const void *b, uint32_t c) {
        noteWrite(WRITE, reinterpret_cast<uint32_t>(_ReturnAddress()));
        return reinterpret_cast<HRESULT (__stdcall *)(void *, uint32_t, const void *, uint32_t)>(originalWrites[WRITE])(device, a, b, c);
    }

    // The device only exists once the game has set up rendering, so its vtable is patched when the end scene call site first sees
    // it (the vtable is the key that site records).
    void hookDevice(const uint32_t vtable) {
        if (hookedDeviceVtable != 0 || vtable == 0) {
            return;
        }
        hookedDeviceVtable = vtable;
        auto *slots = reinterpret_cast<uint32_t *>(vtable);
        replaceSlot(&slots[CREATE_TEXTURE], reinterpret_cast<void *>(&timedCreateTexture), &originalCreateTexture);
        replaceSlot(&slots[CREATE_VERTEX_BUFFER], reinterpret_cast<void *>(&timedCreateVertexBuffer), &originalCreateVertexBuffer);
        replaceSlot(&slots[CREATE_INDEX_BUFFER], reinterpret_cast<void *>(&timedCreateIndexBuffer), &originalCreateIndexBuffer);
        replaceSlot(&slots[UPDATE_TEXTURE], reinterpret_cast<void *>(&timedUpdateTexture), &originalUpdateTexture);
        void *const writers[DEVICE_WRITES] = {
            reinterpret_cast<void *>(&timedWrite3<0>), reinterpret_cast<void *>(&timedWrite2<1>), reinterpret_cast<void *>(&timedWrite4<2>),
            reinterpret_cast<void *>(&timedWrite4<3>), reinterpret_cast<void *>(&timedWrite4<4>), reinterpret_cast<void *>(&timedWrite2<5>),
            reinterpret_cast<void *>(&timedWrite4<6>), reinterpret_cast<void *>(&timedWrite4<7>), reinterpret_cast<void *>(&timedWrite4<8>),
        };
        for (size_t i = 0; i < DEVICE_WRITES; i++) {
            replaceSlot(&slots[DEVICE_WRITE_SLOTS[i]], writers[i], &originalWrites[i]);
        }
    }

    constexpr uintptr_t END_SCENE_SITE = 0x00522625;
    uintptr_t deviceSite = END_SCENE_SITE;

    // ---- D3DX effect parameter setters: ID3DXEffect vtable slots (ID3DXBaseEffect's setters). The effects only exist once the game
    // has loaded its shaders, so the vtable is patched when the CommitChanges call site (ID3DXEffect slot 65) first sees it.

    constexpr size_t EFFECT_SLOTS[EFFECT_SETTERS] = {20, 22, 26, 30, 34, 36, 38, 40, 44, 52};
    enum EffectSetterIndex : size_t {
        SET_VALUE, SET_BOOL, SET_INT, SET_FLOAT, SET_VECTOR, SET_VECTOR_ARRAY, SET_MATRIX, SET_MATRIX_ARRAY, SET_MATRIX_TRANSPOSE, SET_TEXTURE
    };
    uint32_t originalSetters[EFFECT_SETTERS];

    // the value each effect parameter was last set to, as a hash of the setter and the bytes it passed
    struct LastValue {
        uint32_t effect;
        uint32_t handle;
        uint64_t hash;
    };

    constexpr size_t LAST_VALUES = 8192;
    LastValue lastValues[LAST_VALUES];

    uint64_t hashBytes(const size_t setter, const void *data, const size_t bytes) {
        uint64_t hash = 14695981039346656037ULL ^ setter;
        const auto *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < bytes; i++) {
            hash = (hash ^ p[i]) * 1099511628211ULL;
        }
        return hash;
    }

    bool sameAsLast(const size_t setter, const uint32_t effect, const uint32_t handle, const void *data, const size_t bytes) {
        const uint64_t hash = hashBytes(setter, data, bytes);
        const size_t start = ((effect >> 4) * 2654435761u ^ handle * 40503u) & (LAST_VALUES - 1);
        LastValue *free = nullptr;
        for (size_t probe = 0; probe < 8; probe++) {
            LastValue &candidate = lastValues[(start + probe) & (LAST_VALUES - 1)];
            if (candidate.effect == effect && candidate.handle == handle) {
                const bool same = candidate.hash == hash;
                candidate.hash = hash;
                return same;
            }
            if (candidate.effect == 0 && free == nullptr) {
                free = &candidate;
            }
        }
        LastValue &slot = free != nullptr ? *free : lastValues[start];
        slot = {effect, handle, hash};
        return false;
    }

    struct EffectSet {
        EffectSetter *stat = nullptr;
        uint64_t start = 0;

        EffectSet(const size_t setter, void *effect, const uint32_t handle, const void *data, const size_t bytes) {
            Stats *statistics = stats();
            if (statistics == nullptr || __readfsdword(0x24) != mainThread) {
                return;
            }
            stat = &statistics->effectStats.setters[setter];
            stat->calls++;
            if (sameAsLast(setter, reinterpret_cast<uint32_t>(effect), handle, data, bytes)) {
                stat->same++;
            }
            start = __rdtsc();
        }

        ~EffectSet() {
            if (stat != nullptr) {
                add(stat->cyclesLow, stat->cyclesHigh, __rdtsc() - start);
            }
        }
    };

    template<typename Function>
    Function original(const size_t setter) {
        return reinterpret_cast<Function>(originalSetters[setter]);
    }

    using SetValueFunction = HRESULT (__stdcall *)(void *, uint32_t, const void *, UINT);
    using SetWordFunction = HRESULT (__stdcall *)(void *, uint32_t, uint32_t);
    using SetPointerFunction = HRESULT (__stdcall *)(void *, uint32_t, const void *);
    using SetArrayFunction = HRESULT (__stdcall *)(void *, uint32_t, const void *, UINT);

    HRESULT __stdcall timedSetValue(void *effect, const uint32_t handle, const void *data, const UINT bytes) {
        // a null value is D3DX's to refuse, not the wrapper's to read
        EffectSet set(SET_VALUE, effect, handle, data, data != nullptr ? bytes : 0);
        return original<SetValueFunction>(SET_VALUE)(effect, handle, data, bytes);
    }

    template<size_t SETTER>
    HRESULT __stdcall timedSetWord(void *effect, const uint32_t handle, const uint32_t value) {
        EffectSet set(SETTER, effect, handle, &value, sizeof(value));
        return original<SetWordFunction>(SETTER)(effect, handle, value);
    }

    template<size_t SETTER, size_t BYTES>
    HRESULT __stdcall timedSetPointer(void *effect, const uint32_t handle, const void *value) {
        EffectSet set(SETTER, effect, handle, value, value != nullptr ? BYTES : 0);
        return original<SetPointerFunction>(SETTER)(effect, handle, value);
    }

    template<size_t SETTER, size_t BYTES>
    HRESULT __stdcall timedSetArray(void *effect, const uint32_t handle, const void *values, const UINT count) {
        EffectSet set(SETTER, effect, handle, values, values != nullptr ? BYTES * count : 0);
        return original<SetArrayFunction>(SETTER)(effect, handle, values, count);
    }

    // ID3DXEffect::Begin (63), BeginPass (64), CommitChanges (65), EndPass (66), End (67): the device writes they make are counted
    // under them
    constexpr size_t EFFECT_CALLS = 5;
    constexpr size_t EFFECT_CALL_SLOTS[EFFECT_CALLS] = {63, 64, 65, 66, 67};
    uint32_t originalEffectCalls[EFFECT_CALLS];
    // in the process, not in the statistics block: a second game on the machine finds the block taken (instrumentation.cpp)
    uint32_t hookedEffectVtable = 0;

    struct InEffectCall {
        const uint32_t previous;

        explicit InEffectCall(const uint32_t context) : previous(effectContext) {
            effectContext = context;
        }

        ~InEffectCall() {
            effectContext = previous;
        }
    };

    HRESULT __stdcall inBegin(void *effect, UINT *passes, const DWORD flags) {
        InEffectCall in(1);
        return reinterpret_cast<HRESULT (__stdcall *)(void *, UINT *, DWORD)>(originalEffectCalls[0])(effect, passes, flags);
    }

    HRESULT __stdcall inBeginPass(void *effect, const UINT pass) {
        InEffectCall in(2);
        return reinterpret_cast<HRESULT (__stdcall *)(void *, UINT)>(originalEffectCalls[1])(effect, pass);
    }

    // CommitChanges, EndPass and End
    template<size_t CALL>
    HRESULT __stdcall inEffectCall(void *effect) {
        InEffectCall in(CALL + 1);
        return reinterpret_cast<HRESULT (__stdcall *)(void *)>(originalEffectCalls[CALL])(effect);
    }

    void hookEffect(const uint32_t vtable) {
        Stats *statistics = stats();
        if (statistics == nullptr || hookedEffectVtable != 0 || vtable == 0) {
            return;
        }
        void *const calls[EFFECT_CALLS] = {
            reinterpret_cast<void *>(&inBegin), reinterpret_cast<void *>(&inBeginPass), reinterpret_cast<void *>(&inEffectCall<2>),
            reinterpret_cast<void *>(&inEffectCall<3>), reinterpret_cast<void *>(&inEffectCall<4>),
        };
        for (size_t i = 0; i < EFFECT_CALLS; i++) {
            replaceSlot(&reinterpret_cast<uint32_t *>(vtable)[EFFECT_CALL_SLOTS[i]], calls[i], &originalEffectCalls[i]);
        }
        hookedEffectVtable = vtable;
        statistics->effectStats.hookedVtable = vtable;
        void *const wrappers[EFFECT_SETTERS] = {
            reinterpret_cast<void *>(&timedSetValue),
            reinterpret_cast<void *>(&timedSetWord<SET_BOOL>),
            reinterpret_cast<void *>(&timedSetWord<SET_INT>),
            reinterpret_cast<void *>(&timedSetWord<SET_FLOAT>),
            reinterpret_cast<void *>(&timedSetPointer<SET_VECTOR, 16>),
            reinterpret_cast<void *>(&timedSetArray<SET_VECTOR_ARRAY, 16>),
            reinterpret_cast<void *>(&timedSetPointer<SET_MATRIX, 64>),
            reinterpret_cast<void *>(&timedSetArray<SET_MATRIX_ARRAY, 64>),
            reinterpret_cast<void *>(&timedSetPointer<SET_MATRIX_TRANSPOSE, 64>),
            reinterpret_cast<void *>(&timedSetWord<SET_TEXTURE>),
        };
        auto *slots = reinterpret_cast<uint32_t *>(vtable);
        for (size_t i = 0; i < EFFECT_SETTERS; i++) {
            replaceSlot(&slots[EFFECT_SLOTS[i]], wrappers[i], &originalSetters[i]);
        }
    }

    constexpr uintptr_t COMMIT_SITE = 0x00551695;
    uintptr_t effectSite = COMMIT_SITE;

    // `window` is a call instruction (`call rel32`, or `call [reg]`, `call [reg + disp8]`, `call [reg + disp32]`) and the
    // `prefix` bytes in front of it that load its register and arguments (no relative addressing, no esp based operands). The
    // cave runs the prefix, then callBegin(site, key, target, ecx) with key = the register (the vtable) or the target, the call,
    // callEnd(site), and jumps back behind the window. eax, ecx and edx are kept around both helpers.
    template<size_t N>
    bool timeCall(PatchSet &set, Cave &cave, const uintptr_t address, const BYTE (&window)[N], const size_t prefix, const CallGroup group,
                  const char *label, const bool byScope = false) {
        if (siteCount == MAX_SITES || prefix >= N) {
            return false;
        }
        const BYTE *call = window + prefix;
        const size_t callLength = N - prefix;
        const bool direct = call[0] == 0xE8 && callLength == 5;
        uint32_t target = 0;
        if (direct) {
            int32_t displacement;
            memcpy(&displacement, call + 1, sizeof(displacement));
            target = static_cast<uint32_t>(address + N + displacement);
        } else {
            const BYTE modrm = callLength >= 2 ? call[1] : 0;
            const BYTE mod = modrm >> 6;
            const BYTE base = modrm & 7;
            const size_t dispLength = mod == 1 ? 1 : mod == 2 ? 4 : 0;
            if (call[0] != 0xFF || ((modrm >> 3) & 7) != 2 || mod == 3 || base == 4 || (mod == 0 && base == 5) || callLength != 2 + dispLength) {
                return false;
            }
        }

        const uint32_t site = siteCount++;
        sites[site] = {static_cast<uint32_t>(address), group, label, byScope};
        const uintptr_t entry = cave.here();
        emitBytes(cave, window, prefix);
        const BYTE save[3] = {0x50, 0x51, 0x52};
        const BYTE restore[3] = {0x5A, 0x59, 0x58};
        cave.emit(save);
        cave.emit8(0x51);
        if (direct) {
            cave.emit8(0x68);
            cave.emit32(target);
            cave.emit8(0x68);
            cave.emit32(target);
        } else {
            // push [reg + disp] (the target), push reg (the vtable)
            cave.emit8(0xFF);
            cave.emit8(static_cast<BYTE>((call[1] & 0xC7) | 0x30));
            emitBytes(cave, call + 2, callLength - 2);
            cave.emit8(static_cast<BYTE>(0x50 + (call[1] & 7)));
        }
        cave.emit8(0x68);
        cave.emit32(site);
        cave.call(reinterpret_cast<uintptr_t>(&callBegin));
        const BYTE dropFour[3] = {0x83, 0xC4, 0x10};
        cave.emit(dropFour);
        cave.emit(restore);
        if (direct) {
            cave.call(target);
        } else {
            emitBytes(cave, call, callLength);
        }
        cave.emit(save);
        cave.emit8(0x68);
        cave.emit32(site);
        cave.call(reinterpret_cast<uintptr_t>(&callEnd));
        const BYTE dropOne[3] = {0x83, 0xC4, 0x04};
        cave.emit(dropOne);
        cave.emit(restore);
        cave.jmp(address + N);
        set.hook(address, window, entry);
        return true;
    }

    constexpr uintptr_t SCOPE_CONSTRUCTOR = 0x00517690;
    constexpr uintptr_t SCOPE_DESTRUCTOR = 0x00517740;
    constexpr uintptr_t SCOPE_END = 0x0051ED60;

    // PerfScope::PerfScope(name, ...) (thiscall, three stack arguments): mov eax, [esp + 4] / test eax, eax are replaced by
    // push ecx / push edx / push [esp + 0xC] (the name) / push [esp + 0xC] (the return address) / push ecx / call scopeBegin /
    // add esp, 0xC / pop edx / pop ecx, the two instructions, jmp back. The destructor is a jmp to 0x0051ED60: push eax / ecx /
    // edx / push ecx / call scopeEnd / add esp, 4 / pop / jmp 0x0051ED60.
    void timeScopes(PatchSet &set, Cave &cave, const uintptr_t constructor, const uintptr_t destructor, const uintptr_t scopeEndFunction) {
        const BYTE constructorHead[6] = {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0};
        const uintptr_t begin = cave.here();
        const BYTE beginCode[11] = {0x51, 0x52, 0xFF, 0x74, 0x24, 0x0C, 0xFF, 0x74, 0x24, 0x0C, 0x51};
        cave.emit(beginCode);
        cave.call(reinterpret_cast<uintptr_t>(&scopeBegin));
        const BYTE beginRest[5] = {0x83, 0xC4, 0x0C, 0x5A, 0x59};
        cave.emit(beginRest);
        cave.emit(constructorHead);
        cave.jmp(constructor + sizeof(constructorHead));
        set.hook(constructor, constructorHead, begin);

        BYTE destructorHead[5] = {0xE9};
        putInt(destructorHead + 1, static_cast<int>(scopeEndFunction - (destructor + 5)));
        const uintptr_t end = cave.here();
        const BYTE endCode[4] = {0x50, 0x51, 0x52, 0x51};
        cave.emit(endCode);
        cave.call(reinterpret_cast<uintptr_t>(&scopeEnd));
        const BYTE endRest[6] = {0x83, 0xC4, 0x04, 0x5A, 0x59, 0x58};
        cave.emit(endRest);
        cave.jmp(scopeEndFunction);
        set.hook(destructor, destructorHead, end);
    }

    void timeGameScopes(PatchSet &set, Cave &cave) {
        timeScopes(set, cave, SCOPE_CONSTRUCTOR, SCOPE_DESTRUCTOR, SCOPE_END);
    }

    // the engine's frame: the engine update (0x00639CF8 calls it once per frame) and what it calls (0x006325A0)
    bool timeEngine(PatchSet &set, Cave &cave) {
        bool ok = true;
        // mov eax, [esi] / pop ecx / mov ecx, esi / call [eax + 0x28]
        const BYTE engineUpdate[8] = {0x8B, 0x06, 0x59, 0x8B, 0xCE, 0xFF, 0x50, 0x28};
        ok &= timeCall(set, cave, 0x00639EDC, engineUpdate, 5, CALL_ENGINE, "engine update");
        // mov eax, [ecx] / call [eax + 0x28]
        const BYTE first[5] = {0x8B, 0x01, 0xFF, 0x50, 0x28};
        ok &= timeCall(set, cave, 0x006325AE, first, 2, CALL_ENGINE, "engine [def548]+28");
        // mov eax, [esi] / mov ecx, esi / call [eax + 0x9C]
        const BYTE second[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x9C, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x006325CB, second, 4, CALL_ENGINE, "engine [this]+9c");
        // mov eax, [ecx] / call [eax + 0x94]
        const BYTE client[8] = {0x8B, 0x01, 0xFF, 0x90, 0x94, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x006325EB, client, 2, CALL_ENGINE, "engine [dc62c0]+94, logic paused");
        ok &= timeCall(set, cave, 0x006326FC, client, 2, CALL_ENGINE, "engine [dc62c0]+94");
        // mov eax, [esi] / push ebx / call [eax + 0x98]: the logic update of the first sub-frame of a logic frame
        const BYTE logicFirst[9] = {0x8B, 0x06, 0x53, 0xFF, 0x90, 0x98, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x006326C3, logicFirst, 3, CALL_ENGINE, "engine logic update, phase 1");
        // mov edx, [esi] / push eax / mov ecx, esi / call [edx + 0x98]: the other sub-frames
        const BYTE logicOther[11] = {0x8B, 0x16, 0x50, 0x8B, 0xCE, 0xFF, 0x92, 0x98, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x006326EB, logicOther, 5, CALL_ENGINE, "engine logic update, phases 2-6");
        return ok;
    }

    // the logic update 0x0062E4E8 (one call per sub-frame, the phase is its argument)
    bool timeLogic(PatchSet &set, Cave &cave) {
        bool ok = true;
        const BYTE virtual28[5] = {0x8B, 0x01, 0xFF, 0x50, 0x28};
        struct VirtualSite {
            uintptr_t address;
            const char *label;
        };
        const VirtualSite virtualSites[] = {
            {0x0062E6B5, "logic p1 [de3bac]+28"},
            {0x0062E6C0, "logic p1 [de7804]+28"},
            {0x0062E6CE, "logic p1 [de4690+4]+28"},
            {0x0062E6DD, "logic p1 [de897c]+28"},
            {0x0062E8AE, "logic p1 [de7cd8]+28"},
            {0x0062E8B9, "logic p1 [de46a8]+28"},
            {0x0062E8C4, "logic p1 [de772c]+28"},
            {0x0062E941, "logic p2 [de4354]+28"},
            {0x0062E94C, "logic p2 [de4360]+28"},
            {0x0062EB6F, "logic p5 [de4b40]+28"},
            {0x0062EB8B, "logic p5 [de4358]+28"},
            {0x0062EB96, "logic p5 [de435c]+28"},
            {0x0062EBA1, "logic p5 [de8200]+28"},
            {0x0062EBAC, "logic p5 [de3be8]+28"},
            {0x0062EBBE, "logic p5 [de4a1c]+28"},
            {0x0062EBC9, "logic p5 [de369c]+28"},
            {0x0062EBD4, "logic p5 [de89ac]+28"},
            {0x0062EBDF, "logic p5 [de8ac0]+28"},
            {0x0062EBF5, "logic p5 [de4938]+28"},
            {0x0062EC00, "logic p5 [de7924]+28"},
            {0x0062EC0B, "logic p5 [de8304]+28"},
        };
        for (const VirtualSite &site : virtualSites) {
            ok &= timeCall(set, cave, site.address, virtual28, 2, CALL_LOGIC, site.label);
        }

        const BYTE pathQueue[5] = {0xE8, 0xC0, 0x3C, 0x0C, 0x00};
        ok &= timeCall(set, cave, 0x0062E69F, pathQueue, 0, CALL_LOGIC, "logic path queue (every phase)");
        const BYTE afterQueue[5] = {0xE8, 0x60, 0x21, 0xE1, 0xFF};
        ok &= timeCall(set, cave, 0x0062E6A4, afterQueue, 0, CALL_LOGIC, "logic 440809 (every phase)");
        const BYTE p1First[5] = {0xE8, 0x1C, 0x26, 0x1F, 0x00};
        ok &= timeCall(set, cave, 0x0062E8CF, p1First, 0, CALL_LOGIC, "logic p1 820ef0");
        const BYTE p1Second[5] = {0xE8, 0xA6, 0xD5, 0x1E, 0x00};
        ok &= timeCall(set, cave, 0x0062E8DA, p1Second, 0, CALL_LOGIC, "logic p1 81be85");
        const BYTE p2Objects[5] = {0xE8, 0x6D, 0x77, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x0062E96F, p2Objects, 0, CALL_LOGIC, "logic p2 6260e1 per object");
        const BYTE everyPhase[5] = {0xE8, 0x2B, 0xB2, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x0062EB76, everyPhase, 0, CALL_LOGIC, "logic 629da6 (every phase)");
        const BYTE p5First[5] = {0xE8, 0x11, 0xB7, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x0062EBB3, p5First, 0, CALL_LOGIC, "logic p5 62a2c9");
        const BYTE p5Second[5] = {0xE8, 0xE4, 0x08, 0x1E, 0x00};
        ok &= timeCall(set, cave, 0x0062EBEA, p5Second, 0, CALL_LOGIC, "logic p5 80f4d3");
        const BYTE p1ObjectsFirst[5] = {0xE8, 0xCF, 0x1D, 0x06, 0x00};
        ok &= timeCall(set, cave, 0x0062EC6E, p1ObjectsFirst, 0, CALL_LOGIC, "logic p1 690a42 per object");
        const BYTE p1ObjectsSecond[5] = {0xE8, 0x14, 0x92, 0x06, 0x00};
        ok &= timeCall(set, cave, 0x0062EC9D, p1ObjectsSecond, 0, CALL_LOGIC, "logic p1 697eb6 per object");

        // lea ecx, [ebx + 0x10] / mov eax, [ecx] / call [eax]: the update of one module of the sleeping update lists (phases 3 to 6)
        const BYTE module[7] = {0x8D, 0x4B, 0x10, 0x8B, 0x01, 0xFF, 0x10};
        ok &= timeCall(set, cave, 0x0062EA97, module, 5, CALL_MODULE, "module update");
        return ok;
    }

    // the client update 0x0064849E (once per frame) and the display's draw 0x0044B788
    bool timeClient(PatchSet &set, Cave &cave) {
        bool ok = true;
        struct VirtualSite {
            uintptr_t address;
            BYTE slot;
            const char *label;
        };
        const VirtualSite virtualSites[] = {
            {0x0064858F, 0x28, "client [de3b54]+28"},
            {0x0064859E, 0x28, "client [de3c24]+28"},
            {0x006485AD, 0x28, "client [de7734]+28"},
            {0x006485BC, 0x28, "client [de4efc]+28"},
            {0x006485C7, 0x28, "client [de4ab0]+28"},
            {0x006485D6, 0x28, "client [de4334]+28"},
            {0x006485E1, 0x3C, "client [de4334]+3c"},
            {0x006485EC, 0x28, "client [de8b34]+28"},
            {0x006485F7, 0x28, "client [de3670]+28"},
            {0x00648606, 0x28, "client [de36e0]+28"},
            {0x00648611, 0x40, "client [de36e0]+40"},
            {0x00648640, 0x28, "client [de495c]+28"},
            {0x0064864B, 0x28, "client [df06f8]+28"},
            {0x006487E8, 0x28, "client [de8d68]+28"},
            {0x00648833, 0x28, "client [de4ac8+4]+28"},
            {0x0064883E, 0x28, "client display update"},
            {0x00648867, 0x30, "client display draw"},
            {0x00648872, 0x28, "client [de4518]+28"},
            {0x00648891, 0x28, "client [de7890]+28"},
            {0x006488AC, 0x28, "client [de4830]+28"},
        };
        for (const VirtualSite &site : virtualSites) {
            const BYTE window[5] = {0x8B, 0x01, 0xFF, 0x50, site.slot};
            ok &= timeCall(set, cave, site.address, window, 2, CALL_CLIENT, site.label);
        }
        // mov eax, [ebx] / mov ecx, ebx / call [eax + 0x90]
        const BYTE last[10] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x90, 0x90, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x006488B1, last, 4, CALL_CLIENT, "client [this]+90");

        struct DirectSite {
            uintptr_t address;
            BYTE call[5];
            const char *label;
        };
        const DirectSite directSites[] = {
            {0x006484C8, {0xE8, 0x6F, 0x66, 0xEC, 0xFF}, "client 50eb3c"},
            {0x00648616, {0xE8, 0x3B, 0xA6, 0x13, 0x00}, "client 782c56"},
            {0x00648650, {0xE8, 0x1A, 0xA7, 0xEE, 0xFF}, "client 532d6f"},
            {0x006487D6, {0xE8, 0xBB, 0xD1, 0x02, 0x00}, "client drawable update per drawable"},
            {0x0064880C, {0xE8, 0x3A, 0xD1, 0xFF, 0xFF}, "client 64594b"},
            {0x00648817, {0xE8, 0x69, 0x2B, 0xFE, 0xFF}, "client 62b385"},
            {0x0044B7B5, {0xE8, 0x01, 0x79, 0xFF, 0xFF}, "draw 4430bb"},
            {0x0044B7BC, {0xE8, 0x19, 0x81, 0xFF, 0xFF}, "draw 4438da"},
            {0x0044BB00, {0xE8, 0xF3, 0xE1, 0xFF, 0xFF}, "draw frame (panorama shots)"},
            {0x0044BBF5, {0xE8, 0xFE, 0xE0, 0xFF, 0xFF}, "draw frame"},
            {0x0044BC5A, {0xE8, 0x85, 0xD5, 0x02, 0x00}, "draw 4791e4 after the frame"},
        };
        for (const DirectSite &site : directSites) {
            ok &= timeCall(set, cave, site.address, site.call, 0, CALL_CLIENT, site.label);
        }
        return ok;
    }

    // the direct calls of the frame draw 0x00449CF8 outside its render scopes (device lock, scene begin and end, presenting, ...)
    bool timeDraw(PatchSet &set, Cave &cave) {
        bool ok = true;
        struct DirectSite {
            uintptr_t address;
            BYTE call[5];
            const char *label;
        };
        const DirectSite directSites[] = {
            {0x00449D0D, {0xE8, 0xAE, 0x51, 0x0D, 0x00}, "frame 51eec0 (device lock)"},
            {0x00449D17, {0xE8, 0x24, 0xCF, 0x0C, 0x00}, "frame 516c40"},
            {0x00449D3B, {0xE8, 0x84, 0x02, 0x03, 0x00}, "frame 479fc4"},
            {0x00449D55, {0xE8, 0xA1, 0xE7, 0x01, 0x00}, "frame 4684fb"},
            {0x00449D8C, {0xE8, 0x63, 0x91, 0x02, 0x00}, "frame 472ef4"},
            {0x00449DA6, {0xE8, 0x02, 0x9F, 0x02, 0x00}, "frame 473cad"},
            {0x00449E28, {0xE8, 0x68, 0x58, 0x03, 0x00}, "frame 47f695"},
            {0x00449EA5, {0xE8, 0x86, 0xDC, 0x0C, 0x00}, "frame 517b30"},
            {0x00449EFA, {0xE8, 0xA1, 0xCE, 0x0C, 0x00}, "frame 516da0 (first)"},
            {0x0044A0B3, {0xE8, 0xB4, 0xB2, 0x25, 0x00}, "frame 6a536c"},
            {0x0044A19C, {0xE8, 0xFF, 0x1E, 0x65, 0x00}, "frame a9c0a0"},
            {0x0044A1A9, {0xE8, 0x8B, 0x97, 0xFF, 0xFF}, "frame 443939"},
            {0x0044A203, {0xE8, 0xD1, 0x62, 0x21, 0x00}, "frame 6604d9"},
            {0x0044A222, {0xE8, 0x33, 0xFA, 0xFF, 0xFF}, "frame 449c5a"},
            {0x0044A228, {0xE8, 0x73, 0xCB, 0x0C, 0x00}, "frame 516da0 (last)"},
            {0x0044A271, {0xE8, 0x5A, 0x66, 0x0D, 0x00}, "frame 5208d0 (device unlock)"},
        };
        for (const DirectSite &site : directSites) {
            ok &= timeCall(set, cave, site.address, site.call, 0, CALL_CLIENT, site.label);
        }

        // inside the end of the frame 0x00516DA0 (flush, end scene, ...) and the end scene 0x005225E0 (EndScene, Present)
        const DirectSite endSites[] = {
            {0x00516DAA, {0xE8, 0xB1, 0x7E, 0x01, 0x00}, "end frame 52ec60 (flush)"},
            {0x00516DCF, {0xE8, 0x7C, 0x30, 0x01, 0x00}, "end frame 529e50"},
            {0x00516DD4, {0xE8, 0xB7, 0x22, 0x01, 0x00}, "end frame 529090"},
            {0x00516DE0, {0xE8, 0xAB, 0x88, 0x00, 0x00}, "end frame 51f690"},
            {0x00522636, {0xE8, 0x65, 0x40, 0x05, 0x00}, "end scene 5766a0"},
        };
        for (const DirectSite &site : endSites) {
            ok &= timeCall(set, cave, site.address, site.call, 0, CALL_CLIENT, site.label);
        }
        // mov ecx, [eax] / push eax / call [ecx + 0xA8]: IDirect3DDevice9::EndScene
        const BYTE endScene[9] = {0x8B, 0x08, 0x50, 0xFF, 0x91, 0xA8, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x00522625, endScene, 3, CALL_CLIENT, "d3d EndScene");
        // mov edx, [eax] / push ebx (four times) / push eax / call [edx + 0x44]: IDirect3DDevice9::Present
        const BYTE present[10] = {0x8B, 0x10, 0x53, 0x53, 0x53, 0x53, 0x50, 0xFF, 0x52, 0x44};
        ok &= timeCall(set, cave, 0x00522649, present, 7, CALL_CLIENT, "d3d Present");
        return ok;
    }

    // the CPU particles: the manager's update (0x005F5123, every system every client frame), one system's update keyed by its class,
    // its storage module's update (0x007B09A1 for CPU systems: every particle, the dead ones deleted), one particle's update and
    // deletion, the storage's emission and the creation of one particle, which makes a W3D model for a DRAWABLE system
    bool timeParticles(PatchSet &set, Cave &cave) {
        bool ok = true;
        // mov ecx, [TheParticleSystemManager] / mov eax, [ecx] / call [eax + 0x28]
        const BYTE manager[11] = {0x8B, 0x0D, 0x44, 0x37, 0xDE, 0x00, 0x8B, 0x01, 0xFF, 0x50, 0x28};
        ok &= timeCall(set, cave, 0x00449D40, manager, 8, CALL_CLIENT, "frame particle manager update");
        // push [edi + 0x64] / mov eax, [ecx] / call [eax + 0x10]
        const BYTE system[8] = {0xFF, 0x77, 0x64, 0x8B, 0x01, 0xFF, 0x50, 0x10};
        ok &= timeCall(set, cave, 0x005F51E6, system, 5, CALL_CLIENT, "particles: system update [system]+10");
        // mov eax, [ecx] / call [eax + 0x28]
        const BYTE storage[5] = {0x8B, 0x01, 0xFF, 0x50, 0x28};
        ok &= timeCall(set, cave, 0x005F333B, storage, 2, CALL_CLIENT, "particles: storage update [storage]+28");
        // mov ecx, esi / call 0x005FA0E6
        const BYTE particle[7] = {0x8B, 0xCE, 0xE8, 0x2E, 0x97, 0xE4, 0xFF};
        ok &= timeCall(set, cave, 0x007B09B1, particle, 2, CALL_CLIENT, "particles: particle update 5fa0e6");
        // mov eax, [esi] / push 0 / mov ecx, esi / call [eax]
        const BYTE remove[8] = {0x8B, 0x06, 0x6A, 0x00, 0x8B, 0xCE, 0xFF, 0x10};
        ok &= timeCall(set, cave, 0x007B09BC, remove, 6, CALL_CLIENT, "particles: particle delete [particle]+0");
        // push eax / push [ebp + 0xC] / call [edx + 0x24]
        const BYTE emit[7] = {0x50, 0xFF, 0x75, 0x0C, 0xFF, 0x52, 0x24};
        ok &= timeCall(set, cave, 0x005F3DBD, emit, 4, CALL_CLIENT, "particles: emission [storage]+24");
        // mov eax, [esi] / push ebx / push [ebp - 0x10] / lea ecx, [ebp - 0x50] / push [ebp + 8] / push ecx / mov ecx, esi / call [eax + 0x14]
        const BYTE create[18] = {0x8B, 0x06, 0x53, 0xFF, 0x75, 0xF0, 0x8D, 0x4D, 0xB0, 0xFF, 0x75, 0x08, 0x51, 0x8B, 0xCE, 0xFF, 0x50, 0x14};
        ok &= timeCall(set, cave, 0x005F93BF, create, 15, CALL_CLIENT, "particles: create one [system]+14");

        // the particle render pass 0x0044C84A: one system's draw module (keyed by its class), in the main loop and for the sort level
        // lists, and the flush of the batches after the loop
        // mov eax, [ecx] / lea edx, [edi + 0x54] / push edx / lea edx, [ebp - 0x84] / push edx / push [ebp + 8] / call [eax + 0x10]
        const BYTE draw[19] = {0x8B, 0x01, 0x8D, 0x57, 0x54, 0x52, 0x8D, 0x95, 0x7C, 0xFF, 0xFF, 0xFF, 0x52, 0xFF, 0x75, 0x08, 0xFF, 0x50, 0x10};
        ok &= timeCall(set, cave, 0x0044CB8E, draw, 16, CALL_CLIENT, "particles: draw [draw module]+10");
        // mov eax, [ecx] / add edx, 0x54 / push edx / lea edx, [ebp - 0x84] / push edx / push [ebp + 8] / call [eax + 0x10]
        const BYTE drawSorted[19] = {0x8B, 0x01, 0x83, 0xC2, 0x54, 0x52, 0x8D, 0x95, 0x7C, 0xFF, 0xFF, 0xFF, 0x52, 0xFF, 0x75, 0x08, 0xFF, 0x50,
                                     0x10};
        ok &= timeCall(set, cave, 0x0044CC7C, drawSorted, 16, CALL_CLIENT, "particles: draw sorted [draw module]+10");
        // mov ecx, [TheParticleSystemManager] / push [edi + 0x5C] / mov eax, [ecx] / call [eax + 0x3C]
        const BYTE flush[14] = {0x8B, 0x0D, 0x44, 0x37, 0xDE, 0x00, 0xFF, 0x77, 0x5C, 0x8B, 0x01, 0xFF, 0x50, 0x3C};
        ok &= timeCall(set, cave, 0x0044CCFC, flush, 11, CALL_CLIENT, "particles: render flush [manager]+3c");

        // inside RenderObjectDrawModule's draw 0x00964C00, per visible particle: its model's transform, the colour or opacity written
        // into the model (0x0050E040 emissive, 0x0050E244 opacity, 0x0050E413 diffuse) and two more calls on the model
        // mov eax, [ecx] / lea edx, [ebp - 0xC] / push edx / call [eax + 0x54]
        const BYTE transform[9] = {0x8B, 0x01, 0x8D, 0x55, 0xF4, 0x52, 0xFF, 0x50, 0x54};
        ok &= timeCall(set, cave, 0x0096520E, transform, 6, CALL_CLIENT, "ro draw: transform [model]+54");
        // push [esi + 0x8C] / call ...
        const BYTE emissive[11] = {0xFF, 0xB6, 0x8C, 0x00, 0x00, 0x00, 0xE8, 0xC4, 0x8D, 0xBA, 0xFF};
        ok &= timeCall(set, cave, 0x00965271, emissive, 6, CALL_CLIENT, "ro draw: emissive 50e040");
        const BYTE opacity[11] = {0xFF, 0xB6, 0x8C, 0x00, 0x00, 0x00, 0xE8, 0xB4, 0x8F, 0xBA, 0xFF};
        ok &= timeCall(set, cave, 0x00965285, opacity, 6, CALL_CLIENT, "ro draw: opacity 50e244");
        const BYTE diffuse[11] = {0xFF, 0xB6, 0x8C, 0x00, 0x00, 0x00, 0xE8, 0x43, 0x91, 0xBA, 0xFF};
        ok &= timeCall(set, cave, 0x009652C5, diffuse, 6, CALL_CLIENT, "ro draw: diffuse 50e413");
        // mov eax, [ecx] / call [eax + 0x190]
        const BYTE model190[8] = {0x8B, 0x01, 0xFF, 0x90, 0x90, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x009652D9, model190, 2, CALL_CLIENT, "ro draw: [model]+190");
        // mov eax, [ecx] / push 0 / call [eax + 0x194]
        const BYTE model194[10] = {0x8B, 0x01, 0x6A, 0x00, 0xFF, 0x90, 0x94, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x009652EB, model194, 4, CALL_CLIENT, "ro draw: [model]+194");
        return ok;
    }

    // what a material parameter rebuild is made of: the emissive setter 0x0050E040 around it (copy of the list, the new entry, its merge
    // into the copy, the copy's destruction) and the rebuild 0x00551F8F itself (D3DX parameter block, list reset and merge, the
    // per-parameter lookups and sets)
    bool timeRebuild(PatchSet &set, Cave &cave) {
        bool ok = true;
        // push eax / lea ecx, [ebp - 0x24] / call 0x0050DF9F
        const BYTE copy[9] = {0x50, 0x8D, 0x4D, 0xDC, 0xE8, 0x8B, 0xFE, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x0050E10B, copy, 4, CALL_CLIENT, "setter: copy list 50df9f");
        const BYTE entry[5] = {0xE8, 0x12, 0x2B, 0x04, 0x00};
        ok &= timeCall(set, cave, 0x0050E11E, entry, 0, CALL_CLIENT, "setter: new entry 550c35");
        const BYTE replace[5] = {0xE8, 0x29, 0x47, 0xF7, 0xFF};
        ok &= timeCall(set, cave, 0x0050E14C, replace, 0, CALL_CLIENT, "setter: replace entry 48287a");
        const BYTE entryGone[5] = {0xE8, 0x2E, 0xD2, 0xF6, 0xFF};
        ok &= timeCall(set, cave, 0x0050E163, entryGone, 0, CALL_CLIENT, "setter: free entry 47b396");
        const BYTE copyGone[5] = {0xE8, 0x29, 0xDE, 0xF6, 0xFF};
        ok &= timeCall(set, cave, 0x0050E16F, copyGone, 0, CALL_CLIENT, "setter: free copy 47bf9d");

        // mov ecx, [eax] / push eax / call [ecx + 0x130]
        const BYTE deleteBlock[9] = {0x8B, 0x08, 0x50, 0xFF, 0x91, 0x30, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x00551FD5, deleteBlock, 3, CALL_CLIENT, "rebuild: DeleteParameterBlock");
        const BYTE textures[5] = {0xE8, 0x98, 0xFA, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x00551FEB, textures, 0, CALL_CLIENT, "rebuild: clear textures 551a88");
        const BYTE defaults[5] = {0xE8, 0xFE, 0xFB, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x00551FFD, defaults, 0, CALL_CLIENT, "rebuild: defaults 551c00");
        const BYTE merge[5] = {0xE8, 0x5F, 0x08, 0xF3, 0xFF};
        ok &= timeCall(set, cave, 0x00552016, merge, 0, CALL_CLIENT, "rebuild: merge one 48287a");
        // mov eax, [edi] / push edi / call [eax + 0x124]
        const BYTE begin[9] = {0x8B, 0x07, 0x57, 0xFF, 0x90, 0x24, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0055203C, begin, 3, CALL_CLIENT, "rebuild: BeginParameterBlock");
        // mov ecx, [edi] / push eax / push 0 / push edi / call [ecx + 0x24]
        const BYTE byName[9] = {0x8B, 0x0F, 0x50, 0x6A, 0x00, 0x57, 0xFF, 0x51, 0x24};
        ok &= timeCall(set, cave, 0x00552064, byName, 6, CALL_CLIENT, "rebuild: GetParameterByName");
        // mov ecx, [edi] / push eax / push edi / call [ecx + 0xF8]
        const BYTE used[10] = {0x8B, 0x0F, 0x50, 0x57, 0xFF, 0x91, 0xF8, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0055207B, used, 4, CALL_CLIENT, "rebuild: IsParameterUsed");
        const BYTE texture[5] = {0xE8, 0xB5, 0x07, 0xFE, 0xFF};
        ok &= timeCall(set, cave, 0x005520BB, texture, 0, CALL_CLIENT, "rebuild: texture by name 532875");
        const BYTE keepTexture[5] = {0xE8, 0xAA, 0xFA, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x005520E7, keepTexture, 0, CALL_CLIENT, "rebuild: keep texture 551b96");
        // push [ebp + 8] / push edi / call [eax + 0x78]
        const BYTE setFloat[7] = {0xFF, 0x75, 0x08, 0x57, 0xFF, 0x50, 0x78};
        ok &= timeCall(set, cave, 0x00552113, setFloat, 4, CALL_CLIENT, "rebuild: SetFloat");
        // push edi / call [eax + 0x88]
        const BYTE setVector[7] = {0x57, 0xFF, 0x90, 0x88, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0055215E, setVector, 1, CALL_CLIENT, "rebuild: SetVector");
        // mov eax, [edi] / push edi / call [eax + 0x128]
        const BYTE end[9] = {0x8B, 0x07, 0x57, 0xFF, 0x90, 0x28, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x00552199, end, 3, CALL_CLIENT, "rebuild: EndParameterBlock");
        return ok;
    }

    // inside the mesh render path: one mesh's draw (0x0054A1F0), the effect apply (0x0055165E), the scene render (0x00517C60, its
    // calls keyed by the render scope around them: shadow map or view) and the scope name building of the batch loop (0x00573BEB)
    bool timeRender(PatchSet &set, Cave &cave) {
        bool ok = true;
        // mov edx, [ebx] / push 1 / mov ecx, ebx / call [edx + 0x10]
        const BYTE passApply[9] = {0x8B, 0x13, 0x6A, 0x01, 0x8B, 0xCB, 0xFF, 0x52, 0x10};
        ok &= timeCall(set, cave, 0x0054AAE1, passApply, 6, CALL_CLIENT, "mesh pass apply [pass]+10");
        const BYTE lights[5] = {0xE8, 0xC5, 0x0D, 0x04, 0x00};
        ok &= timeCall(set, cave, 0x0054AAFD, lights, 0, CALL_CLIENT, "mesh 58b8c7");
        const BYTE draw[5] = {0xE8, 0x81, 0x54, 0xFD, 0xFF};
        ok &= timeCall(set, cave, 0x0054AB1A, draw, 0, CALL_CLIENT, "mesh draw 51ffa0");
        const BYTE end[5] = {0xE8, 0xF9, 0x03, 0xF3, 0xFF};
        ok &= timeCall(set, cave, 0x0054AB69, end, 0, CALL_CLIENT, "mesh 47af67");

        const BYTE stateApply[5] = {0xE8, 0x50, 0xBC, 0xFC, 0xFF};
        ok &= timeCall(set, cave, 0x0055167B, stateApply, 0, CALL_CLIENT, "effect render state apply 51d2d0");
        const BYTE parameters[5] = {0xE8, 0x68, 0xF7, 0xFF, 0xFF};
        ok &= timeCall(set, cave, 0x00551689, parameters, 0, CALL_CLIENT, "effect parameters 550df6");
        // mov ecx, [eax] / push eax / call [ecx + 0x104]: ID3DXEffect::CommitChanges
        const BYTE commit[9] = {0x8B, 0x08, 0x50, 0xFF, 0x91, 0x04, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, COMMIT_SITE, commit, 3, CALL_CLIENT, "d3dx CommitChanges");

        // mov eax, [ecx] / push edi / call [eax + 0x34]
        const BYTE sceneFirst[6] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x34};
        ok &= timeCall(set, cave, 0x00517C7A, sceneFirst, 3, CALL_CLIENT, "scene [ecx]+34", true);
        const BYTE sceneVisibility[5] = {0xE8, 0xA9, 0xD8, 0x01, 0x00};
        ok &= timeCall(set, cave, 0x00517C82, sceneVisibility, 0, CALL_CLIENT, "scene 535530", true);
        const BYTE sceneProfileFirst[5] = {0xE8, 0x57, 0xDC, 0xF4, 0xFF};
        ok &= timeCall(set, cave, 0x00517CA8, sceneProfileFirst, 0, CALL_CLIENT, "scene 465904 (first)", true);
        // mov edx, [esi] / mov ecx, esi / call [edx + 0x1C]
        const BYTE sceneRender[7] = {0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x1C};
        ok &= timeCall(set, cave, 0x00517CB0, sceneRender, 4, CALL_CLIENT, "scene [this]+1c", true);
        const BYTE sceneProfileSecond[5] = {0xE8, 0x7D, 0xDB, 0xF4, 0xFF};
        ok &= timeCall(set, cave, 0x00517D82, sceneProfileSecond, 0, CALL_CLIENT, "scene 465904 (second)", true);
        // mov eax, [ecx] / add esp, 8 / push esi / call [eax + 0x58]
        const BYTE sceneObjects[9] = {0x8B, 0x01, 0x83, 0xC4, 0x08, 0x56, 0xFF, 0x50, 0x58};
        ok &= timeCall(set, cave, 0x00517D98, sceneObjects, 6, CALL_CLIENT, "scene [ecx]+58", true);
        const BYTE sceneMeshes[5] = {0xE8, 0x24, 0xFD, 0x02, 0x00};
        ok &= timeCall(set, cave, 0x00517DA7, sceneMeshes, 0, CALL_CLIENT, "scene 547ad0", true);
        const BYTE sceneShaders[5] = {0xE8, 0x4F, 0xC6, 0x05, 0x00};
        ok &= timeCall(set, cave, 0x00517DB2, sceneShaders, 0, CALL_CLIENT, "scene 574406 (FX batches)", true);
        // mov edx, [ecx] / push esi / call [edx + 8]
        const BYTE sceneLast[6] = {0x8B, 0x11, 0x56, 0xFF, 0x52, 0x08};
        ok &= timeCall(set, cave, 0x00517DCA, sceneLast, 3, CALL_CLIENT, "scene [ecx]+8", true);
        const BYTE sceneFlush[5] = {0xE8, 0x85, 0x6E, 0x01, 0x00};
        ok &= timeCall(set, cave, 0x00517DD6, sceneFlush, 0, CALL_CLIENT, "scene 52ec60 (flush)", true);
        const BYTE sceneEnd[5] = {0xE8, 0x3A, 0xC2, 0x02, 0x00};
        ok &= timeCall(set, cave, 0x00517DE1, sceneEnd, 0, CALL_CLIENT, "scene 544020", true);

        const BYTE nameCopy[5] = {0xE8, 0xCB, 0x91, 0x4C, 0x00};
        ok &= timeCall(set, cave, 0x00573D46, nameCopy, 0, CALL_CLIENT, "batch scope name _mbscpy");
        const BYTE nameAppend[5] = {0xE8, 0x7D, 0x98, 0x4C, 0x00};
        ok &= timeCall(set, cave, 0x00573D7A, nameAppend, 0, CALL_CLIENT, "batch scope name _mbscat");
        return ok;
    }

    // the calls of HordeAIUpdate's update (0x0089E2D0), the battalion AI
    bool timeHorde(PatchSet &set, Cave &cave) {
        bool ok = true;
        // mov eax, [ecx] / mov [ebp - 0xC], ebx / call [eax + 0x7C]
        const BYTE state[8] = {0x8B, 0x01, 0x89, 0x5D, 0xF4, 0xFF, 0x50, 0x7C};
        ok &= timeCall(set, cave, 0x0089E2E4, state, 5, CALL_LOGIC, "horde ai [ai]+7c");
        const BYTE first[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x2C, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E2EE, first, 4, CALL_LOGIC, "horde ai [state]+12c");
        // mov eax, [ecx] / push 0 / call [eax + 0x114]
        const BYTE second[10] = {0x8B, 0x01, 0x6A, 0x00, 0xFF, 0x90, 0x14, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E306, second, 4, CALL_LOGIC, "horde ai [ai]+114");
        const BYTE third[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0xA4, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E313, third, 4, CALL_LOGIC, "horde ai [state]+a4");
        const BYTE fourth[8] = {0x8B, 0x01, 0xFF, 0x90, 0xB8, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E329, fourth, 2, CALL_LOGIC, "horde ai [this]+1b8");
        // mov eax, [esi] / push edi / mov ecx, esi / call [eax + 0x98]
        const BYTE retarget[11] = {0x8B, 0x06, 0x57, 0x8B, 0xCE, 0xFF, 0x90, 0x98, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E355, retarget, 5, CALL_LOGIC, "horde ai [state]+98");
        const BYTE fifth[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0xA0, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E39D, fifth, 4, CALL_LOGIC, "horde ai [state]+a0");
        // lea ecx, [ebp - 0x18] / push ecx / mov ecx, esi / call [eax + 0x248]
        const BYTE formation[12] = {0x8D, 0x4D, 0xE8, 0x51, 0x8B, 0xCE, 0xFF, 0x90, 0x48, 0x02, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E3BB, formation, 6, CALL_LOGIC, "horde ai [state]+248");
        const BYTE move[5] = {0xE8, 0xE2, 0xE0, 0xDC, 0xFF};
        ok &= timeCall(set, cave, 0x0089E3E3, move, 0, CALL_LOGIC, "horde ai 66c4ca");
        const BYTE sixth[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x9C, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E3EA, sixth, 4, CALL_LOGIC, "horde ai [state]+9c");
        // push [ebp - 8] / mov eax, [esi] / mov ecx, esi / call [eax + 0x10]
        const BYTE command[10] = {0xFF, 0x75, 0xF8, 0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x10};
        ok &= timeCall(set, cave, 0x0089E3FC, command, 7, CALL_LOGIC, "horde ai [state]+10");
        const BYTE seventh[10] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0xDC, 0x00, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0089E409, seventh, 4, CALL_LOGIC, "horde ai [state]+dc");
        const BYTE base[5] = {0xE8, 0x75, 0x01, 0xDD, 0xFF};
        ok &= timeCall(set, cave, 0x0089E415, base, 0, CALL_LOGIC, "horde ai base update 66e58f");
        return ok;
    }

    struct DirectCall {
        uintptr_t address;
        BYTE call[5];
        const char *label;
    };

    template<size_t N>
    bool timeDirectCalls(PatchSet &set, Cave &cave, const DirectCall (&calls)[N], const CallGroup group, const bool byScope) {
        bool ok = true;
        for (const DirectCall &call : calls) {
            ok &= timeCall(set, cave, call.address, call.call, 0, group, call.label, byScope);
        }
        return ok;
    }

    // the render pass over the object lists (0x00470F44) and its object list render (0x00470176), keyed by the render scope around
    // them (shadow map or view)
    bool timeRenderLists(PatchSet &set, Cave &cave) {
        const DirectCall calls[] = {
            {0x00470F7C, {0xE8, 0xF5, 0xF1, 0xFF, 0xFF}, "pass 470176 (object lists)"},
            {0x00471025, {0xE8, 0x2A, 0x8D, 0x02, 0x00}, "pass 499d54"},
            {0x00471363, {0xE8, 0x88, 0xDB, 0x0C, 0x00}, "pass 53eef0"},
            {0x004713B3, {0xE8, 0x48, 0xDD, 0x0C, 0x00}, "pass 53f100"},
            {0x00471480, {0xE8, 0xC6, 0x75, 0xFF, 0xFF}, "pass 468a4b"},
            {0x004714DE, {0xE8, 0x6D, 0xED, 0x0C, 0x00}, "pass 540250"},
            {0x0047156D, {0xE8, 0x2E, 0x9B, 0xFF, 0xFF}, "pass 46b0a0 (trees, shrubs)"},
            {0x00471621, {0xE8, 0x47, 0x87, 0x02, 0x00}, "pass 499d6d"},
            {0x004716BB, {0xE8, 0xF5, 0xF1, 0xFF, 0xFF}, "pass 4708b5"},
            {0x004716EE, {0xE8, 0xF7, 0xAC, 0xFD, 0xFF}, "pass 44c3ea"},
            {0x004716F4, {0xE8, 0x67, 0xD5, 0x0B, 0x00}, "pass 52ec60 (flush)"},
            {0x0047171A, {0xE8, 0xEC, 0xF2, 0xFF, 0xFF}, "pass 470a0b"},
            {0x004704CB, {0xE8, 0xBE, 0xEF, 0xFF, 0xFF}, "lists 46f48e (1)"},
            {0x004704F3, {0xE8, 0x96, 0xEF, 0xFF, 0xFF}, "lists 46f48e (2)"},
            {0x004705C4, {0xE8, 0xC5, 0xEE, 0xFF, 0xFF}, "lists 46f48e (3)"},
            {0x004705EB, {0xE8, 0x9E, 0xEE, 0xFF, 0xFF}, "lists 46f48e (4)"},
            {0x004706C7, {0xE8, 0xC2, 0xED, 0xFF, 0xFF}, "lists 46f48e (5)"},
            {0x004706F0, {0xE8, 0x99, 0xED, 0xFF, 0xFF}, "lists 46f48e (6)"},
            {0x004702FE, {0xE8, 0xDC, 0xE0, 0xFF, 0xFF}, "lists 46e3df (1)"},
            {0x00470736, {0xE8, 0xA4, 0xDC, 0xFF, 0xFF}, "lists 46e3df (2)"},
        };
        return timeDirectCalls(set, cave, calls, CALL_CLIENT, true);
    }

    // the calls of the object list render 0x0046F48E, keyed by the render scope around them
    bool timeObjectRender(PatchSet &set, Cave &cave) {
        const DirectCall calls[] = {
            {0x0046F4C6, {0xE8, 0x25, 0xFA, 0x0C, 0x00}, "object 53eef0"},
            {0x0046F5A4, {0xE8, 0xC7, 0xBD, 0x0C, 0x00}, "object 53b370"},
            {0x0046F5B0, {0xE8, 0x1E, 0xE8, 0x20, 0x00}, "object 67ddd3"},
            {0x0046F60F, {0xE8, 0x46, 0x0F, 0x20, 0x00}, "object 67055a"},
            {0x0046F631, {0xE8, 0xC1, 0xE2, 0x21, 0x00}, "object 68d8f7 (1)"},
            {0x0046F6B8, {0xE8, 0xC4, 0x9F, 0xFD, 0xFF}, "object 449681"},
            {0x0046F6C6, {0xE8, 0x2C, 0xE2, 0x21, 0x00}, "object 68d8f7 (2)"},
            {0x0046F743, {0xE8, 0xB8, 0xF9, 0x0C, 0x00}, "object 53f100 (1)"},
            {0x0046F750, {0xE8, 0xB1, 0x12, 0x20, 0x00}, "object 670a06"},
            {0x0046F75A, {0xE8, 0x3B, 0x38, 0x20, 0x00}, "object 672f9a"},
            {0x0046F764, {0xE8, 0x46, 0x38, 0x20, 0x00}, "object 672faf"},
            {0x0046F7A8, {0xE8, 0xA3, 0x0A, 0x0D, 0x00}, "object 540250 (1)"},
            {0x0046F8EE, {0xE8, 0x58, 0x91, 0xFF, 0xFF}, "object 468a4b (1)"},
            {0x0046F967, {0xE8, 0xE4, 0x08, 0x0D, 0x00}, "object 540250 (2)"},
            {0x0046FA26, {0xE8, 0xA5, 0x2D, 0x0D, 0x00}, "object 5427d0"},
            {0x0046FA33, {0xE8, 0xE8, 0x2C, 0x0D, 0x00}, "object 542720 (1)"},
            {0x0046FA44, {0xE8, 0xD7, 0x2C, 0x0D, 0x00}, "object 542720 (2)"},
            {0x0046FB1F, {0xE8, 0xDC, 0xF5, 0x0C, 0x00}, "object 53f100 (2)"},
            {0x0046FB77, {0xE8, 0xCF, 0x8E, 0xFF, 0xFF}, "object 468a4b (2)"},
            {0x0046FBB7, {0xE8, 0x94, 0x06, 0x0D, 0x00}, "object 540250 (3)"},
            {0x0046FC02, {0xE8, 0x49, 0x06, 0x0D, 0x00}, "object 540250 (4)"},
            {0x0046FC6C, {0xE8, 0xCC, 0xEC, 0xFF, 0xFF}, "object 46e93d (1)"},
            {0x0046FC7E, {0xE8, 0xCD, 0x05, 0x0D, 0x00}, "object 540250 (5)"},
            {0x0046FCF9, {0xE8, 0x3F, 0xEC, 0xFF, 0xFF}, "object 46e93d (2)"},
            {0x0046FD19, {0xE8, 0x32, 0x05, 0x0D, 0x00}, "object 540250 (6)"},
            {0x0046FD7C, {0xE8, 0x7F, 0x07, 0x0D, 0x00}, "object 540500"},
            {0x0046FDE2, {0xE8, 0x39, 0x29, 0x0D, 0x00}, "object 542720 (3)"},
            {0x0046FE03, {0xE8, 0xD5, 0xEC, 0xFF, 0xFF}, "object 46eadd"},
        };
        bool ok = timeDirectCalls(set, cave, calls, CALL_CLIENT, true);
        // its virtual calls: the drawable's (ebx) and its render object's, the render info's (+0x50); +0x30 is the render object's Render
        const BYTE bounds[14] = {0x8B, 0xCB, 0x89, 0x7D, 0xFC, 0x89, 0x7D, 0xAC, 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046F4D0, bounds, 8, CALL_CLIENT, "object [eax]+104 (1)", true);
        const BYTE drawable[15] = {0x8B, 0x03, 0x8B, 0xCB, 0xF3, 0x0F, 0x11, 0x45, 0x94, 0xFF, 0x90, 0x5C, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046F500, drawable, 9, CALL_CLIENT, "object [eax]+15c", true);
        const BYTE camera[7] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x1C};
        ok &= timeCall(set, cave, 0x0046F569, camera, 4, CALL_CLIENT, "object [eax]+1c", true);
        const BYTE visible[7] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x50, 0x48};
        ok &= timeCall(set, cave, 0x0046F691, visible, 4, CALL_CLIENT, "object [eax]+48", true);
        const BYTE skinned1[10] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x90, 0xC0, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046F6D7, skinned1, 4, CALL_CLIENT, "object [eax]+1c0 (1)", true);
        const BYTE render1[8] = {0x8B, 0x03, 0x56, 0x8B, 0xCB, 0xFF, 0x50, 0x30};
        ok &= timeCall(set, cave, 0x0046FA9D, render1, 5, CALL_CLIENT, "object render [eax]+30 (1)", true);
        const BYTE skinned2[10] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x90, 0xC0, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046FAAE, skinned2, 4, CALL_CLIENT, "object [eax]+1c0 (2)", true);
        const BYTE lights1[10] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046FC2A, lights1, 4, CALL_CLIENT, "object [eax]+104 (2)", true);
        const BYTE lights2[10] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046FCB7, lights2, 4, CALL_CLIENT, "object [eax]+104 (3)", true);
        const BYTE skinned3[10] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x90, 0xC0, 0x01, 0x00, 0x00};
        ok &= timeCall(set, cave, 0x0046FD45, skinned3, 4, CALL_CLIENT, "object [eax]+1c0 (3)", true);
        const BYTE info[7] = {0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x50, 0x50};
        ok &= timeCall(set, cave, 0x0046FD6B, info, 4, CALL_CLIENT, "object [eax]+50", true);
        const BYTE render2[8] = {0x8B, 0x03, 0x57, 0x8B, 0xCB, 0xFF, 0x50, 0x30};
        ok &= timeCall(set, cave, 0x0046FE0F, render2, 5, CALL_CLIENT, "object render [eax]+30 (2)", true);
        const BYTE render3[8] = {0x8B, 0x03, 0x57, 0x8B, 0xCB, 0xFF, 0x50, 0x30};
        const bool render3Timed = timeCall(set, cave, 0x0046FE39, render3, 5, CALL_CLIENT, "object render [eax]+30 (3)");
        if (render3Timed) {
            // the common Render call: by the render object's class, odd keys in the shadow map pass
            sites[siteCount - 1].vtableByShadow = true;
        }
        ok &= render3Timed;
        return ok;
    }

    // the calls of AIUpdate's update 0x0066E58F (every AI module, the battalions' too)
    bool timeAiUpdate(PatchSet &set, Cave &cave) {
        const DirectCall calls[] = {
            {0x0066E5B0, {0xE8, 0x22, 0x87, 0x0E, 0x00}, "ai update 756cd7 (1)"},
            {0x0066E5C3, {0xE8, 0x85, 0x82, 0xFB, 0xFF}, "ai update 62684d"},
            {0x0066E5EA, {0xE8, 0x05, 0x48, 0xF7, 0xFF}, "ai update 5e2df4"},
            {0x0066E613, {0xE8, 0xC5, 0x50, 0x0E, 0x00}, "ai update 7536dd (state machine)"},
            {0x0066E625, {0xE8, 0x31, 0x4B, 0x0E, 0x00}, "ai update 75315b (state machine)"},
            {0x0066E65D, {0xE8, 0x75, 0x86, 0x0E, 0x00}, "ai update 756cd7 (2)"},
            {0x0066E68E, {0xE8, 0x85, 0x02, 0xE0, 0xFF}, "ai update 46e918"},
            {0x0066E702, {0xE8, 0x41, 0xE0, 0xFF, 0xFF}, "ai update 66c748"},
            {0x0066E70C, {0xE8, 0xDE, 0xAE, 0xFF, 0xFF}, "ai update 6695ef"},
            {0x0066E732, {0xE8, 0x5C, 0x68, 0xFF, 0xFF}, "ai update 664f93"},
            {0x0066E73A, {0xE8, 0xE2, 0x71, 0xFF, 0xFF}, "ai update 665921"},
            {0x0066E78C, {0xE8, 0x72, 0x9B, 0xFF, 0xFF}, "ai update 668303"},
            {0x0066E7A5, {0xE8, 0x46, 0x3C, 0xE2, 0xFF}, "ai update 4923f0"},
            {0x0066E7B3, {0xE8, 0xA2, 0x6B, 0xFF, 0xFF}, "ai update 66535a"},
            {0x0066E7F9, {0xE8, 0x2E, 0x4D, 0xFF, 0xFF}, "ai update 66352c"},
            {0x0066E890, {0xE8, 0xE9, 0x38, 0xFF, 0xFF}, "ai update 66217e"},
            {0x0066E8A8, {0xE8, 0x41, 0x36, 0x13, 0x00}, "ai update 7a1eee"},
            {0x0066E8B6, {0xE8, 0x21, 0xAE, 0x02, 0x00}, "ai update 6996dc"},
        };
        return timeDirectCalls(set, cave, calls, CALL_LOGIC, false);
    }

    // StateMachine::updateStateMachine 0x008DB6D5 (every state machine, nested ones too): the current state's update and the
    // transition checks after it, by the state's class
    bool timeStateMachine(PatchSet &set, Cave &cave) {
        const BYTE update[7] = {0x8B, 0x01, 0x8B, 0xD9, 0xFF, 0x50, 0x18};
        const BYTE transitions[6] = {0x50, 0xE8, 0xCE, 0xFD, 0xFF, 0xFF};
        const BYTE sleepTransitions[6] = {0x50, 0xE8, 0xE2, 0xFE, 0xFF, 0xFF};
        const bool ok = timeCall(set, cave, 0x008DB709, update, 4, CALL_LOGIC, "state update")
            && timeCall(set, cave, 0x008DB728, transitions, 1, CALL_LOGIC, "state transitions")
            && timeCall(set, cave, 0x008DB6F6, sleepTransitions, 1, CALL_LOGIC, "state sleep transitions");
        if (ok) {
            for (uint32_t site = siteCount - 3; site < siteCount; site++) {
                sites[site].byState = true;
            }
        }
        return ok;
    }

    // the msvcr71 file functions, through their import slots; an import is replaced only while its slot still holds the library's
    // own function
    bool timeFiles(PatchSet &set, Cave &) {
        const HMODULE runtime = GetModuleHandleW(L"msvcr71.dll");
        if (runtime == nullptr) {
            return false;
        }
        struct Import {
            const char *name;
            uintptr_t slot;
            void *wrapper;
            void *original;
        };
        const Import imports[] = {
            {"_read", READ_SLOT, reinterpret_cast<void *>(&timedRead), reinterpret_cast<void *>(&originalRead)},
            {"_lseek", SEEK_SLOT, reinterpret_cast<void *>(&timedSeek), reinterpret_cast<void *>(&originalSeek)},
            {"_open", OPEN_SLOT, reinterpret_cast<void *>(&timedOpen), reinterpret_cast<void *>(&originalOpen)},
            {"_wopen", WIDE_OPEN_SLOT, reinterpret_cast<void *>(&timedWideOpen), reinterpret_cast<void *>(&originalWideOpen)},
        };
        for (const Import &import : imports) {
            const auto library = reinterpret_cast<uint32_t>(GetProcAddress(runtime, import.name));
            if (library == 0) {
                return false;
            }
            *static_cast<uint32_t *>(import.original) = library;
            set.replaceSlot(import.slot, library, reinterpret_cast<uintptr_t>(import.wrapper));
        }
        return true;
    }

    bool commitGroup(bool (*time)(PatchSet &, Cave &), void (*extra)(PatchSet &, Cave &)) {
        BYTE *memory = allocateCave();
        if (memory == nullptr) {
            return false;
        }
        Cave cave(memory);
        static PatchSet set;
        set.reset();
        const uint32_t firstSite = siteCount;
        bool ok = time(set, cave);
        if (extra != nullptr) {
            extra(set, cave);
        }
        if (!ok || !commit(set, cave)) {
            VirtualFree(memory, 0, MEM_RELEASE);
            siteCount = firstSite;
            return false;
        }
        // a group of import slots (timeFiles) emits no code
        if (cave.size == 0) {
            VirtualFree(memory, 0, MEM_RELEASE);
        }
        return true;
    }
}

void instrumentation::pauseCallTimers(const uint64_t cycles) {
    for (size_t i = 0; i < depth; i++) {
        opened[i].start += cycles;
    }
}

bool instrumentation::applyCallTiming() {

    if (stats() == nullptr) {
        return false;
    }
    // the dll is loaded while the process starts, on the thread that runs the game
    mainThread = GetCurrentThreadId();
    loadedAt = __rdtsc();
    siteCount = 0;

    // each group is applied as a whole or not at all, the others still
    const bool engine = commitGroup(timeEngine, timeGameScopes);
    const bool logic = commitGroup(timeLogic, nullptr);
    const bool client = commitGroup(timeClient, nullptr);
    const bool draw = commitGroup(timeDraw, nullptr);
    const bool particles = commitGroup(timeParticles, nullptr);
    const bool rebuild = commitGroup(timeRebuild, nullptr);
    const bool files = commitGroup(timeFiles, nullptr);
    const bool render = commitGroup(timeRender, nullptr);
    const bool horde = commitGroup(timeHorde, nullptr);
    const bool lists = commitGroup(timeRenderLists, nullptr);
    const bool objects = commitGroup(timeObjectRender, nullptr);
    const bool ai = commitGroup(timeAiUpdate, nullptr);
    const bool states = commitGroup(timeStateMachine, nullptr);
    return engine && logic && client && draw && particles && rebuild && files && render && horde && lists && objects && ai && states;
}
