#pragma once

#include <windows.h>
#include <cstdint>

// The Direct3D 9 and D3DX effect methods the game calls and the hooks on them, shared by the render thread and the D3D call stats
// (instrumentation/d3d_call_stats.cpp), one of them at a time.
// The methods' kinds and argument layouts (INTERFACES) are transcribed from the BFME2 Accelerator's gen_rt_hooks.py (Ohad Baehr,
// https://github.com/OhadBaehr/bfme2-accelerator, MIT, THIRD-PARTY-NOTICES.txt).
//
// A method's number is its interface's base plus its vtable slot; the D3DX functions bfme2-accelerator wraps follow from METHODS on.
// Every slot of the device's and the effect's vtables gets a thunk, and of every resource vtable once an object of its class is
// created, handed to the device or handed out by it; D3D9 has several classes per interface (buffers and surfaces by pool), each with
// its own vtable. The device's vtable is a table inside the device object, which D3D9 rewrites from its class's vtable for every
// state block recording and when the device is lost; so the device gets a hooked copy of it instead. A thunk passes the call to the
// client before it runs; the methods that hand out objects and BeginStateBlock get wrappers that do the same and then look at the
// result. The D3DX functions are hooked through the exe's import slots.
namespace d3d {

    constexpr uint32_t METHODS = 432;
    constexpr uint32_t D3DX_FUNCTIONS = 14;

    struct Interface {
        uint32_t base;
        uint32_t slots;
        // the accelerator's kind of each slot (gen_rt_hooks.py): A queued, S waits for the queue, D direct, R release queued,
        // L under the queue's lock, X hand-written
        const char *kinds;
        // each slot's arguments after the object, separated by '|' (gen_rt_hooks.py): v value, o COM object, h effect handle,
        // s string, p<N> pointer to N bytes, pc<M>@i pointer to M times argument i bytes, - none
        const char *specs;
    };

    enum InterfaceIndex : uint32_t {
        DEVICE, EFFECT, TEXTURE, CUBE_TEXTURE, VOLUME_TEXTURE, SURFACE, VOLUME, VERTEX_BUFFER, INDEX_BUFFER, STATE_BLOCK, QUERY,
        SWAP_CHAIN, VERTEX_SHADER, PIXEL_SHADER, DECLARATION, INTERFACES_KNOWN,
        // a texture, cube texture or volume texture: GetType tells which
        BASE_TEXTURE = INTERFACES_KNOWN,
    };

    extern const Interface INTERFACES[INTERFACES_KNOWN];

    constexpr uint32_t RESET = 16;
    constexpr uint32_t PRESENT = 17;
    constexpr uint32_t CREATE_TEXTURE = 23;
    constexpr uint32_t CREATE_VERTEX_BUFFER = 26;
    constexpr uint32_t CREATE_INDEX_BUFFER = 27;
    constexpr uint32_t CREATE_OFFSCREEN_PLAIN_SURFACE = 36;
    constexpr uint32_t UPDATE_SURFACE = 30;
    constexpr uint32_t UPDATE_TEXTURE = 31;
    constexpr uint32_t GET_RENDER_TARGET_DATA = 32;
    constexpr uint32_t GET_FRONT_BUFFER_DATA = 33;
    constexpr uint32_t STRETCH_RECT = 34;
    constexpr uint32_t COLOR_FILL = 35;
    constexpr uint32_t SET_RENDER_TARGET = 37;
    constexpr uint32_t GET_RENDER_TARGET = 38;
    constexpr uint32_t SET_DEPTH_STENCIL_SURFACE = 39;
    constexpr uint32_t GET_DEPTH_STENCIL_SURFACE = 40;
    constexpr uint32_t BEGIN_STATE_BLOCK = 60;
    constexpr uint32_t END_STATE_BLOCK = 61;
    constexpr uint32_t SET_TEXTURE = 65;
    constexpr uint32_t GET_NPATCH_MODE = 80;
    constexpr uint32_t DRAW_PRIMITIVE_UP = 83;
    constexpr uint32_t DRAW_INDEXED_PRIMITIVE_UP = 84;
    constexpr uint32_t PROCESS_VERTICES = 85;
    constexpr uint32_t SET_VERTEX_DECLARATION = 87;
    constexpr uint32_t SET_VERTEX_SHADER = 92;
    constexpr uint32_t SET_STREAM_SOURCE = 100;
    constexpr uint32_t SET_INDICES = 104;
    constexpr uint32_t SET_PIXEL_SHADER = 107;
    constexpr uint32_t EFFECT_SET_TEXTURE = 120 + 52;
    constexpr uint32_t TEXTURE_LOCK = 200 + 19;
    constexpr uint32_t TEXTURE_UNLOCK = 200 + 20;
    constexpr uint32_t CUBE_LOCK = 230 + 19;
    constexpr uint32_t VOLUME_TEXTURE_LOCK = 260 + 19;
    constexpr uint32_t SURFACE_LOCK = 290 + 13;
    constexpr uint32_t SURFACE_UNLOCK = 290 + 14;
    constexpr uint32_t SURFACE_GET_DC = 290 + 15;
    constexpr uint32_t VOLUME_LOCK = 310 + 9;
    constexpr uint32_t VERTEX_BUFFER_LOCK = 330 + 11;
    constexpr uint32_t VERTEX_BUFFER_UNLOCK = 330 + 12;
    constexpr uint32_t INDEX_BUFFER_LOCK = 350 + 11;
    constexpr uint32_t INDEX_BUFFER_UNLOCK = 350 + 12;
    constexpr uint32_t SWAP_CHAIN_PRESENT = 390 + 3;

