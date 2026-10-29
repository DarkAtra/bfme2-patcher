#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "render_scope_names.h"

// Render scope names
//
// Problem: the renderer builds a profiler event name for every render scope it opens, several hundred bytes of string
// copying per mesh and frame, although nothing reads the names unless a profiler such as PIX is attached.
// Fix: skips building the names. The only loss is that a PIX capture no longer shows the stage names.
//
// The render wraps thirty of its stages in a scope object (PerfScope, 0x00517690) that builds an event name for
// D3DPERF_BeginEvent. For the two scopes that are opened per mesh that is two strncpy calls, one of which pads to 256
// bytes, plus a strlen and a name widening, i.e. several hundred bytes of string work per mesh and frame. With no
// profiler attached D3DPERF_BeginEvent returns without reading the name, and nothing else reads the buffer (the 64
// references to the scope objects in the eight functions that create them are all the constructor and destructor
// calls), so the name is never used.
//
// The constructor already has an exit that returns the object untouched, taken for a null name. This patch takes it
// for every scope. The destructor still calls D3DPERF_EndEvent, which does nothing without a profiler. The cost is that
// a PIX capture no longer shows the stage names. Derived from pySAGE's perf-scope-skip (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t SCOPE_CONSTRUCTOR = 0x00517690;
    constexpr uintptr_t SCOPE_NULL_NAME_BRANCH = 0x00517699;
    constexpr uintptr_t SCOPE_EXIT = 0x00517731;
}

bool applyRenderScopeNames() {

    static PatchSet set;
    set.reset();

    // mov eax, [esp + 4] / test eax, eax / push esi / mov esi, ecx
    const BYTE constructorHead[9] = {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x56, 0x8B, 0xF1};
    set.expect(SCOPE_CONSTRUCTOR, constructorHead);

    // je SCOPE_EXIT, which becomes a jmp to it
    const BYTE nullNameBranch[6] = {0x0F, 0x84, 0x92, 0x00, 0x00, 0x00};
    set.hook(SCOPE_NULL_NAME_BRANCH, nullNameBranch, SCOPE_EXIT);

    // mov eax, esi / pop esi / ret 0xC
    const BYTE scopeExit[6] = {0x8B, 0xC6, 0x5E, 0xC2, 0x0C, 0x00};
    set.expect(SCOPE_EXIT, scopeExit);

    return commit(set, Cave(nullptr));
}
