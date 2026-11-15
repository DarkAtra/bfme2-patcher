#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "d3d_methods.h"

namespace d3d {

    const Interface INTERFACES[INTERFACES_KNOWN] = {
        {0, 119, "SDSLSADDLDSSSSLDXXLLSASXLLXXLLXASSAALXXXXAAAASAASASASASASASSSSASSASASASASASASASAXAAXXSLXSASLASASASASXSASXSLASASASASSSSL",
            "vv|-|-|-|-|-|v|v|vv|v|vvv|vvv|v|vv|vv|-|v|vvvv|vvvv|vv|v|vvp1536|vv|vvvvvvvv|vvvvvvvvv|vvvvvvv|vvvvvv|vvvvvv|vvvvvvvv|"
            "vvvvvvvv|op16op8|oo|vv|vv|op16op16v|op16v|vvvvvv|vo|vv|o|v|-|-|vpc16@0vvvv|vp64|vv|vp64|p24|v|p68|v|vp104|vv|vv|vv|vp16|vv|"
            "vv|vv|vv|-|v|p8|v|vv|vo|vvv|vvv|vvv|vvv|v|vp1024|vv|v|v|p16|v|v|-|v|-|vvv|vvvvvv|vvvv|vvvvvvvv|vvvvvv|vv|o|v|v|v|vv|o|v|"
            "vpc16@2v|vvv|vpc16@2v|vvv|vpc4@2v|vvv|vovv|vvvv|vv|vv|o|v|vv|o|v|vpc16@2v|vvv|vpc16@2v|vvv|vpc4@2v|vvv|vvv|vvv|v|vv"},
        {120, 79, "LSXXXXXXXXXXXXXXXXXXXSXSASXXASXSASXSASXSASXSXSASXSASXSSSALXSSSXXAAAALSSXSXXXXSX",
            "vv|-|-|v|vv|vv|vv|vv|vv|vv|vv|vv|v|v|vv|vv|v|v|vv|vv|vvv|vvv|hv|vv|hpc4@2v|vvv|hv|vv|hpc4@2v|vvv|hv|vv|hpc4@2v|vvv|hp16|vv|"
            "hpc16@2v|vvv|hp64|vv|hpc64@2v|vvv|vvv|vvv|hp64|vv|hpc64@2v|vvv|vvv|vvv|hs|vv|ho|vv|vv|vv|hvv|v|h|-|v|vv|vv|vv|v|-|-|-|v|-|"
            "-|v|v|-|-|v|v|vv|hpc1@3vv"},
        {200, 22, "LDRDSSSSSADSSDSSADDXXA", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|v|-|-|v|-|-|vv|vv|vvvv|v|p16"},
        {230, 22, "LDRDSSSSSADSSDSSADDSAA", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|v|-|-|v|-|-|vv|vvv|vvvvv|vv|vp16"},
        {260, 22, "LDRDSSSSSADSSDSSADDSAA", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|v|-|-|v|-|-|vv|vv|vvvv|v|p24"},
        {290, 17, "LDRDSSSSSADDDXXSS", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|vv|v|vvv|-|v|v"},
        {310, 11, "LDRDSSSDDSA", "vv|-|-|v|vvvv|vvv|v|vv|v|vvv|-"},
        {330, 14, "LDRDSSSSSADXXD", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|vvvv|-|v"},
        {350, 14, "LDRDSSSSSADXXD", "vv|-|-|v|vvvv|vvv|v|v|-|-|-|vvvv|-|v"},
        {370, 6, "LDRDAA", "vv|-|-|v|-|-"},
        {380, 8, "LDRDDDAS", "vv|-|-|v|-|-|v|vvv"},
        {390, 10, "LDRXSLLLDL", "vv|-|-|vvvvv|v|vvv|v|v|v|v"},
        {400, 5, "LDRDS", "vv|-|-|v|vv"},
        {410, 5, "LDRDS", "vv|-|-|v|vv"},
        {420, 5, "LDRDS", "vv|-|-|v|vv"},
    };

    namespace {

        constexpr size_t MAX_SLOTS = 128;
        constexpr size_t MAX_DEVICE_SLOTS = 512;
        constexpr size_t MAX_VTABLES = 48;
        constexpr size_t THUNK_BYTES = 32;
        constexpr size_t THUNK_MEMORY = 64 * 1024;

        struct Hooked {
            uint32_t vtable;
            uint32_t api;
            bool active;
            uint32_t originals[MAX_SLOTS];
            // where a call reads the method it runs: the original, or for the device the slot of D3D9's own table
            const uint32_t *through[MAX_SLOTS];
            uint32_t hooks[MAX_SLOTS];
        };

        Hooked hooked[MAX_VTABLES];
        size_t hookedCount = 0;
        size_t deviceEntry = MAX_VTABLES;
        // the device's vtable from hookDevice on: D3D9 never writes it
        uint32_t deviceTable[MAX_DEVICE_SLOTS];
        BYTE *thunks = nullptr;
        size_t thunkSize = 0;
        Client client = nullptr;
        HookCounts ownCounts = {};
        HookCounts *counts = &ownCounts;
        uint32_t mainThread = 0;
        // set while the game thread is inside the client or the hooking: the D3D calls made then are not the game's
        bool busy = false;
        Range d3d9 = {};
        Range d3dx = {};
        Range self = {};
        Layout layouts[METHODS];
        // kindOf's answers, once parseLayouts has run
        char kinds[METHODS];

        constexpr size_t D3DX_IMPORT_SLOTS = 64;
        // the exe's import slots of d3dx9_27.dll, ended by a zero
        uint32_t *d3dxImports = reinterpret_cast<uint32_t *>(0x00BD09B4);
        constexpr const char *D3DX_NAMES[D3DX_FUNCTIONS] = {
            "D3DXCreateEffect", "D3DXCreateEffectFromFileA", "D3DXCreateTexture", "D3DXCreateTextureFromFileInMemoryEx",
            "D3DXCreateCubeTextureFromFileInMemoryEx", "D3DXCreateVolumeTexture", "D3DXCreateVolumeTextureFromFileInMemoryEx",
            "D3DXLoadVolumeFromFileInMemory", "D3DXCheckTextureRequirements", "D3DXLoadSurfaceFromFileInMemory",
            "D3DXLoadSurfaceFromMemory", "D3DXLoadSurfaceFromSurface", "D3DXFilterTexture", "D3DXAssembleShader",
        };
        uint32_t d3dxOriginals[D3DX_FUNCTIONS];

        uint16_t readNumber(const char *&spec) {
            uint16_t value = 0;
            while (*spec >= '0' && *spec <= '9') {
                value = static_cast<uint16_t>(value * 10 + (*spec++ - '0'));
            }
            return value;
        }

        void parseLayouts() {
            for (const Interface &type : INTERFACES) {
                const char *spec = type.specs;
                for (uint32_t slot = 0; slot < type.slots; slot++) {
                    Layout &layout = layouts[type.base + slot];
                    layout = {};
                    kinds[type.base + slot] = type.kinds[slot];
                    while (*spec != '\0' && *spec != '|') {
                        const char token = *spec++;
                        if (token == '-') {
                            continue;
                        }
                        const auto argument = static_cast<uint8_t>(layout.arguments++);
                        if (token == 'o') {
                            layout.objects = static_cast<uint16_t>(layout.objects | 1 << argument);
                        } else if (token == 'h') {
                            layout.handles = static_cast<uint16_t>(layout.handles | 1 << argument);
                        } else if (token == 's') {
                            layout.string = static_cast<uint8_t>(argument + 1);
                        } else if (token == 'p') {
                            Block block = {argument, NO_COUNT, 0};
                            if (*spec == 'c') {
                                spec++;
                                block.bytes = readNumber(spec);
                                // '@' and the argument
                                spec++;
                                block.count = static_cast<uint8_t>(*spec++ - '0');
                            } else {
                                block.bytes = readNumber(spec);
                            }
                            if (layout.blocks < 2) {
                                layout.block[layout.blocks++] = block;
                            }
                        }
                    }
                    if (*spec == '|') {
                        spec++;
                    }
                }
            }
        }

        // a slot points into a loaded module (D3D9, D3DX, this dll, or Windows' compatibility layer, which wraps a few device
        // methods)
        bool knownCode(const uint32_t address) {
            HMODULE module = nullptr;
            return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(address), &module) && module != nullptr;
        }

        uint32_t emitThunk(uint32_t method, const uint32_t *original);
        uint32_t wrapperFor(uint32_t method);

        bool writeSlots(uint32_t *slots, const size_t count, const uint32_t *values) {
            DWORD previous;
            // vtables may share their page with code
            if (!VirtualProtect(slots, count * sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &previous)) {
                return false;
            }
            for (size_t i = 0; i < count; i++) {
                slots[i] = values[i];
            }
            DWORD ignored;
            VirtualProtect(slots, count * sizeof(uint32_t), previous, &ignored);
            return true;
        }

        // live: the device's own table, which D3D9 rewrites from its class's vtable (dropping the wrappers Windows' compatibility layer
        // put into slots 0, 2, 13 and 16): the hooks run what it holds, as the game's calls through it did; a slot leading to this dll
        // (call_timing's wrappers) keeps leading there
        void hookVtable(const uint32_t vtable, const uint32_t api, const uint32_t *live = nullptr) {
            if (hookedCount == MAX_VTABLES) {
                counts->refused++;
                return;
            }
            Hooked &entry = hooked[hookedCount++];
            entry.vtable = vtable;
            entry.api = api;
            entry.active = false;
            const Interface &type = INTERFACES[api];
            auto *slots = reinterpret_cast<uint32_t *>(vtable);
            for (size_t slot = 0; slot < type.slots; slot++) {
                if (!knownCode(slots[slot])) {
                    counts->refused++;
                    return;
                }
            }
            if (thunkSize + type.slots * THUNK_BYTES > THUNK_MEMORY) {
                counts->refused++;
                return;
            }
            for (size_t slot = 0; slot < type.slots; slot++) {
                entry.originals[slot] = slots[slot];
                entry.through[slot] = live != nullptr && !self.contains(slots[slot]) ? &live[slot] : &entry.originals[slot];
                const uint32_t wrapper = wrapperFor(type.base + slot);
                entry.hooks[slot] = wrapper != 0 ? wrapper : emitThunk(type.base + slot, entry.through[slot]);
            }
            if (!writeSlots(slots, type.slots, entry.hooks)) {
                counts->refused++;
                return;
            }
            entry.active = true;
            counts->vtables++;
        }

        bool alreadyHooked(const uint32_t vtable) {
            for (size_t i = 0; i < hookedCount; i++) {
                if (hooked[i].vtable == vtable) {
                    return true;
                }
            }
            return false;
        }

        void discover(const uint32_t object, const uint32_t api) {
            if (object == 0) {
                return;
            }
            const uint32_t vtable = *reinterpret_cast<const uint32_t *>(object);
            if (alreadyHooked(vtable)) {
                return;
            }
            uint32_t known = api;
            if (api == BASE_TEXTURE) {
                // GetType: D3DRTYPE_TEXTURE 3, D3DRTYPE_VOLUMETEXTURE 4, D3DRTYPE_CUBETEXTURE 5
                const uint32_t type = slotOf<uint32_t (__stdcall *)(uint32_t)>(object, 10)(object);
                known = type == 3 ? TEXTURE : type == 4 ? VOLUME_TEXTURE : type == 5 ? CUBE_TEXTURE : INTERFACES_KNOWN;
                if (known == INTERFACES_KNOWN) {
                    return;
                }
            }
            hookVtable(vtable, known);
        }

        // the objects a call hands to the device: their classes may not have been seen yet
        void discoverArguments(const uint32_t method, const uint32_t *frame) {
            switch (method) {
                case SET_TEXTURE:
                case EFFECT_SET_TEXTURE:
                    discover(frame[3], BASE_TEXTURE);
                    break;
                case UPDATE_TEXTURE:
                    discover(frame[2], BASE_TEXTURE);
                    discover(frame[3], BASE_TEXTURE);
                    break;
                case SET_STREAM_SOURCE:
                    discover(frame[3], VERTEX_BUFFER);
                    break;
                case SET_INDICES:
                    discover(frame[2], INDEX_BUFFER);
                    break;
                case SET_RENDER_TARGET:
                    discover(frame[3], SURFACE);
                    break;
                case SET_DEPTH_STENCIL_SURFACE:
                case COLOR_FILL:
                    discover(frame[2], SURFACE);
                    break;
                case UPDATE_SURFACE:
                case STRETCH_RECT:
                    discover(frame[2], SURFACE);
                    discover(frame[4], SURFACE);
                    break;
                case GET_RENDER_TARGET_DATA:
                    discover(frame[2], SURFACE);
                    discover(frame[3], SURFACE);
                    break;
                case SET_VERTEX_DECLARATION:
                    discover(frame[2], DECLARATION);
                    break;
                case SET_VERTEX_SHADER:
                    discover(frame[2], VERTEX_SHADER);
                    break;
                case SET_PIXEL_SHADER:
                    discover(frame[2], PIXEL_SHADER);
                    break;
                default:
                    break;
            }
        }

        // the classes are discovered before the client sees the call, so that a client timing the call does not count it
        uint32_t __cdecl dispatch(const uint32_t method, uint32_t *frame, const uint32_t *original) {
            if (__readfsdword(0x24) != mainThread) {
                const uint32_t target = client(method, frame, *original);
                return target != 0 ? target : *original;
            }
            if (busy) {
                return *original;
            }
            busy = true;
            if (method < METHODS) {
                discoverArguments(method, frame);
            }
            const uint32_t target = client(method, frame, *original);
            busy = false;
            return target != 0 ? target : *original;
        }

        // push ecx / push edx / lea eax, [esp + 8] / push original / push eax / push method / call dispatch / add esp, 12 / pop edx /
        // pop ecx / jmp eax: the method sees the stack, ecx and edx of the call (eax is free at a stdcall method's entry)
        uint32_t emitThunk(const uint32_t method, const uint32_t *original) {
            BYTE code[THUNK_BYTES] = {
                0x51, 0x52, 0x8D, 0x44, 0x24, 0x08, 0x68, 0x00, 0x00, 0x00, 0x00, 0x50, 0x68, 0x00, 0x00, 0x00, 0x00,
                0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0x5A, 0x59, 0xFF, 0xE0, 0xCC, 0xCC, 0xCC,
            };
            BYTE *at = thunks + thunkSize;
            const auto through = reinterpret_cast<uint32_t>(original);
            const auto call = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dispatch) - (reinterpret_cast<uintptr_t>(at) + 22));
            memcpy(code + 7, &through, sizeof(through));
            memcpy(code + 13, &method, sizeof(method));
            memcpy(code + 18, &call, sizeof(call));
            memcpy(at, code, sizeof(code));
            thunkSize += THUNK_BYTES;
            return reinterpret_cast<uint32_t>(at);
        }

        // what a wrapper does before it runs the method: the same as a thunk; it runs what this returns (the method, or the client's
        // target)
        uint32_t notify(const uint32_t method, uint32_t *frame, const uint32_t original) {
            return dispatch(method, frame, &original);
        }

        // the object a method handed out through `out`
        void noteMade(const uint32_t out, const uint32_t api) {
            if (out == 0 || __readfsdword(0x24) != mainThread || busy) {
                return;
            }
            busy = true;
            discover(*reinterpret_cast<const uint32_t *>(out), api);
            busy = false;
        }

        template<uint32_t METHOD, size_t AT, uint32_t MADE, typename... Args>
        HRESULT __stdcall making(const uint32_t object, Args... args) {
            const uint32_t original = originalOf(object, METHOD);
            const uint32_t target = notify(METHOD, static_cast<uint32_t *>(_AddressOfReturnAddress()), original);
            const HRESULT result = reinterpret_cast<HRESULT (__stdcall *)(uint32_t, Args...)>(target)(object, args...);
            const uint32_t values[] = {static_cast<uint32_t>(args)...};
            if (SUCCEEDED(result)) {
                noteMade(values[AT], MADE);
            }
            return result;
        }

        using U = uint32_t;

        template<size_t INDEX, size_t AT, typename... Args>
        HRESULT __stdcall creatingEffect(Args... args) {
            const uint32_t original = d3dxOriginals[INDEX];
            const uint32_t target = notify(METHODS + INDEX, static_cast<uint32_t *>(_AddressOfReturnAddress()), original);
            const HRESULT result = reinterpret_cast<HRESULT (__stdcall *)(Args...)>(target)(args...);
            const uint32_t values[] = {static_cast<uint32_t>(args)...};
            if (SUCCEEDED(result)) {
                noteMade(values[AT], EFFECT);
            }
            return result;
        }

        struct Maker {
            uint32_t method;
            void *wrapper;
        };

        const Maker MAKERS[] = {
            {14, reinterpret_cast<void *>(&making<14, 1, SWAP_CHAIN, U, U>)},
            {18, reinterpret_cast<void *>(&making<18, 3, SURFACE, U, U, U, U>)},
            {23, reinterpret_cast<void *>(&making<23, 6, TEXTURE, U, U, U, U, U, U, U, U>)},
            {24, reinterpret_cast<void *>(&making<24, 7, VOLUME_TEXTURE, U, U, U, U, U, U, U, U, U>)},
            {25, reinterpret_cast<void *>(&making<25, 5, CUBE_TEXTURE, U, U, U, U, U, U, U>)},
            {26, reinterpret_cast<void *>(&making<26, 4, VERTEX_BUFFER, U, U, U, U, U, U>)},
            {27, reinterpret_cast<void *>(&making<27, 4, INDEX_BUFFER, U, U, U, U, U, U>)},
            {28, reinterpret_cast<void *>(&making<28, 6, SURFACE, U, U, U, U, U, U, U, U>)},
            {29, reinterpret_cast<void *>(&making<29, 6, SURFACE, U, U, U, U, U, U, U, U>)},
            {36, reinterpret_cast<void *>(&making<36, 4, SURFACE, U, U, U, U, U, U>)},
            {38, reinterpret_cast<void *>(&making<38, 1, SURFACE, U, U>)},
            {40, reinterpret_cast<void *>(&making<40, 0, SURFACE, U>)},
            {59, reinterpret_cast<void *>(&making<59, 1, STATE_BLOCK, U, U>)},
            {61, reinterpret_cast<void *>(&making<61, 0, STATE_BLOCK, U>)},
            {64, reinterpret_cast<void *>(&making<64, 1, BASE_TEXTURE, U, U>)},
            {86, reinterpret_cast<void *>(&making<86, 1, DECLARATION, U, U>)},
            {91, reinterpret_cast<void *>(&making<91, 1, VERTEX_SHADER, U, U>)},
            {106, reinterpret_cast<void *>(&making<106, 1, PIXEL_SHADER, U, U>)},
            {118, reinterpret_cast<void *>(&making<118, 1, QUERY, U, U>)},
            // GetSurfaceLevel, GetCubeMapSurface, GetVolumeLevel, the swap chain's GetBackBuffer
            {200 + 18, reinterpret_cast<void *>(&making<200 + 18, 1, SURFACE, U, U>)},
            {230 + 18, reinterpret_cast<void *>(&making<230 + 18, 2, SURFACE, U, U, U>)},
            {260 + 18, reinterpret_cast<void *>(&making<260 + 18, 1, VOLUME, U, U>)},
            {390 + 5, reinterpret_cast<void *>(&making<390 + 5, 2, SURFACE, U, U, U>)},
        };

        HRESULT __stdcall beginStateBlock(const uint32_t device) {
            const uint32_t original = originalOf(device, BEGIN_STATE_BLOCK);
            const uint32_t target = notify(BEGIN_STATE_BLOCK, static_cast<uint32_t *>(_AddressOfReturnAddress()), original);
            const HRESULT result = reinterpret_cast<HRESULT (__stdcall *)(uint32_t)>(target)(device);
            if (SUCCEEDED(result)) {
                counts->recordings++;
            }
            return result;
        }

        bool committed(const void *address, const DWORD protections) {
            MEMORY_BASIC_INFORMATION info;
            return VirtualQuery(address, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT && (info.Protect & protections) != 0
                   && (info.Protect & PAGE_GUARD) == 0;
        }

        // D3D9 calls methods of its own behind the interface's through the device's vtable (175 slots), so the copy takes all of them
        size_t deviceTableLength(const uint32_t *table) {
            constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                       | PAGE_EXECUTE_WRITECOPY;
            constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            size_t length = INTERFACES[DEVICE].slots;
            while (length < MAX_DEVICE_SLOTS && committed(&table[length], READABLE)
                   && committed(reinterpret_cast<const void *>(table[length]), EXECUTABLE)) {
                length++;
            }
            return length;
        }

        uint32_t wrapperFor(const uint32_t method) {
            if (method == BEGIN_STATE_BLOCK) {
                return reinterpret_cast<uint32_t>(&beginStateBlock);
            }
            for (const Maker &maker : MAKERS) {
                if (maker.method == method) {
                    return reinterpret_cast<uint32_t>(maker.wrapper);
                }
            }
            return 0;
        }

        void hookImports() {
            const HMODULE library = GetModuleHandleW(L"d3dx9_27.dll");
            if (library == nullptr) {
                return;
            }
            // D3DXCreateEffect(device, data, length, defines, include, flags, pool, &effect, &errors) and
            // D3DXCreateEffectFromFileA(device, file, defines, include, flags, pool, &effect, &errors): the effects' class is
            // hooked as they are made
            const uint32_t wrappers[D3DX_FUNCTIONS] = {
                reinterpret_cast<uint32_t>(&creatingEffect<0, 7, U, U, U, U, U, U, U, U, U>),
                reinterpret_cast<uint32_t>(&creatingEffect<1, 6, U, U, U, U, U, U, U, U>),
            };
            for (size_t i = 0; i < D3DX_FUNCTIONS; i++) {
                const auto function = reinterpret_cast<uint32_t>(GetProcAddress(library, D3DX_NAMES[i]));
                for (size_t slot = 0; function != 0 && slot < D3DX_IMPORT_SLOTS && d3dxImports[slot] != 0; slot++) {
                    if (d3dxImports[slot] == function && thunkSize + THUNK_BYTES <= THUNK_MEMORY) {
                        d3dxOriginals[i] = function;
                        const uint32_t thunk = wrappers[i] != 0 ? wrappers[i] : emitThunk(static_cast<uint32_t>(METHODS + i), &d3dxOriginals[i]);
                        writeSlots(&d3dxImports[slot], 1, &thunk);
                        break;
                    }
                }
            }
        }

        void findModules() {
            d3d9 = moduleRange(GetModuleHandleW(L"d3d9.dll"));
            d3dx = moduleRange(GetModuleHandleW(L"d3dx9_27.dll"));
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&dispatch), &module);
            self = moduleRange(module);
        }
    }

    char kindOf(const uint32_t method) {
        if (method < METHODS && kinds[method] != 0) {
            return kinds[method];
        }
        for (const Interface &type : INTERFACES) {
            if (method >= type.base && method < type.base + type.slots) {
                return type.kinds[method - type.base];
            }
        }
        return '?';
    }

    const Layout &layoutOf(const uint32_t method) {
        static const Layout NONE = {};
        return method < METHODS ? layouts[method] : NONE;
    }

    Range moduleRange(const HMODULE module) {
        if (module == nullptr) {
            return {};
        }
        const auto base = reinterpret_cast<uint32_t>(module);
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
        return {base, base + nt->OptionalHeader.SizeOfImage};
    }

    bool fromD3d(const uint32_t address) {
        return d3d9.contains(address) || d3dx.contains(address);
    }

    bool start(const Client onCall, HookCounts *const hookCounts) {
        if (client != nullptr || onCall == nullptr) {
            return false;
        }
        thunks = static_cast<BYTE *>(VirtualAlloc(nullptr, THUNK_MEMORY, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (thunks == nullptr) {
            return false;
        }
        // the dll is loaded while the process starts, on the thread that runs the game
        mainThread = GetCurrentThreadId();
        if (hookCounts != nullptr) {
            counts = hookCounts;
        }
        parseLayouts();
        client = onCall;
        return true;
    }

    void hookDevice(const uint32_t device) {
        if (client == nullptr || deviceEntry != MAX_VTABLES || device == 0 || hookedCount == MAX_VTABLES) {
            return;
        }
        findModules();
        auto *pointer = reinterpret_cast<volatile uint32_t *>(device);
        const auto *table = reinterpret_cast<const uint32_t *>(*pointer);
        memcpy(deviceTable, table, deviceTableLength(table) * sizeof(uint32_t));
        deviceEntry = hookedCount;
        hookVtable(reinterpret_cast<uint32_t>(deviceTable), DEVICE, table);
        if (!hooked[deviceEntry].active) {
            return;
        }
        *pointer = reinterpret_cast<uint32_t>(deviceTable);
        hookImports();
    }

    void hookEffect(const uint32_t vtable) {
        if (client == nullptr || vtable == 0 || alreadyHooked(vtable)) {
            return;
        }
        findModules();
        hookVtable(vtable, EFFECT);
    }

    bool deviceHooked(const uint32_t device) {
        return deviceEntry != MAX_VTABLES && hooked[deviceEntry].active && device != 0
               && *reinterpret_cast<const uint32_t *>(device) == hooked[deviceEntry].vtable;
    }

    uint32_t originalOf(const uint32_t object, const uint32_t method) {
        const uint32_t vtable = *reinterpret_cast<const uint32_t *>(object);
        for (size_t i = 0; i < hookedCount; i++) {
            const Hooked &entry = hooked[i];
            if (entry.vtable == vtable && entry.active) {
                return *entry.through[method - INTERFACES[entry.api].base];
            }
        }
        return 0;
    }

    uint32_t originalSlotOf(const uint32_t object, const uint32_t slot) {
        const uint32_t vtable = *reinterpret_cast<const uint32_t *>(object);
        for (size_t i = 0; i < hookedCount; i++) {
            const Hooked &entry = hooked[i];
            if (entry.vtable == vtable && entry.active) {
                return slot < INTERFACES[entry.api].slots ? *entry.through[slot] : 0;
            }
        }
        return 0;
    }

    uint32_t thunkFor(const uint32_t method, const uint32_t *original) {
        if (thunks == nullptr || thunkSize + THUNK_BYTES > THUNK_MEMORY) {
            return 0;
        }
        return emitThunk(method, original);
    }

    BYTE *codeSpace(const size_t bytes) {
        const size_t rounded = (bytes + THUNK_BYTES - 1) / THUNK_BYTES * THUNK_BYTES;
        if (thunks == nullptr || thunkSize + rounded > THUNK_MEMORY) {
            return nullptr;
        }
        BYTE *at = thunks + thunkSize;
        thunkSize += rounded;
        return at;
    }

    uint32_t gameThread() {
        return mainThread;
    }
}