    // the kind of a method (Interface::kinds), '?' for a number no interface has
    char kindOf(uint32_t method);

    constexpr uint8_t NO_COUNT = 0xFF;

    struct Block {
        uint8_t pointer;
        // the argument `bytes` is multiplied by, NO_COUNT for a fixed size
        uint8_t count;
        uint16_t bytes;
    };

    // a method's arguments after the object, read from its spec (Interface::specs)
    struct Layout {
        uint8_t arguments;
        uint8_t blocks;
        // the string argument's index + 1, 0 for none
        uint8_t string;
        // the COM objects among the arguments, one bit each
        uint16_t objects;
        // the D3DX handles among the arguments, one bit each (a handle may be a parameter name instead, see isName)
        uint16_t handles;
        Block block[2];
    };

    const Layout &layoutOf(uint32_t method);

    // a D3DX handle that is a parameter name (D3DX tells them apart the same way: its own handles have the top bit and the two low
    // bits set)
    inline bool isName(const uint32_t handle) {
        return handle != 0 && (handle & 0x80000003u) != 0x80000003u;
    }

    template<typename Function>
    Function slotOf(const uint32_t object, const size_t slot) {
        return reinterpret_cast<Function>((*reinterpret_cast<uint32_t *const *>(object))[slot]);
    }

    struct Range {
        uint32_t start;
        uint32_t end;

        bool contains(const uint32_t address) const {
            return address >= start && address < end;
        }
    };

    Range moduleRange(HMODULE module);

    // an address in d3d9.dll or d3dx9_27.dll: a call from there is D3D9's or D3DX's own (D3DX sets the device up inside the effect
    // calls)
    bool fromD3d(uint32_t address);

    // Called with every hooked call before it runs; frame: the return address, the object, then the arguments; original: the method
    // the call runs (originalOf). Returns 0 to run the method, or else the address to run instead, with the same stack (it
    // has to take the same arguments); the wrappers (methods that hand out objects, BeginStateBlock, EndStateBlock) call it in place
    // of the method and still look at the result. On the game thread the client's own D3D calls and those made while it runs are
    // not passed to it.
    using Client = uint32_t (__cdecl *)(uint32_t method, uint32_t *frame, uint32_t original);

    struct HookCounts {
        uint32_t vtables;
        // state block recordings begun (BeginStateBlock calls that succeeded)
        uint32_t recordings;
        // vtables not hooked: a slot outside the loaded modules, or no room left
        uint32_t refused;
    };

    // On the game thread, once: false if a client is already set. `counts` may be null.
    bool start(Client client, HookCounts *counts);

    // the device, once it exists (the D3DX imports are hooked with it); later calls change nothing
    void hookDevice(uint32_t device);
    void hookEffect(uint32_t vtable);

    // the device still leads to the hooked copy (a new device has a table of its own, even where the old one was)
    bool deviceHooked(uint32_t device);

    // the method a call of the slot runs (what the object's class had there before it was hooked), 0 for a class not hooked
    uint32_t originalOf(uint32_t object, uint32_t method);
    // the same by the slot of the object's own vtable, whichever interface it has
    uint32_t originalSlotOf(uint32_t object, uint32_t slot);

    // a thunk for `method` that runs *original (a stand-in method, for calibrations); 0 when there is no room left
    uint32_t thunkFor(uint32_t method, const uint32_t *original);

    // executable memory from the thunks' block, nullptr when there is no room left
    BYTE *codeSpace(size_t bytes);

    uint32_t gameThread();
}
