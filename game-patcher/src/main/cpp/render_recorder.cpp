#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "d3d_methods.h"
#include "features.h"
#include "render_queue.h"
#include "render_recorder.h"

// What happens to a call of the game thread, by the kind bfme2-accelerator gives its method (d3d_methods.h):
// * A (state setters, draws, effect passes) and the frequent hand-written ones (SetStreamSource, SetIndices, SetVertexDeclaration,
//   the effect's setters, SetTechnique and Begin / Apply / DeleteParameterBlock) are recorded: the method, its arguments and the
//   bytes they point to (a parameter or technique name given as an effect handle too); a record holds no reference to its COM object
//   arguments (see R). The game gets D3D_OK back.
// * R (a resource's Release): AddRef now and two Releases recorded, so the object lives until the calls recorded before have run;
//   the game gets the count it would have got. That count is only known while no record that changes it waits to run (an earlier
//   Release of the object, or of its texture for a level, a call that changes counts: countsChange, as the effect's SetTexture);
//   otherwise it waits.
// * D runs at once (the device is created thread-safe).
// * L runs at once under the exec lock, between the worker's batches; its creations of video memory resources wait, as the other
//   creations do.
// * Begin of an effect: its pass count is the technique's, which nothing recorded changes. The first Begin of an effect's technique
//   waits and runs on the game thread; later ones are recorded and hand back the count the first one did. D3DX records state blocks
//   inside Begin and BeginPass, on the worker too: the device's hooked vtable is a copy D3D9 does not rewrite for them
//   (d3d_methods.h), so the game thread's calls meanwhile are still recorded.
// * The effect's descriptions and its other questions whose answer does not change while it lives (QUESTIONS): the first per effect
//   and question waits, later ones hand back its answer at once.
// * Locks of a vertex or index buffer and of a surface the CPU owns hand out a mirror of its bytes once it has one; the last Unlock
//   records the bytes the locks spanned (see the staged locks below).
// * SetRenderTarget and SetDepthStencilSurface are recorded; GetRenderTarget and GetDepthStencilSurface hand back what was set.
// * EndParameterBlock is recorded and hands out a stand-in for the block (see the parameter blocks below).
// * A Release whose count the caller does not read (COUNT_UNREAD) needs no wait while such records are pending.
// * Present is recorded, with at most one frame in flight; it hands back the result of the last Present the worker ran, so a lost
//   device shows one frame later. Reset waits and runs now.
// * Everything else (S, the other hand-written ones, the D3DX functions) waits until the worker has run everything recorded and
//   then runs on the game thread.
// Calls of the worker (D3DX's device calls inside a recorded effect call, the Releases) run as they come, and so do the calls D3D9
// and D3DX make themselves on the game thread inside a call that runs there now. Calls of other threads
// (the load screen's, which holds the renderer's lock meanwhile) wait until the worker has run everything recorded and run under
// the exec lock.
// The worker runs the records with the game thread's x87 control word and MXCSR at the time they were recorded.

namespace render_recorder {

    namespace {

        constexpr uint32_t CALL = 1;
        constexpr uint32_t NOTHING = 0;
        constexpr uint32_t RING_BYTES = 32 * 1024 * 1024;
        constexpr uint32_t MAX_ARGUMENTS = 12;
        constexpr uint32_t MAX_COPIES = 4;
        // a call carrying more waits instead
        constexpr uint32_t MAX_DATA = 4 * 1024 * 1024;
        constexpr uint32_t FX = 120;
        constexpr uint32_t FX_RELEASE = FX + 2;
        constexpr uint32_t SET_TECHNIQUE = FX + 58;
        constexpr uint32_t BEGIN = FX + 63;

        struct Call {
            uint32_t function;
            uint32_t method;
            // the values: the object, then the arguments
            uint32_t count;
            // bit i: values[i] (past `count` too) is counted as held for this record (held below); the worker counts it out after the
            // call. COUNT_CHANGE: the call changes reference counts (countsChange)
            uint32_t retires;
            uint32_t values[1];
        };

        constexpr uint32_t callBytes(const uint32_t count) {
            return static_cast<uint32_t>(offsetof(Call, values)) + 4 * count;
        }

        // the call in a reserved record, its header filled (the ring holds old records' bytes after its first lap)
        Call *callIn(render_queue::Record *record, const uint32_t function, const uint32_t method, const uint32_t count) {
            auto *call = reinterpret_cast<Call *>(record + 1);
            call->function = function;
            call->method = method;
            call->count = count;
            call->retires = 0;
            return call;
        }

        struct Copy {
            // the value that points to the bytes
            uint32_t value;
            uint32_t from;
            uint32_t bytes;
        };

        Counts tallies = {};
        // the counts are kept only in a build with GAME_PATCHER_RENDER_THREAD_COUNTS; tallies.hooks, which the hooks keep as they hook,
        // always (it is read below)
        constexpr bool counting = features::RENDER_THREAD_COUNTS;

        void tally(uint32_t &counter, const uint32_t by = 1) {
            if (counting) {
                counter += by;
            }
        }

        bool started = false;
        bool active = false;

        // what the worker writes per record, on a cache line of its own: next to what the game thread uses per call, each write
        // would take that line away from it. runningMethod and ran only while counting
        struct alignas(64) WorkerState {
            // the method of the record the worker runs
            uint32_t runningMethod;
            // the records it ran (Counts::ran, copied there by counts())
            long ran;
            // the records with COUNT_CHANGE it ran
            volatile long countChangesRetired;
        };

        WorkerState worker = {};
        // the records with COUNT_CHANGE the game thread made
        uint32_t countChangesIssued = 0;
        constexpr uint32_t COUNT_CHANGE = 0x80000000u;

        // A call that changes reference counts when it runs: a parameter block's End, Apply and Delete (D3DX holds the textures a block
        // and the effect's parameters hold) and the effect's SetTexture (D3DX holds the new texture and lets go of the one the parameter
        // held). While one waits to run, a count handed back could be off (release). D3D9's state blocks hold no counted reference to
        // what they capture (the test), so neither D3DX's Begin that saves state nor a state block's Capture is one.
        void countsChange(Call *call) {
            call->retires |= COUNT_CHANGE;
            countChangesIssued++;
        }
        // what the skipped method returns to the game
        uint32_t skipResult = 0;

        template<size_t>
        using Word = uint32_t;

        template<size_t... I>
        uint32_t __stdcall skipped(uint32_t, Word<I>...) {
            return skipResult;
        }

        template<size_t... I>
        uint32_t skipperOf(std::index_sequence<I...>) {
            return reinterpret_cast<uint32_t>(&skipped<I...>);
        }

        template<size_t... I>
        uint32_t invokeWith(const uint32_t function, const uint32_t *values, std::index_sequence<I...>) {
            return reinterpret_cast<uint32_t (__stdcall *)(Word<I>...)>(function)(values[I]...);
        }

        template<size_t COUNT>
        uint32_t invoke(const uint32_t function, const uint32_t *values) {
            return invokeWith(function, values, std::make_index_sequence<COUNT>{});
        }

        using Invoker = uint32_t (*)(uint32_t, const uint32_t *);

        // by the number of arguments after the object
        uint32_t skippers[MAX_ARGUMENTS + 1];
        // by the number of values
        Invoker invokers[MAX_ARGUMENTS + 2];

        // A locked call: the method under the exec lock, so that it runs between the worker's batches. The method travels per thread
        // (the game thread's kind L calls, the other threads' calls).
        __declspec(thread) uint32_t lockedTarget = 0;

        template<size_t... I>
        uint32_t __stdcall locked(uint32_t object, Word<I>... arguments) {
            const uint32_t target = lockedTarget;
            render_queue::lock();
            const uint32_t result = reinterpret_cast<uint32_t (__stdcall *)(uint32_t, Word<I>...)>(target)(object, arguments...);
            render_queue::unlock();
            return result;
        }

        template<size_t... I>
        uint32_t lockerOf(std::index_sequence<I...>) {
            return reinterpret_cast<uint32_t>(&locked<I...>);
        }

        // by the number of arguments after the object
        uint32_t lockers[MAX_ARGUMENTS + 1];

        template<size_t... N>
        void fillTables(std::index_sequence<N...>) {
            const uint32_t skip[] = {skipperOf(std::make_index_sequence<N>{})...};
            const uint32_t lock[] = {lockerOf(std::make_index_sequence<N>{})...};
            const Invoker run[] = {&invoke<N + 1>...};
            for (size_t i = 0; i <= MAX_ARGUMENTS; i++) {
                skippers[i] = skip[i];
                lockers[i] = lock[i];
                invokers[i + 1] = run[i];
            }
        }

        // ---- the recorded Releases still to run, per object (a texture's for its levels): the game thread counts them in (`issued`,
        // as it records), the worker out (`retired`, once one has run). A slot whose
        // counts are equal holds nothing and may be taken for another object; only the game thread writes keys and `issued`, only
        // the worker `retired`.

        constexpr uint32_t HELD_SLOTS = 16384;
        constexpr uint32_t HELD_PROBES = 32;

        struct Held {
            volatile uint32_t object;
            volatile uint32_t issued;
            volatile uint32_t retired;
        };

        Held held[HELD_SLOTS];

        uint32_t heldStart(const uint32_t object) {
            return ((object >> 4) * 2654435761u) >> 18;
        }

        // game thread: the object's slot, a free one taken for it when `take`; nullptr when its probes are all held by others
        Held *heldOf(const uint32_t object, const bool take) {
            const uint32_t start = heldStart(object);
            Held *free = nullptr;
            for (uint32_t i = 0; i < HELD_PROBES; i++) {
                Held &entry = held[(start + i) & (HELD_SLOTS - 1)];
                if (entry.object == object) {
                    return &entry;
                }
                if (free == nullptr && entry.issued == entry.retired) {
                    free = &entry;
                }
            }
            if (take && free != nullptr) {
                free->object = object;
            }
            return take ? free : nullptr;
        }

        bool isHeld(const uint32_t object) {
            const Held *entry = heldOf(object, false);
            return entry != nullptr && entry->issued != entry->retired;
        }

        // worker: the object's slot is the one the game thread counted it in (it is not taken while the object is held)
        void retire(const uint32_t object) {
            const uint32_t start = heldStart(object);
            for (uint32_t i = 0; i < HELD_PROBES; i++) {
                Held &entry = held[(start + i) & (HELD_SLOTS - 1)];
                if (entry.object == object && entry.issued != entry.retired) {
                    entry.retired = entry.retired + 1;
                    return;
                }
            }
        }

        // ---- the floating-point state: D3DX runs the effects' preshaders with the calling thread's x87 control word and MXCSR
        // (D3D9 sets the creating thread's control word once, at CreateDevice, and a new thread starts with another one), so the
        // worker takes the game thread's, sent as a record ahead of the first call recorded under a new state. MXCSR's sticky
        // exception flags are left out.

        constexpr uint32_t STATE = 2;
        constexpr uint32_t MXCSR_CONTROL = 0xFFC0;

        struct FloatState {
            uint32_t controlWord;
            uint32_t mxcsr;
        };

        // both in one word, MXCSR's control bits high: what was sent last
        uint32_t sentState = 0xFFFFFFFF;

        // the x87 control word, MXCSR's control bits high; on its own because a function with inline assembly is never inlined and
        // spills around it
        __declspec(naked) uint32_t __stdcall floatState() {
            __asm {
                sub esp, 8
                fnstcw word ptr [esp]
                stmxcsr dword ptr [esp + 4]
                movzx eax, word ptr [esp]
                mov ecx, dword ptr [esp + 4]
                and ecx, 0FFC0h
                shl ecx, 16
                or eax, ecx
                add esp, 8
                ret
            }
        }

        void sendFloatState() {
            const uint32_t now = floatState();
            if (now == sentState) {
                return;
            }
            const uint32_t controlWord = now & 0xFFFF;
            const uint32_t mxcsr = now >> 16;
            render_queue::Record *record = render_queue::reserve(STATE, sizeof(FloatState));
            auto *state = reinterpret_cast<FloatState *>(record + 1);
            state->controlWord = controlWord;
            state->mxcsr = mxcsr;
            render_queue::commit();
            sentState = now;
            tally(tallies.stateRecords);
        }

        void loadFloatState(const FloatState &state) {
            uint16_t controlWord = static_cast<uint16_t>(state.controlWord);
            __asm fldcw controlWord
            _mm_setcsr((_mm_getcsr() & ~MXCSR_CONTROL) | state.mxcsr);
        }

        // the result of the last Present the worker ran
        volatile LONG presentResult = 0;

        bool run(const render_queue::Record *record) {
            if (record->tag == CALL) {
                const auto *call = reinterpret_cast<const Call *>(record + 1);
                if (counting) {
                    worker.runningMethod = call->method;
                }
                const uint32_t result = invokers[call->count](call->function, call->values);
                for (uint32_t retires = call->retires & ~COUNT_CHANGE; retires != 0; retires &= retires - 1) {
                    retire(call->values[_tzcnt_u32(retires)]);
                }
                if ((call->retires & COUNT_CHANGE) != 0) {
                    worker.countChangesRetired = worker.countChangesRetired + 1;
                }
                if (counting) {
                    worker.ran++;
                }
                if (call->method == d3d::PRESENT || call->method == d3d::SWAP_CHAIN_PRESENT) {
                    presentResult = static_cast<LONG>(result);
                    // the game thread may wait for this one
                    return true;
                }
            } else if (record->tag == STATE) {
                loadFloatState(*reinterpret_cast<const FloatState *>(record + 1));
            }
            return false;
        }

        // the game thread's call being handled, for the waits by method
        uint32_t handling = 0;
        uint32_t handlingFrom = 0;

        // a wait, by method and by caller
        void countWait() {
            tallies.waited++;
            if (handling < d3d::METHODS + d3d::D3DX_FUNCTIONS) {
                tallies.waitsBy[handling]++;
            }
            size_t at = 0;
            size_t fewest = 0;
            while (at < WAIT_CALLERS && tallies.waitCallers[at].address != 0
                   && (tallies.waitCallers[at].address != handlingFrom || tallies.waitCallers[at].method != handling)) {
                if (tallies.waitCallers[at].waits < tallies.waitCallers[fewest].waits) {
                    fewest = at;
                }
                at++;
            }
            if (at == WAIT_CALLERS) {
                at = fewest;
                tallies.otherWaitCallers++;
            }
            tallies.waitCallers[at].address = handlingFrom;
            tallies.waitCallers[at].method = handling;
            tallies.waitCallers[at].waits++;
        }

        uint32_t wait() {
            if (counting) {
                countWait();
            }
            render_queue::drain();
            return 0;
        }

        // waits, runs the method on the game thread now and hands its result back
        uint32_t runNow(const uint32_t arguments, const uint32_t *frame, const uint32_t original) {
            wait();
            skipResult = invokers[1 + arguments](original, frame + 1);
            return skippers[arguments];
        }

        uint32_t alignUp(const uint32_t value) {
            return (value + 15) & ~15u;
        }

        // a string's bytes with its terminator; 0 when it is longer than a record carries or cannot be read
        uint32_t textBytes(const uint32_t text) {
            __try {
                const size_t length = strnlen(reinterpret_cast<const char *>(text), MAX_DATA);
                return length == MAX_DATA ? 0 : static_cast<uint32_t>(length + 1);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

        // the bytes a call's arguments point to; false when they are more than a record carries or cannot be read
        bool plan(const d3d::Layout &layout, const uint32_t *frame, Copy (&copies)[MAX_COPIES], uint32_t &count, uint32_t &bytes) {
            count = 0;
            bytes = 0;
            for (uint32_t b = 0; b < layout.blocks; b++) {
                const d3d::Block &block = layout.block[b];
                const uint32_t from = frame[2 + block.pointer];
                if (from == 0) {
                    continue;
                }
                const uint64_t size = static_cast<uint64_t>(block.bytes) * (block.count == d3d::NO_COUNT ? 1 : frame[2 + block.count]);
                if (size > MAX_DATA) {
                    return false;
                }
                copies[count++] = {1u + block.pointer, from, static_cast<uint32_t>(size)};
            }
            uint32_t texts = layout.string != 0 ? 1u << (layout.string - 1) : 0;
            for (uint32_t handles = layout.handles; handles != 0; handles &= handles - 1) {
                const uint32_t argument = _tzcnt_u32(handles);
                if (d3d::isName(frame[2 + argument])) {
                    texts |= 1u << argument;
                }
            }
            for (; texts != 0; texts &= texts - 1) {
                const uint32_t argument = _tzcnt_u32(texts);
                const uint32_t value = frame[2 + argument];
                if (value == 0) {
                    continue;
                }
                if (count == MAX_COPIES) {
                    return false;
                }
                const uint32_t length = textBytes(value);
                if (length == 0) {
                    return false;
                }
                copies[count++] = {1u + argument, value, length};
            }
            for (uint32_t i = 0; i < count; i++) {
                bytes += copies[i].bytes + 15;
            }
            return bytes <= MAX_DATA;
        }

        bool copyData(const Copy (&copies)[MAX_COPIES], const uint32_t count, Call *call, uint32_t at) {
            __try {
                for (uint32_t i = 0; i < count; i++) {
                    at = alignUp(at);
                    memcpy(reinterpret_cast<void *>(at), reinterpret_cast<const void *>(copies[i].from), copies[i].bytes);
                    call->values[copies[i].value] = at;
                    at += copies[i].bytes;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        }

        uint32_t record(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const d3d::Layout &layout = d3d::layoutOf(method);
            if (layout.arguments > MAX_ARGUMENTS) {
                return wait();
            }
            Copy copies[MAX_COPIES];
            uint32_t copyCount = 0;
            uint32_t dataBytes = 0;
            if ((layout.blocks != 0 || layout.string != 0 || layout.handles != 0) && !plan(layout, frame, copies, copyCount, dataBytes)) {
                return wait();
            }
            // A record holds no reference to its objects: every Release of the game thread keeps its object alive behind everything
            // recorded before (release).
            const uint32_t count = 1u + layout.arguments;
            sendFloatState();
            render_queue::Record *record = render_queue::reserve(CALL, callBytes(count) + dataBytes);
            if (record == nullptr) {
                return wait();
            }
            Call *call = callIn(record, original, method, count);
            memcpy(call->values, frame + 1, 4 * count);
            if (copyCount != 0 && !copyData(copies, copyCount, call, reinterpret_cast<uint32_t>(call) + callBytes(count))) {
                record->tag = NOTHING;
                render_queue::commit();
                return wait();
            }
            // FX + 75, FX + 76: ApplyParameterBlock and DeleteParameterBlock of a block D3DX handed out itself
            if (method == d3d::EFFECT_SET_TEXTURE || method == FX + 75 || method == FX + 76) {
                countsChange(call);
            }
            render_queue::commit();
            tally(tallies.recorded);
            skipResult = 0;
            return skippers[layout.arguments];
        }

        // the game's Release calls whose count no path reads (tools/analysis/EaxLiveness.java on the exe, 2026-11-14): their return
        // addresses. Two more by what returns them: 0x0057655C ends 0x00576550, the destructor of a COM pointer wrapper (the CRT's
        // vector destructor iterator gets it as the element destructor), and 0x0051CD28 is in DX8Wrapper::Clear (0x0051CCD0); both
        // return nothing
        constexpr uint32_t COUNT_UNREAD[] = {
            0x00530CD2, 0x0051C69C, 0x0051DD31, 0x00477103, 0x00477167, 0x00477135, 0x00532604, 0x0053241C, 0x00554940, 0x00554872,
            0x0051DC7A, 0x0051DC8F, 0x0051DD46, 0x0051DD5B, 0x0051DE0A, 0x0051DE1F, 0x0051DEC5, 0x0051DEDA, 0x0051DEEF,
            0x004FA1B0, 0x004FA1BD, 0x004FA1DD, 0x004FA1ED, 0x004FA1FD, 0x004FA20D, 0x004FA2BA, 0x004FA2FF, 0x004FA6AC, 0x004FA82E,
            0x004FA9D0, 0x004FAB90, 0x004FABDE, 0x004FB1ED, 0x004FB1FA,
            0x00476E0B, 0x00476E4A, 0x004770D2, 0x004771B0, 0x004771EF, 0x0047722F, 0x0047726F,
            0x0057655C, 0x0051CD28,
            // the Releases that still waited in the battles of 2026-11-15 (around the text renderer and the texture loader)
            0x0055567E, 0x005548D2, 0x005586B5, 0x00555256, 0x00532425, 0x00531D0E,
            0x0058BDB6, 0x005557D4, 0x00539240,
            // returned by 0x0051DBF0 (the tool took 0x0051DCEC, a label after a ret, for a function): its 24 callers do not read it,
            // and its address appears nowhere as data
            0x0051DD70,
        };

        bool countUnread(const uint32_t from) {
            for (const uint32_t address : COUNT_UNREAD) {
                if (address == from) {
                    return true;
                }
            }
            return false;
        }

        // the object's method in `slot` of its vtable, past the hooks (also called on other threads, whose hooked calls would wait)
        template<typename Function>
        Function unhooked(const uint32_t object, const uint32_t slot) {
            const uint32_t original = d3d::originalSlotOf(object, slot);
            return original != 0 ? reinterpret_cast<Function>(original) : d3d::slotOf<Function>(object, slot);
        }

        constexpr GUID BASE_TEXTURE_INTERFACE = {0x580ca87e, 0x1d3c, 0x4d54, {0x99, 0x1d, 0xb7, 0xd3, 0xe3, 0xc2, 0x98, 0xce}};

        // the object whose count a Release changes: a texture's level surface or volume has its texture's count (the test)
        uint32_t countOwner(const uint32_t method, const uint32_t object) {
            // GetContainer
            const uint32_t slot = method == 290 + 2 ? 11 : method == 310 + 2 ? 7 : 0;
            uint32_t container = 0;
            if (slot == 0 || FAILED(unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, uint32_t *)>(object, slot)(object, &BASE_TEXTURE_INTERFACE,
                                                                                                                      &container))
                || container == 0) {
                return object;
            }
            unhooked<ULONG (__stdcall *)(uint32_t)>(container, 2)(container);
            return container;
        }

        // A Release: with no record pending that changes the count of the object (held, by its texture for a level) or counts beyond
        // its arguments (countsChange), the count after the AddRef here, less the game's reference and this one, is the count the game
        // would get. Otherwise that count is not known before the worker has run, so it waits, unless the caller does not read it.
        uint32_t release(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const uint32_t object = frame[1];
            const uint32_t owner = countOwner(method, object);
            const bool unknown = isHeld(owner) || countChangesIssued != static_cast<uint32_t>(worker.countChangesRetired);
            Held *holder = unknown && !countUnread(frame[0]) ? nullptr : heldOf(owner, true);
            if (holder == nullptr) {
                tally(tallies.releasesWaited);
                return runNow(0, frame, original);
            }
            const ULONG references = d3d::slotOf<ULONG (__stdcall *)(uint32_t)>(object, 1)(object);
            sendFloatState();
            for (int i = 0; i < 2; i++) {
                render_queue::Record *record = render_queue::reserve(CALL, callBytes(2));
                Call *call = callIn(record, original, method, 1);
                call->values[0] = object;
                // past the call's values: counted out by the worker
                call->values[1] = owner;
                call->retires = 1u << 1;
                holder->issued = holder->issued + 1;
                render_queue::commit();
            }
            tally(tallies.recorded, 2);
            skipResult = references - 2;
            return skippers[0];
        }

        // ---- effects: the technique set on each, and the pass count Begin handed back for it (the technique's: nothing recorded
        // changes it), so that only the first Begin of a technique waits.

        constexpr size_t MAX_EFFECTS = 1024;
        constexpr size_t MAX_SEEN = 4096;

        struct Current {
            uint32_t effect;
            // 0 for a technique set by name
            uint32_t technique;
            // a new number each time the effect is remembered again after it was forgotten
            uint32_t lifetime;
        };

        struct Seen {
            uint32_t effect;
            uint32_t technique;
            uint32_t passes;
        };

        Current currents[MAX_EFFECTS];
        size_t currentCount = 0;
        uint32_t lifetimes = 0;
        Seen seen[MAX_SEEN];
        size_t seenCount = 0;

        Current *currentOf(const uint32_t effect) {
            for (size_t i = 0; i < currentCount; i++) {
                if (currents[i].effect == effect) {
                    return &currents[i];
                }
            }
            if (currentCount == MAX_EFFECTS) {
                return nullptr;
            }
            currents[currentCount] = {effect, 0, ++lifetimes};
            return &currents[currentCount++];
        }

        uint32_t techniqueOf(const uint32_t effect) {
            const Current *current = currentOf(effect);
            return current != nullptr ? current->technique : 0;
        }

        void setTechnique(const uint32_t effect, const uint32_t handle) {
            Current *current = currentOf(effect);
            if (current != nullptr) {
                current->technique = d3d::isName(handle) ? 0 : handle;
            }
        }

        const Seen *seenOf(const uint32_t effect, const uint32_t technique) {
            for (size_t i = 0; i < seenCount; i++) {
                const Seen &entry = seen[i];
                if (entry.effect == effect && entry.technique == technique) {
                    return &entry;
                }
            }
            return nullptr;
        }

        void remember(const uint32_t effect, const uint32_t technique, const uint32_t passes) {
            if (seenCount < MAX_SEEN) {
                seen[seenCount++] = {effect, technique, passes};
            }
        }

        // ---- parameter blocks: EndParameterBlock hands out a block of its own (a stand-in) at once and records the real End; the
        // worker keeps the handle D3DX gave for it, and a recorded ApplyParameterBlock / DeleteParameterBlock of a stand-in runs
        // with that handle. Where one runs at once (after a wait, or on another thread), the stand-in in its arguments is swapped for
        // the handle first. The game keeps the handle, tests it for null and hands it back to those two (material_parameters.cpp, the
        // material's +0x24).

        constexpr uint32_t END_PARAMETER_BLOCK = FX + 74;
        constexpr uint32_t APPLY_PARAMETER_BLOCK = FX + 75;
        constexpr uint32_t DELETE_PARAMETER_BLOCK = FX + 76;
        constexpr uint32_t MAX_BLOCKS = 4096;
        // a D3DX handle is a pointer, or a pointer | 3: never this low nibble
        constexpr uint32_t STAND_IN = 0xB10C0001;

        // game thread: the effect each stand-in belongs to, 0 for a free one
        uint32_t blockOwners[MAX_BLOCKS];
        uint32_t freeBlocks[MAX_BLOCKS];
        uint32_t freeBlockCount = 0;
        uint32_t blocksUsed = 0;
        // worker: the handle D3DX gave for each
        volatile uint32_t blockHandles[MAX_BLOCKS];

        bool standIn(const uint32_t handle, uint32_t &index) {
            index = handle >> 4 & 0xFFF;
            return (handle & 0xFFFF000F) == STAND_IN && blockOwners[index] != 0;
        }

        void freeBlock(const uint32_t index) {
            blockOwners[index] = 0;
            freeBlocks[freeBlockCount++] = index;
        }

        uint32_t __stdcall endBlock(const uint32_t effect, const uint32_t index, const uint32_t original) {
            blockHandles[index] = reinterpret_cast<uint32_t (__stdcall *)(uint32_t)>(original)(effect);
            return 0;
        }

        uint32_t __stdcall onBlock(const uint32_t effect, const uint32_t index, const uint32_t original) {
            return static_cast<uint32_t>(reinterpret_cast<HRESULT (__stdcall *)(uint32_t, uint32_t)>(original)(effect, blockHandles[index]));
        }

        bool recordBlockCall(void *function, const uint32_t method, const uint32_t effect, const uint32_t index, const uint32_t original) {
            render_queue::Record *record = render_queue::reserve(CALL, callBytes(3));
            if (record == nullptr) {
                return false;
            }
            Call *call = callIn(record, reinterpret_cast<uint32_t>(function), method, 3);
            call->values[0] = effect;
            call->values[1] = index;
            call->values[2] = original;
            countsChange(call);
            render_queue::commit();
            tally(tallies.recorded);
            return true;
        }

        // EndParameterBlock(effect)
        uint32_t endParameterBlock(const uint32_t *frame, const uint32_t original) {
            if (freeBlockCount == 0 && blocksUsed == MAX_BLOCKS) {
                return runNow(0, frame, original);
            }
            const uint32_t index = freeBlockCount != 0 ? freeBlocks[--freeBlockCount] : blocksUsed++;
            if (!recordBlockCall(reinterpret_cast<void *>(&endBlock), END_PARAMETER_BLOCK, frame[1], index, original)) {
                freeBlocks[freeBlockCount++] = index;
                return runNow(0, frame, original);
            }
            blockOwners[index] = frame[1];
            skipResult = STAND_IN + (index << 4);
            return skippers[0];
        }

        // another thread's (or a waiting) Apply / Delete runs with the handle in place of the stand-in
        void swapStandIn(const uint32_t method, uint32_t *frame) {
            uint32_t index;
            if ((method == APPLY_PARAMETER_BLOCK || method == DELETE_PARAMETER_BLOCK) && standIn(frame[2], index)) {
                frame[2] = blockHandles[index];
                if (method == DELETE_PARAMETER_BLOCK) {
                    freeBlock(index);
                }
            }
        }

        // ApplyParameterBlock(effect, block), DeleteParameterBlock(effect, block)
        uint32_t onParameterBlock(const uint32_t method, uint32_t *frame, const uint32_t original) {
            uint32_t index;
            if (!standIn(frame[2], index)) {
                return record(method, frame, original);
            }
            if (!recordBlockCall(reinterpret_cast<void *>(&onBlock), method, frame[1], index, original)) {
                wait();
                swapStandIn(method, frame);
                return 0;
            }
            if (method == DELETE_PARAMETER_BLOCK) {
                freeBlock(index);
            }
            skipResult = 0;
            return skippers[1];
        }

        void forget(const uint32_t effect) {
            for (uint32_t i = 0; i < blocksUsed; i++) {
                if (blockOwners[i] == effect) {
                    freeBlock(i);
                }
            }
            for (size_t i = 0; i < currentCount;) {
                if (currents[i].effect == effect) {
                    currents[i] = currents[--currentCount];
                } else {
                    i++;
                }
            }
            for (size_t i = 0; i < seenCount;) {
                if (seen[i].effect == effect) {
                    seen[i] = seen[--seenCount];
                } else {
                    i++;
                }
            }
        }

        // Begin(effect, &passes, flags)
        uint32_t begin(uint32_t *frame, const uint32_t original) {
            const uint32_t effect = frame[1];
            const uint32_t technique = techniqueOf(effect);
            const Seen *before = technique != 0 ? seenOf(effect, technique) : nullptr;
            if (before == nullptr) {
                const uint32_t target = runNow(2, frame, original);
                if (SUCCEEDED(static_cast<HRESULT>(skipResult)) && technique != 0 && frame[2] != 0) {
                    remember(effect, technique, *reinterpret_cast<const uint32_t *>(frame[2]));
                }
                return target;
            }
            sendFloatState();
            render_queue::Record *record = render_queue::reserve(CALL, callBytes(3) + 16);
            Call *call = callIn(record, original, BEGIN, 3);
            call->values[0] = effect;
            // D3DX writes the pass count into the record
            call->values[1] = alignUp(reinterpret_cast<uint32_t>(call) + callBytes(3));
            call->values[2] = frame[3];
            render_queue::commit();
            tally(tallies.recorded);
            if (frame[2] != 0) {
                *reinterpret_cast<uint32_t *>(frame[2]) = before->passes;
            }
            skipResult = 0;
            return skippers[2];
        }

        // ---- the descriptions (of the effect, a parameter, a technique, a pass: names, counts, classes, types, sizes, flags) do not
        // change while the effect lives, so the first one per effect and handle is kept and handed back without a wait. A handle that
        // is a string waits.

        constexpr uint32_t GET_DESC = FX + 3;
        constexpr uint32_t GET_PARAMETER_DESC = FX + 4;
        constexpr uint32_t GET_TECHNIQUE_DESC = FX + 5;
        constexpr uint32_t GET_PASS_DESC = FX + 6;
        // the values of an annotation (see the annotations below)
        constexpr uint32_t GET_BOOL = FX + 23;
        constexpr uint32_t GET_INT = FX + 27;
        constexpr uint32_t GET_FLOAT = FX + 31;
        constexpr uint32_t GET_VECTOR = FX + 35;
        constexpr uint32_t GET_STRING = FX + 51;
        constexpr size_t DESCRIBED = 8192;
        constexpr size_t DESCRIBED_PROBES = 8;
        // the largest, D3DXPARAMETER_DESC
        constexpr size_t DESC_BYTES = 44;

        // the bytes d3dx9_27 writes (D3DXEFFECT_DESC, D3DXPARAMETER_DESC, D3DXTECHNIQUE_DESC, D3DXPASS_DESC measured; a BOOL, INT,
        // FLOAT, string pointer, D3DXVECTOR4)
        size_t describedBytes(const uint32_t method) {
            switch (method) {
                case GET_PARAMETER_DESC:
                    return 44;
                case GET_TECHNIQUE_DESC:
                    return 12;
                case GET_BOOL:
                case GET_INT:
                case GET_FLOAT:
                case GET_STRING:
                    return 4;
                default:
                    return 16;
            }
        }

        struct Described {
            uint32_t effect;
            uint32_t method;
            uint32_t handle;
            // the effect's Current::lifetime when it was kept
            uint32_t lifetime;
            uint8_t desc[DESC_BYTES];
        };

        Described described[DESCRIBED];
        size_t describedInserts = 0;

        size_t describedStart(const uint32_t effect, const uint32_t handle, const uint32_t method) {
            return ((effect >> 4) * 2654435761u ^ handle * 40503u ^ method * 69069u) & (DESCRIBED - 1);
        }

        // GetDesc(effect, &desc), GetParameterDesc / GetTechniqueDesc / GetPassDesc(effect, handle, &desc), an annotation's
        // GetBool / GetInt / GetFloat / GetVector / GetString(effect, handle, &value)
        uint32_t describe(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const bool own = method == GET_DESC;
            const uint32_t arguments = own ? 1 : 2;
            const uint32_t effect = frame[1];
            const uint32_t handle = own ? 0 : frame[2];
            const uint32_t out = frame[1 + arguments];
            const size_t bytes = describedBytes(method);
            const Current *current = (own || (handle != 0 && !d3d::isName(handle))) && out != 0 ? currentOf(effect) : nullptr;
            if (current == nullptr) {
                return runNow(arguments, frame, original);
            }
            const uint32_t lifetime = current->lifetime;
            const size_t start = describedStart(effect, handle, method);
            for (size_t probe = 0; probe < DESCRIBED_PROBES; probe++) {
                const Described &entry = described[(start + probe) & (DESCRIBED - 1)];
                if (entry.effect == effect && entry.method == method && entry.handle == handle && entry.lifetime == lifetime) {
                    memcpy(reinterpret_cast<void *>(out), entry.desc, bytes);
                    skipResult = 0;
                    return skippers[arguments];
                }
            }
            const uint32_t target = runNow(arguments, frame, original);
            if (SUCCEEDED(static_cast<HRESULT>(skipResult))) {
                size_t at = start + describedInserts++ % DESCRIBED_PROBES;
                for (size_t probe = 0; probe < DESCRIBED_PROBES; probe++) {
                    if (described[(start + probe) & (DESCRIBED - 1)].lifetime == 0) {
                        at = start + probe;
                        break;
                    }
                }
                Described &entry = described[at & (DESCRIBED - 1)];
                entry.effect = effect;
                entry.method = method;
                entry.handle = handle;
                entry.lifetime = lifetime;
                memcpy(entry.desc, reinterpret_cast<const void *>(out), bytes);
            }
            return target;
        }

        // ---- annotations: a handle GetAnnotation / GetAnnotationByName handed out keeps its value while the effect lives, so
        // GetBool / GetInt / GetFloat / GetVector / GetString of one go through describe. A Set of one takes the mark off.

        constexpr size_t ANNOTATIONS = 4096;
        constexpr size_t ANNOTATION_PROBES = 8;

        struct Annotation {
            uint32_t effect;
            // the effect's Current::lifetime when it was marked
            uint32_t lifetime;
            uint32_t handle;
        };

        Annotation annotations[ANNOTATIONS];
        size_t annotationInserts = 0;
        uint32_t annotationsMarked = 0;

        size_t annotationStart(const uint32_t effect, const uint32_t handle) {
            return ((effect >> 4) * 2654435761u ^ handle * 40503u) & (ANNOTATIONS - 1);
        }

        Annotation *annotationOf(const uint32_t effect, const uint32_t handle) {
            const Current *current = nullptr;
            for (size_t i = 0; i < currentCount && current == nullptr; i++) {
                if (currents[i].effect == effect) {
                    current = &currents[i];
                }
            }
            if (current == nullptr) {
                return nullptr;
            }
            const size_t start = annotationStart(effect, handle);
            for (size_t probe = 0; probe < ANNOTATION_PROBES; probe++) {
                Annotation &entry = annotations[(start + probe) & (ANNOTATIONS - 1)];
                if (entry.effect == effect && entry.handle == handle && entry.lifetime == current->lifetime) {
                    return &entry;
                }
            }
            return nullptr;
        }

        void markAnnotation(const uint32_t effect, const uint32_t lifetime, const uint32_t handle) {
            if (handle == 0 || annotationOf(effect, handle) != nullptr) {
                return;
            }
            const size_t start = annotationStart(effect, handle);
            size_t at = start + annotationInserts++ % ANNOTATION_PROBES;
            for (size_t probe = 0; probe < ANNOTATION_PROBES; probe++) {
                if (annotations[(start + probe) & (ANNOTATIONS - 1)].lifetime == 0) {
                    at = start + probe;
                    break;
                }
            }
            annotations[at & (ANNOTATIONS - 1)] = {effect, lifetime, handle};
            annotationsMarked++;
        }

        bool annotationValue(const uint32_t method) {
            return method == GET_BOOL || method == GET_INT || method == GET_FLOAT || method == GET_VECTOR || method == GET_STRING;
        }

        // the values kept for an annotation that was set
        void forgetDescribed(const uint32_t effect, const uint32_t handle) {
            for (const uint32_t method : {GET_BOOL, GET_INT, GET_FLOAT, GET_VECTOR, GET_STRING}) {
                const size_t start = describedStart(effect, handle, method);
                for (size_t probe = 0; probe < DESCRIBED_PROBES; probe++) {
                    Described &entry = described[(start + probe) & (DESCRIBED - 1)];
                    if (entry.effect == effect && entry.method == method && entry.handle == handle) {
                        entry.lifetime = 0;
                    }
                }
            }
        }

        // SetValue ... SetString, SetTexture (the even slots 20 to 52) and SetRawValue
        bool effectSetter(const uint32_t method) {
            return (method >= FX + 20 && method <= FX + 52 && (method - FX) % 2 == 0) || method == FX + 78;
        }

        // ---- the effect's questions whose answers do not change while it lives (the handles of its parameters, techniques, passes
        // and annotations by index, name or semantic; whether a technique uses a parameter): the first per effect and question is kept.
        // A string where a handle goes, or a name too long to keep, waits.

        constexpr uint32_t GET_PARAMETER = FX + 8;
        constexpr uint32_t GET_PARAMETER_BY_NAME = FX + 9;
        constexpr uint32_t GET_PARAMETER_BY_SEMANTIC = FX + 10;
        constexpr uint32_t GET_PARAMETER_ELEMENT = FX + 11;
        constexpr uint32_t GET_TECHNIQUE = FX + 12;
        constexpr uint32_t GET_TECHNIQUE_BY_NAME = FX + 13;
        constexpr uint32_t GET_PASS = FX + 14;
        constexpr uint32_t GET_ANNOTATION = FX + 18;
        constexpr uint32_t GET_ANNOTATION_BY_NAME = FX + 19;
        constexpr uint32_t IS_PARAMETER_USED = FX + 62;
        constexpr size_t ANSWERED = 4096;
        constexpr size_t ANSWERED_PROBES = 8;
        constexpr size_t MAX_NAME = 128;

        struct Question {
            uint32_t method;
            uint32_t arguments;
            // bit i: argument i is a handle
            uint32_t handles;
            // the argument that is a name, + 1 (0: none)
            uint32_t name;
        };

        constexpr Question QUESTIONS[] = {
            {GET_PARAMETER, 2, 0x1, 0},            // (parent, index)
            {GET_PARAMETER_BY_NAME, 2, 0x1, 2},    // (parent, name)
            {GET_PARAMETER_BY_SEMANTIC, 2, 0x1, 2}, // (parent, semantic)
            {GET_PARAMETER_ELEMENT, 2, 0x1, 0},    // (parameter, index)
            {GET_TECHNIQUE, 1, 0x0, 0},            // (index)
            {GET_TECHNIQUE_BY_NAME, 1, 0x0, 1},    // (name)
            {GET_PASS, 2, 0x1, 0},                 // (technique, index)
            {GET_ANNOTATION, 2, 0x1, 0},           // (object, index)
            {GET_ANNOTATION_BY_NAME, 2, 0x1, 2},   // (object, name)
            {IS_PARAMETER_USED, 2, 0x3, 0},        // (parameter, technique)
        };

        const Question *questionOf(const uint32_t method) {
            for (const Question &question : QUESTIONS) {
                if (question.method == method) {
                    return &question;
                }
            }
            return nullptr;
        }

        struct Answered {
            uint32_t effect;
            // the effect's Current::lifetime when it was kept
            uint32_t lifetime;
            uint32_t method;
            uint32_t first;
            // the technique, or the name's hash
            uint32_t second;
            uint32_t answer;
            char name[MAX_NAME];
        };

        Answered answered[ANSWERED];
        size_t answeredInserts = 0;

        uint32_t answer(const Question &question, const uint32_t *frame, const uint32_t original) {
            const uint32_t method = question.method;
            const uint32_t effect = frame[1];
            const char *name = question.name != 0 ? reinterpret_cast<const char *>(frame[1 + question.name]) : nullptr;
            const size_t length = name != nullptr ? strnlen(name, MAX_NAME) : 0;
            bool keepable = question.name == 0 || (name != nullptr && length < MAX_NAME);
            for (uint32_t i = 0; i < question.arguments; i++) {
                keepable = keepable && ((question.handles >> i & 1) == 0 || !d3d::isName(frame[2 + i]));
            }
            const Current *current = keepable ? currentOf(effect) : nullptr;
            if (current == nullptr) {
                return runNow(question.arguments, frame, original);
            }
            const uint32_t lifetime = current->lifetime;
            // the arguments other than the name; the name's hash in place of it
            const uint32_t first = question.name == 1 ? 0 : frame[2];
            uint32_t second = question.arguments < 2 ? 0 : frame[3];
            if (name != nullptr) {
                second = 2166136261u;
                for (size_t i = 0; i < length; i++) {
                    second = (second ^ static_cast<uint8_t>(name[i])) * 16777619u;
                }
            }
            const size_t start = ((effect >> 4) * 2654435761u ^ method * 40503u ^ first * 69069u ^ second) & (ANSWERED - 1);
            for (size_t probe = 0; probe < ANSWERED_PROBES; probe++) {
                const Answered &entry = answered[(start + probe) & (ANSWERED - 1)];
                if (entry.effect == effect && entry.lifetime == lifetime && entry.method == method && entry.first == first
                    && entry.second == second && (name == nullptr || memcmp(entry.name, name, length + 1) == 0)) {
                    skipResult = entry.answer;
                    if (method == GET_ANNOTATION || method == GET_ANNOTATION_BY_NAME) {
                        markAnnotation(effect, lifetime, skipResult);
                    }
                    return skippers[question.arguments];
                }
            }
            const uint32_t target = runNow(question.arguments, frame, original);
            if (method == GET_ANNOTATION || method == GET_ANNOTATION_BY_NAME) {
                markAnnotation(effect, lifetime, skipResult);
            }
            size_t at = start + answeredInserts++ % ANSWERED_PROBES;
            for (size_t probe = 0; probe < ANSWERED_PROBES; probe++) {
                if (answered[(start + probe) & (ANSWERED - 1)].lifetime == 0) {
                    at = start + probe;
                    break;
                }
            }
            Answered &entry = answered[at & (ANSWERED - 1)];
            entry.effect = effect;
            entry.lifetime = lifetime;
            entry.method = method;
            entry.first = first;
            entry.second = second;
            entry.answer = skipResult;
            memset(entry.name, 0, MAX_NAME);
            if (name != nullptr) {
                memcpy(entry.name, name, length);
            }
            return target;
        }

        // ---- vertex and index buffer locks, staged (bfme2-accelerator's phase 4): a buffer gets a mirror of its bytes when its old
        // bytes are undefined, so that the mirror may start out zeroed: at the first lock of a buffer the game thread made (marked
        // fresh by createBuffer), or at a lock that discards a dynamic one. From then on every lock of the game thread hands out the
        // mirror at once (a buffer that is not dynamic keeps it within MAX_KEPT_MIRRORS), and the last Unlock records the bytes of the
        // range the locks spanned; the worker
        // locks the buffer with the first lock's flags, copies them in and unlocks it, in its place in the queue. A write the mirror
        // would not see (another thread's lock, a lock that cannot be staged, ProcessVertices into the buffer) drops the mirror
        // first, and the buffer's locks wait again until it is discarded once more. The mirror hangs on the buffer as private data:
        // its address under one key, a COM object under another that D3D9 releases when it destroys the buffer. A record does not
        // hold the buffer: a Release keeps it alive behind everything recorded before (release).

        constexpr uint32_t BUFFER_SET_PRIVATE_DATA = 4;
        constexpr uint32_t BUFFER_GET_PRIVATE_DATA = 5;
        constexpr uint32_t BUFFER_FREE_PRIVATE_DATA = 6;
        constexpr uint32_t BUFFER_LOCK = 11;
        constexpr uint32_t BUFFER_UNLOCK = 12;
        constexpr uint32_t BUFFER_GET_DESC = 13;
        constexpr uint32_t VERTEX_BUFFER = d3d::VERTEX_BUFFER_LOCK - BUFFER_LOCK;
        constexpr uint32_t INDEX_BUFFER = d3d::INDEX_BUFFER_LOCK - BUFFER_LOCK;
        constexpr uint32_t LOCK_READONLY = 0x10;
        constexpr uint32_t LOCK_DISCARD = 0x2000;
        constexpr uint32_t USAGE_DYNAMIC = 0x200;
        constexpr uint32_t PRIVATE_DATA_UNKNOWN = 1;
        constexpr uint32_t MAX_MIRROR = 64 * 1024 * 1024;
        // the mirrors of buffers that are not dynamic, kept for their later locks as long as all of them fit
        constexpr long MAX_KEPT_MIRRORS = 64 * 1024 * 1024;
        constexpr size_t MAX_OPEN = 16;
        constexpr GUID MIRROR_ADDRESS = {0x7d1c5e2a, 0x41b3, 0x4f6e, {0x8a, 0x27, 0x5c, 0x93, 0xe0, 0x14, 0xb6, 0x2d}};
        constexpr GUID MIRROR_LIFE = {0x7d1c5e2b, 0x41b3, 0x4f6e, {0x8a, 0x27, 0x5c, 0x93, 0xe0, 0x14, 0xb6, 0x2d}};
        // a buffer the game thread made and nothing has locked yet: its bytes are undefined
        constexpr GUID BUFFER_FRESH = {0x7d1c5e2d, 0x41b3, 0x4f6e, {0x8a, 0x27, 0x5c, 0x93, 0xe0, 0x14, 0xb6, 0x2d}};

        struct Mirror {
            const void *const *vtable;
            volatile long references;
            uint8_t *bytes;
            uint32_t size;
            // game thread: the open locks, the range they span, the first one's flags, whether one of them may write
            uint32_t locks;
            uint32_t from;
            uint32_t to;
            uint32_t flags;
            bool written;
            // the mirror stays after its locks (a dynamic buffer's, or another's within MAX_KEPT_MIRRORS; it holds every write
            // since the buffer was made), or goes (made for the first lock of a fresh buffer past that)
            bool kept;
            // counted in keptMirrorBytes
            bool counted;
        };

        // the buffer and surface mirrors alive (the test reads it)
        volatile long liveMirrors = 0;

        void countMirrors(const long by) {
            if (counting) {
                InterlockedExchangeAdd(&liveMirrors, by);
            }
        }

        volatile long keptMirrorBytes = 0;

        HRESULT __stdcall mirrorQueryInterface(Mirror *, const GUID *, void **out) {
            *out = nullptr;
            return E_NOINTERFACE;
        }

        ULONG __stdcall mirrorAddRef(Mirror *mirror) {
            return static_cast<ULONG>(InterlockedIncrement(&mirror->references));
        }

        ULONG __stdcall mirrorRelease(Mirror *mirror) {
            const long left = InterlockedDecrement(&mirror->references);
            if (left == 0) {
                if (mirror->counted) {
                    InterlockedExchangeAdd(&keptMirrorBytes, -static_cast<long>(mirror->size));
                }
                VirtualFree(mirror->bytes, 0, MEM_RELEASE);
                HeapFree(GetProcessHeap(), 0, mirror);
                countMirrors(-1);
            }
            return static_cast<ULONG>(left);
        }

        const void *const MIRROR_VTABLE[3] = {
            reinterpret_cast<const void *>(&mirrorQueryInterface), reinterpret_cast<const void *>(&mirrorAddRef),
            reinterpret_cast<const void *>(&mirrorRelease),
        };

        struct Open {
            uint32_t buffer;
            Mirror *mirror;
        };

        Open open[MAX_OPEN];
        size_t openCount = 0;

        Mirror *mirrorOf(const uint32_t buffer, const uint32_t base) {
            Mirror *mirror = nullptr;
            uint32_t bytes = sizeof(mirror);
            const HRESULT found = unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, void *, uint32_t *)>(buffer, BUFFER_GET_PRIVATE_DATA)(
                buffer, &MIRROR_ADDRESS, &mirror, &bytes);
            return SUCCEEDED(found) && bytes == sizeof(mirror) ? mirror : nullptr;
        }

        void dropMirror(const uint32_t buffer, const uint32_t base) {
            const auto free = unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *)>(buffer, BUFFER_FREE_PRIVATE_DATA);
            free(buffer, &MIRROR_ADDRESS);
            free(buffer, &MIRROR_LIFE);
            free(buffer, &BUFFER_FRESH);
        }

        // the fresh mark, taken off
        bool takeFresh(const uint32_t buffer, const uint32_t base) {
            uint8_t fresh = 0;
            uint32_t bytes = sizeof(fresh);
            if (FAILED(unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, void *, uint32_t *)>(buffer, BUFFER_GET_PRIVATE_DATA)(
                    buffer, &BUFFER_FRESH, &fresh, &bytes))) {
                return false;
            }
            unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *)>(buffer, BUFFER_FREE_PRIVATE_DATA)(buffer, &BUFFER_FRESH);
            return true;
        }

        Mirror *makeMirror(const uint32_t buffer, const uint32_t base, const bool fresh) {
            // D3DVERTEXBUFFER_DESC / D3DINDEXBUFFER_DESC: Usage at 8, Size at 16
            uint32_t desc[6] = {};
            if (FAILED(unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t *)>(buffer, BUFFER_GET_DESC)(buffer, desc))
                || (!fresh && (desc[2] & USAGE_DYNAMIC) == 0) || desc[4] == 0 || desc[4] > MAX_MIRROR) {
                return nullptr;
            }
            auto *bytes = static_cast<uint8_t *>(VirtualAlloc(nullptr, desc[4], MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            auto *mirror = static_cast<Mirror *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Mirror)));
            if (bytes == nullptr || mirror == nullptr) {
                VirtualFree(bytes, 0, MEM_RELEASE);
                HeapFree(GetProcessHeap(), 0, mirror);
                return nullptr;
            }
            mirror->vtable = MIRROR_VTABLE;
            mirror->references = 1;
            mirror->bytes = bytes;
            mirror->size = desc[4];
            mirror->counted = (desc[2] & USAGE_DYNAMIC) == 0 && keptMirrorBytes + static_cast<long>(desc[4]) <= MAX_KEPT_MIRRORS;
            mirror->kept = (desc[2] & USAGE_DYNAMIC) != 0 || mirror->counted;
            if (mirror->counted) {
                InterlockedExchangeAdd(&keptMirrorBytes, static_cast<long>(desc[4]));
            }
            countMirrors(1);
            const auto set = unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, const void *, uint32_t, uint32_t)>(buffer,
                                                                                                                       BUFFER_SET_PRIVATE_DATA);
            const HRESULT tied = set(buffer, &MIRROR_LIFE, mirror, sizeof(mirror), PRIVATE_DATA_UNKNOWN);
            mirrorRelease(mirror);
            if (FAILED(tied)) {
                return nullptr;
            }
            if (FAILED(set(buffer, &MIRROR_ADDRESS, &mirror, sizeof(mirror), 0))) {
                dropMirror(buffer, base);
                return nullptr;
            }
            return mirror;
        }

        Open *openOf(const uint32_t buffer) {
            for (size_t i = 0; i < openCount; i++) {
                if (open[i].buffer == buffer) {
                    return &open[i];
                }
            }
            return nullptr;
        }

        // the worker's half of a staged lock (or the game thread's, after a wait, for more than a record carries)
        uint32_t __stdcall writeBuffer(const uint32_t buffer, const uint32_t offset, const uint32_t size, const uint32_t flags,
                                       const uint32_t data) {
            void *at = nullptr;
            const HRESULT locked = unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t, uint32_t, void **, uint32_t)>(buffer, BUFFER_LOCK)(
                buffer, offset, size, &at, flags);
            if (SUCCEEDED(locked)) {
                if (at != nullptr) {
                    memcpy(at, reinterpret_cast<const void *>(data), size);
                }
                unhooked<HRESULT (__stdcall *)(uint32_t)>(buffer, BUFFER_UNLOCK)(buffer);
            }
            return static_cast<uint32_t>(locked);
        }

        // Lock(buffer, offset, size, &data, flags)
        uint32_t lockBuffer(const uint32_t method, const uint32_t *frame) {
            const uint32_t buffer = frame[1];
            const uint32_t offset = frame[2];
            const uint32_t flags = frame[5];
            const uint32_t base = method - BUFFER_LOCK;
            if (frame[4] == 0) {
                return wait();
            }
            Open *already = openOf(buffer);
            Mirror *mirror = already != nullptr ? already->mirror : mirrorOf(buffer, base);
            if (mirror == nullptr) {
                const bool fresh = takeFresh(buffer, base);
                mirror = fresh || (flags & LOCK_DISCARD) != 0 ? makeMirror(buffer, base, fresh) : nullptr;
                if (mirror == nullptr) {
                    return wait();
                }
            }
            const uint64_t end = frame[3] == 0 ? mirror->size : static_cast<uint64_t>(offset) + frame[3];
            // D3D9 refuses the lock and writes nothing
            if (end > mirror->size || end <= offset) {
                return wait();
            }
            if (already == nullptr) {
                if (openCount == MAX_OPEN) {
                    dropMirror(buffer, base);
                    return wait();
                }
                open[openCount++] = {buffer, mirror};
            }
            if (mirror->locks == 0) {
                mirror->from = offset;
                mirror->to = static_cast<uint32_t>(end);
                mirror->flags = flags & ~LOCK_READONLY;
                mirror->written = false;
            } else {
                mirror->from = (std::min)(mirror->from, offset);
                mirror->to = (std::max)(mirror->to, static_cast<uint32_t>(end));
            }
            mirror->written |= (flags & LOCK_READONLY) == 0;
            mirror->locks++;
            *reinterpret_cast<uint8_t **>(frame[4]) = mirror->bytes + offset;
            tally(tallies.staged);
            skipResult = 0;
            return skippers[4];
        }

        // Unlock(buffer)
        uint32_t unlockBuffer(const uint32_t method, const uint32_t *frame) {
            const uint32_t buffer = frame[1];
            Open *entry = openOf(buffer);
            if (entry == nullptr) {
                return wait();
            }
            Mirror *mirror = entry->mirror;
            if (--mirror->locks == 0) {
                *entry = open[--openCount];
                if (mirror->written) {
                    const uint32_t bytes = mirror->to - mirror->from;
                    const uint32_t data = reinterpret_cast<uint32_t>(mirror->bytes + mirror->from);
                    render_queue::Record *record = bytes <= MAX_DATA ? render_queue::reserve(CALL, callBytes(5) + bytes + 16) : nullptr;
                    if (record == nullptr) {
                        wait();
                        writeBuffer(buffer, mirror->from, bytes, mirror->flags, data);
                    } else {
                        Call *call = callIn(record, reinterpret_cast<uint32_t>(&writeBuffer), method - BUFFER_UNLOCK + BUFFER_LOCK, 5);
                        const uint32_t copy = alignUp(reinterpret_cast<uint32_t>(call) + callBytes(5));
                        memcpy(reinterpret_cast<void *>(copy), reinterpret_cast<const void *>(data), bytes);
                        call->values[0] = buffer;
                        call->values[1] = mirror->from;
                        call->values[2] = bytes;
                        call->values[3] = mirror->flags;
                        call->values[4] = copy;
                        render_queue::commit();
                        tally(tallies.recorded);
                    }
                }
                if (!mirror->kept) {
                    dropMirror(buffer, method - BUFFER_UNLOCK);
                }
            }
            skipResult = 0;
            return skippers[0];
        }

        // CreateVertexBuffer / CreateIndexBuffer(device, length, usage, FVF or format, pool, &buffer, shared): the buffer is marked
        // fresh
        uint32_t createBuffer(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const uint32_t target = runNow(6, frame, original);
            const auto *out = reinterpret_cast<const uint32_t *>(frame[6]);
            if (SUCCEEDED(static_cast<HRESULT>(skipResult)) && out != nullptr && *out != 0) {
                const uint8_t fresh = 1;
                unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, const void *, uint32_t, uint32_t)>(
                    *out, BUFFER_SET_PRIVATE_DATA)(*out, &BUFFER_FRESH, &fresh,
                                                                                                                    sizeof(fresh), 0);
            }
            return target;
        }

        // ---- surface locks, staged (bfme2-accelerator's phase 5: the radar draws each dot through a 1x1 lock, SurfaceClass::DrawPixel
        // 0x5165E0, and its clear and the shroud lock whole surfaces): a surface the CPU owns (not the default pool, no render target,
        // whole bytes per pixel) gets a mirror at its third lock of the game thread, read from the surface after a wait. From then on
        // its locks of the game thread hand out the mirror, and the last Unlock records the rows of the rectangle they spanned; the
        // worker copies them in under a lock of that rectangle with the first lock's flags. The mirrors are found by surface in a
        // table; each hangs on its surface as a COM object that D3D9 releases when it destroys the surface, which takes it out of the
        // table before the address can be handed out again. A write the mirror would not see drops it first (beforeUnseenWrite).

        constexpr uint32_t SURFACE = d3d::SURFACE_LOCK - 13;
        constexpr uint32_t TEXTURE = d3d::TEXTURE_LOCK - 19;
        constexpr uint32_t CUBE_TEXTURE = d3d::CUBE_LOCK - 19;
        constexpr uint32_t RESOURCE_RELEASE = 2;
        constexpr uint32_t SURFACE_GET_DESC = 12;
        constexpr uint32_t SURFACE_LOCK_RECT = 13;
        constexpr uint32_t SURFACE_UNLOCK_RECT = 14;
        // GetSurfaceLevel, GetCubeMapSurface
        constexpr uint32_t LEVEL_SURFACE = 18;
        constexpr uint32_t POOL_DEFAULT = 0;
        constexpr uint32_t USAGE_RENDER_TARGET_OR_DEPTH = 0x3;
        constexpr uint32_t LOCKS_BEFORE_MIRROR = 2;
        constexpr uint32_t REFUSED = 0xFFFFFFFF;
        constexpr uint32_t MAX_SURFACE_MIRROR = 4 * 1024 * 1024;
        constexpr long MAX_SURFACE_MIRRORS = 64 * 1024 * 1024;
        constexpr uint32_t SURFACE_SLOTS = 1024;
        constexpr uint32_t SURFACE_PROBES = 16;
        constexpr GUID SURFACE_MIRROR_LIFE = {0x7d1c5e2c, 0x41b3, 0x4f6e, {0x8a, 0x27, 0x5c, 0x93, 0xe0, 0x14, 0xb6, 0x2d}};
        // a level of a texture the game thread made, which nothing has locked yet: its bytes are undefined
        constexpr GUID SURFACE_FRESH = {0x7d1c5e2e, 0x41b3, 0x4f6e, {0x8a, 0x27, 0x5c, 0x93, 0xe0, 0x14, 0xb6, 0x2d}};

        struct SurfaceMirror {
            const void *const *vtable;
            volatile long references;
            uint8_t *bytes;
            uint32_t size;
            uint32_t slot;
            uint32_t pitch;
            uint32_t width;
            uint32_t height;
            uint32_t pixelBytes;
            // a dynamic texture's surface in the default pool, which the device may copy into (counted in gpuWritableMirrors)
            bool gpuWritable;
            // the mirror stays after its locks; one made for the first lock of a fresh surface goes, unless the texture is dynamic
            bool kept;
            // game thread: the open locks, the rectangle they span, the first one's flags, whether one of them may write
            uint32_t locks;
            RECT spanned;
            uint32_t flags;
            bool written;
        };

        struct Mirrored {
            volatile uint32_t surface;
            SurfaceMirror *volatile mirror;
        };

        // the locks of surfaces without a mirror so far (a count left by a surface gone only brings the next one's mirror sooner)
        struct Counted {
            uint32_t surface;
            uint32_t locks;
        };

        Mirrored mirrored[SURFACE_SLOTS];
        Counted counted[SURFACE_SLOTS];
        volatile long surfaceMirrorBytes = 0;
        volatile long gpuWritableMirrors = 0;

        uint32_t surfaceStart(const uint32_t surface) {
            return ((surface >> 4) * 2654435761u) >> 22;
        }

        HRESULT __stdcall surfaceMirrorQueryInterface(SurfaceMirror *, const GUID *, void **out) {
            *out = nullptr;
            return E_NOINTERFACE;
        }

        ULONG __stdcall surfaceMirrorAddRef(SurfaceMirror *mirror) {
            return static_cast<ULONG>(InterlockedIncrement(&mirror->references));
        }

        ULONG __stdcall surfaceMirrorRelease(SurfaceMirror *mirror) {
            const long left = InterlockedDecrement(&mirror->references);
            if (left == 0) {
                // on the worker too (a recorded Release ends the surface), while the game thread may take a free slot: the slot is only
                // free once its surface is cleared, after its mirror
                Mirrored &entry = mirrored[mirror->slot];
                if (InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&entry.mirror), nullptr, mirror) == mirror) {
                    entry.surface = 0;
                }
                InterlockedExchangeAdd(&surfaceMirrorBytes, -static_cast<long>(mirror->size));
                if (mirror->gpuWritable) {
                    InterlockedDecrement(&gpuWritableMirrors);
                }
                VirtualFree(mirror->bytes, 0, MEM_RELEASE);
                HeapFree(GetProcessHeap(), 0, mirror);
                countMirrors(-1);
            }
            return static_cast<ULONG>(left);
        }

        const void *const SURFACE_MIRROR_VTABLE[3] = {
            reinterpret_cast<const void *>(&surfaceMirrorQueryInterface), reinterpret_cast<const void *>(&surfaceMirrorAddRef),
            reinterpret_cast<const void *>(&surfaceMirrorRelease),
        };

        uint32_t pixelBytesOf(const uint32_t format) {
            switch (format) {
                // A8R8G8B8, X8R8G8B8, A2B10G10R10, A8B8G8R8, X8B8G8R8, G16R16
                case 21:
                case 22:
                case 31:
                case 32:
                case 33:
                case 34:
                    return 4;
                // R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, A8R3G3B2, X4R4G4B4, A8L8, L16
                case 23:
                case 24:
                case 25:
                case 26:
                case 29:
                case 30:
                case 51:
                case 81:
                    return 2;
                // R3G3B2, A8, L8, A4L4
                case 27:
                case 28:
                case 50:
                case 52:
                    return 1;
                default:
                    return 0;
            }
        }

        SurfaceMirror *surfaceMirrorOf(const uint32_t surface) {
            const uint32_t start = surfaceStart(surface);
            for (uint32_t i = 0; i < SURFACE_PROBES; i++) {
                const Mirrored &entry = mirrored[(start + i) & (SURFACE_SLOTS - 1)];
                if (entry.surface == surface) {
                    return entry.mirror;
                }
            }
            return nullptr;
        }

        // D3DLOCKED_RECT
        struct LockedRect {
            int32_t pitch;
            uint8_t *bits;
        };

        using LockRect = HRESULT (__stdcall *)(uint32_t, LockedRect *, const RECT *, uint32_t);
        using UnlockRect = HRESULT (__stdcall *)(uint32_t);

        // discarding: for a lock that discards a dynamic texture's surface (its old bytes are undefined then: the mirror starts out
        // zeroed, without a wait); otherwise the mirror is read from the surface after a wait
        // fresh: for the first lock of a fresh surface (its bytes are undefined: the mirror starts out zeroed, without a wait, and goes
        // after the lock). unsuited: refused for what the surface is (format, pool, usage), not for want of room, which a later lock
        // may have
        SurfaceMirror *makeSurfaceMirror(const uint32_t surface, const bool discarding, const bool fresh, bool &unsuited) {
            // D3DSURFACE_DESC: Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality, Width, Height
            uint32_t desc[8] = {};
            unsuited = false;
            if (FAILED(unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t *)>(surface, SURFACE_GET_DESC)(surface, desc))
                || (desc[3] == POOL_DEFAULT && (desc[2] & USAGE_DYNAMIC) == 0) || (desc[2] & USAGE_RENDER_TARGET_OR_DEPTH) != 0
                || pixelBytesOf(desc[0]) == 0 || desc[6] == 0 || desc[7] == 0) {
                unsuited = true;
                if (counting) {
                    tallies.unsuitedSurfaces++;
                    const uint32_t refused[5] = {desc[0], desc[2], desc[3], desc[6], desc[7]};
                    memcpy(tallies.lastUnsuitedSurface, refused, sizeof(refused));
                }
                return nullptr;
            }
            const uint32_t start = surfaceStart(surface);
            uint32_t slot = SURFACE_SLOTS;
            for (uint32_t i = 0; i < SURFACE_PROBES && slot == SURFACE_SLOTS; i++) {
                if (mirrored[(start + i) & (SURFACE_SLOTS - 1)].surface == 0) {
                    slot = (start + i) & (SURFACE_SLOTS - 1);
                }
            }
            if (slot == SURFACE_SLOTS) {
                return nullptr;
            }
            const uint32_t pixelBytes = pixelBytesOf(desc[0]);
            const bool undefined = fresh || (discarding && (desc[2] & USAGE_DYNAMIC) != 0);
            LockedRect locked = {static_cast<int32_t>(desc[6] * pixelBytes), nullptr};
            if (!undefined) {
                wait();
                if (FAILED(unhooked<LockRect>(surface, SURFACE_LOCK_RECT)(surface, &locked, nullptr, LOCK_READONLY))) {
                    return nullptr;
                }
            }
            const uint64_t size = static_cast<uint64_t>(locked.pitch) * desc[7];
            uint8_t *bytes = nullptr;
            if (locked.pitch >= static_cast<int32_t>(desc[6] * pixelBytes) && size <= MAX_SURFACE_MIRROR
                && surfaceMirrorBytes + static_cast<long>(size) <= MAX_SURFACE_MIRRORS) {
                bytes = static_cast<uint8_t *>(VirtualAlloc(nullptr, static_cast<size_t>(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                if (bytes != nullptr && !undefined) {
                    memcpy(bytes, locked.bits, static_cast<size_t>(size));
                }
            }
            if (!undefined) {
                unhooked<UnlockRect>(surface, SURFACE_UNLOCK_RECT)(surface);
            }
            auto *mirror = bytes != nullptr ? static_cast<SurfaceMirror *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SurfaceMirror))) : nullptr;
            if (mirror == nullptr) {
                VirtualFree(bytes, 0, MEM_RELEASE);
                return nullptr;
            }
            mirror->vtable = SURFACE_MIRROR_VTABLE;
            mirror->references = 1;
            mirror->bytes = bytes;
            mirror->size = static_cast<uint32_t>(size);
            mirror->slot = slot;
            mirror->pitch = static_cast<uint32_t>(locked.pitch);
            mirror->width = desc[6];
            mirror->height = desc[7];
            mirror->pixelBytes = pixelBytes;
            mirror->gpuWritable = desc[3] == POOL_DEFAULT;
            mirror->kept = !fresh || (desc[2] & USAGE_DYNAMIC) != 0;
            if (mirror->gpuWritable) {
                InterlockedIncrement(&gpuWritableMirrors);
            }
            countMirrors(1);
            InterlockedExchangeAdd(&surfaceMirrorBytes, static_cast<long>(size));
            // in the table before D3D9 holds it: a failed tie releases it out of the table again
            mirrored[slot].mirror = mirror;
            mirrored[slot].surface = surface;
            const HRESULT tied = unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, const void *, uint32_t, uint32_t)>(
                surface, BUFFER_SET_PRIVATE_DATA)(surface, &SURFACE_MIRROR_LIFE, mirror, sizeof(mirror), PRIVATE_DATA_UNKNOWN);
            surfaceMirrorRelease(mirror);
            return SUCCEEDED(tied) ? mirror : nullptr;
        }

        void dropSurfaceMirror(const uint32_t surface) {
            if (surface != 0) {
                const auto free = unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *)>(surface, BUFFER_FREE_PRIVATE_DATA);
                free(surface, &SURFACE_MIRROR_LIFE);
                free(surface, &SURFACE_FRESH);
            }
        }

        // the fresh mark, taken off
        bool takeFreshSurface(const uint32_t surface) {
            uint8_t fresh = 0;
            uint32_t bytes = sizeof(fresh);
            if (FAILED(unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, void *, uint32_t *)>(surface, BUFFER_GET_PRIVATE_DATA)(
                    surface, &SURFACE_FRESH, &fresh, &bytes))) {
                return false;
            }
            unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *)>(surface, BUFFER_FREE_PRIVATE_DATA)(surface, &SURFACE_FRESH);
            return true;
        }

        constexpr uint32_t D3DX_CREATE_TEXTURE = d3d::METHODS + 2;

        // a surface just made: its first lock needs no wait for its bytes; what was counted for a surface at its address before is
        // forgotten
        void markFresh(const uint32_t surface) {
            const uint8_t fresh = 1;
            unhooked<HRESULT (__stdcall *)(uint32_t, const GUID *, const void *, uint32_t, uint32_t)>(surface, BUFFER_SET_PRIVATE_DATA)(
                surface, &SURFACE_FRESH, &fresh, sizeof(fresh), 0);
            Counted &count = counted[surfaceStart(surface)];
            if (count.surface == surface) {
                count = {0, 0};
            }
        }

        // CreateTexture(device, width, height, levels, usage, format, pool, &texture, shared), D3DXCreateTexture(device, width, height,
        // levels, usage, format, pool, &texture): each level of the texture is marked fresh
        uint32_t createTexture(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const bool d3dx = method == D3DX_CREATE_TEXTURE;
            const uint32_t target = runNow(d3dx ? 7 : 8, frame, original);
            const auto *out = reinterpret_cast<const uint32_t *>(frame[8]);
            if (FAILED(static_cast<HRESULT>(skipResult)) || out == nullptr || *out == 0) {
                return target;
            }
            const uint32_t texture = *out;
            const uint32_t levels = unhooked<uint32_t (__stdcall *)(uint32_t)>(texture, 13)(texture);
            for (uint32_t level = 0; level < levels; level++) {
                uint32_t surface = 0;
                if (SUCCEEDED(unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t, uint32_t *)>(texture, LEVEL_SURFACE)(texture, level, &surface))
                    && surface != 0) {
                    markFresh(surface);
                    unhooked<ULONG (__stdcall *)(uint32_t)>(surface, RESOURCE_RELEASE)(surface);
                }
            }
            return target;
        }

        // CreateOffscreenPlainSurface(device, width, height, format, pool, &surface, shared), kind L: outside the default pool it runs
        // now under the exec lock and the surface is marked fresh (the game's text renderer makes one in system memory per sentence and
        // locks it at once); in the default pool it waits (createsVideoMemory)
        uint32_t createSurface(const uint32_t *frame, const uint32_t original) {
            if (frame[5] == POOL_DEFAULT) {
                return runNow(6, frame, original);
            }
            render_queue::lock();
            skipResult = invokers[1 + 6](original, frame + 1);
            render_queue::unlock();
            tally(tallies.locked);
            const auto *out = reinterpret_cast<const uint32_t *>(frame[6]);
            if (SUCCEEDED(static_cast<HRESULT>(skipResult)) && out != nullptr && *out != 0) {
                markFresh(*out);
            }
            return skippers[6];
        }

        // the kind L calls that make video memory resources: CreateAdditionalSwapChain, CreateVolumeTexture, CreateCubeTexture,
        // CreateRenderTarget, CreateDepthStencilSurface. They wait, as the other creations do: run before the Releases recorded ahead
        // of them, they would fail where the game's call succeeds once video memory runs out.
        bool createsVideoMemory(const uint32_t method) {
            return method == 14 || method == 24 || method == 25 || method == 28 || method == 29;
        }

        // a texture's level (face 0xFFFFFFFF) or a cube texture's face and level: GetSurfaceLevel(level, &surface),
        // GetCubeMapSurface(face, level, &surface)
        void dropLevelMirror(const uint32_t texture, const uint32_t face, const uint32_t level) {
            uint32_t surface = 0;
            const HRESULT got = face == 0xFFFFFFFF
                ? unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t, uint32_t *)>(texture, LEVEL_SURFACE)(texture, level, &surface)
                : unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t *)>(texture, LEVEL_SURFACE)(texture, face, level,
                                                                                                                                 &surface);
            if (SUCCEEDED(got) && surface != 0) {
                dropSurfaceMirror(surface);
                unhooked<ULONG (__stdcall *)(uint32_t)>(surface, RESOURCE_RELEASE)(surface);
            }
        }

        // every level (and face) of a texture the device copies into (UpdateTexture)
        void dropTextureMirrors(const uint32_t texture) {
            // GetType: D3DRTYPE_TEXTURE 3, D3DRTYPE_CUBETEXTURE 5
            const uint32_t type = unhooked<uint32_t (__stdcall *)(uint32_t)>(texture, 10)(texture);
            if (type != 3 && type != 5) {
                return;
            }
            const uint32_t levels = unhooked<uint32_t (__stdcall *)(uint32_t)>(texture, 13)(texture);
            for (uint32_t face = 0; face < (type == 3 ? 1u : 6u); face++) {
                for (uint32_t level = 0; level < levels; level++) {
                    dropLevelMirror(texture, type == 3 ? 0xFFFFFFFF : face, level);
                }
            }
        }

        // the worker's half of a staged surface lock (or the game thread's, after a wait, for more than a record carries)
        uint32_t __stdcall writeSurface(const uint32_t surface, const uint32_t left, const uint32_t top, const uint32_t right,
                                        const uint32_t bottom, const uint32_t flags, const uint32_t data, const uint32_t sourcePitch,
                                        const uint32_t rowBytes) {
            const RECT rect = {static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom)};
            LockedRect locked = {};
            const HRESULT result = d3d::slotOf<LockRect>(surface, SURFACE_LOCK_RECT)(surface, &locked, &rect, flags);
            if (SUCCEEDED(result)) {
                for (uint32_t row = 0; row < bottom - top; row++) {
                    memcpy(locked.bits + static_cast<size_t>(row) * locked.pitch, reinterpret_cast<const uint8_t *>(data) + row * sourcePitch, rowBytes);
                }
                d3d::slotOf<UnlockRect>(surface, SURFACE_UNLOCK_RECT)(surface);
            }
            return static_cast<uint32_t>(result);
        }

        // LockRect(surface, &locked, rect, flags)
        uint32_t lockSurface(const uint32_t *frame) {
            const uint32_t surface = frame[1];
            auto *out = reinterpret_cast<LockedRect *>(frame[2]);
            const auto *rect = reinterpret_cast<const RECT *>(frame[3]);
            const uint32_t flags = frame[4];
            if (out == nullptr) {
                return wait();
            }
            SurfaceMirror *mirror = surfaceMirrorOf(surface);
            if (mirror == nullptr) {
                bool unsuited;
                if (takeFreshSurface(surface)) {
                    mirror = makeSurfaceMirror(surface, false, true, unsuited);
                }
            }
            if (mirror == nullptr) {
                Counted &count = counted[surfaceStart(surface)];
                if (count.surface != surface) {
                    count = {surface, 0};
                }
                const bool discarding = (flags & LOCK_DISCARD) != 0;
                if (count.locks == REFUSED || (++count.locks <= LOCKS_BEFORE_MIRROR && !discarding)) {
                    if (counting) {
                        uint32_t desc[8] = {};
                        unhooked<HRESULT (__stdcall *)(uint32_t, uint32_t *)>(surface, SURFACE_GET_DESC)(surface, desc);
                        const uint32_t waited[6] = {desc[0], desc[2], desc[3], desc[6], desc[7], count.locks};
                        memcpy(tallies.lastCountedSurface, waited, sizeof(waited));
                    }
                    return wait();
                }
                bool unsuited;
                mirror = makeSurfaceMirror(surface, discarding, false, unsuited);
                if (mirror == nullptr) {
                    count.locks = unsuited ? REFUSED : 0;
                    if (!unsuited) {
                        tally(tallies.surfaceMirrorsWithoutRoom);
                    }
                    return wait();
                }
            }
            const RECT spanned = rect != nullptr ? *rect : RECT{0, 0, static_cast<LONG>(mirror->width), static_cast<LONG>(mirror->height)};
            // D3D9 refuses the lock and writes nothing
            if (spanned.left < 0 || spanned.top < 0 || spanned.left >= spanned.right || spanned.top >= spanned.bottom
                || spanned.right > static_cast<LONG>(mirror->width) || spanned.bottom > static_cast<LONG>(mirror->height)) {
                return wait();
            }
            if (mirror->locks == 0) {
                mirror->spanned = spanned;
                mirror->flags = flags & ~LOCK_READONLY;
                mirror->written = false;
            } else {
                mirror->spanned.left = (std::min)(mirror->spanned.left, spanned.left);
                mirror->spanned.top = (std::min)(mirror->spanned.top, spanned.top);
                mirror->spanned.right = (std::max)(mirror->spanned.right, spanned.right);
                mirror->spanned.bottom = (std::max)(mirror->spanned.bottom, spanned.bottom);
            }
            mirror->written |= (flags & LOCK_READONLY) == 0;
            mirror->locks++;
            out->pitch = static_cast<int32_t>(mirror->pitch);
            out->bits = mirror->bytes + static_cast<size_t>(spanned.top) * mirror->pitch + static_cast<size_t>(spanned.left) * mirror->pixelBytes;
            tally(tallies.staged);
            skipResult = 0;
            return skippers[3];
        }

        // UnlockRect(surface)
        uint32_t unlockSurface(const uint32_t *frame) {
            const uint32_t surface = frame[1];
            SurfaceMirror *mirror = surfaceMirrorOf(surface);
            if (mirror == nullptr || mirror->locks == 0) {
                return wait();
            }
            if (--mirror->locks != 0) {
                skipResult = 0;
                return skippers[0];
            }
            if (mirror->written) {
                const RECT &r = mirror->spanned;
                const uint32_t rowBytes = static_cast<uint32_t>(r.right - r.left) * mirror->pixelBytes;
                const uint32_t rows = static_cast<uint32_t>(r.bottom - r.top);
                const uint64_t bytes = static_cast<uint64_t>(rowBytes) * rows;
                const uint8_t *from = mirror->bytes + static_cast<size_t>(r.top) * mirror->pitch + static_cast<size_t>(r.left) * mirror->pixelBytes;
                render_queue::Record *record = bytes <= MAX_DATA ? render_queue::reserve(CALL, callBytes(9) + static_cast<uint32_t>(bytes) + 16) : nullptr;
                if (record == nullptr) {
                    wait();
                    writeSurface(surface, r.left, r.top, r.right, r.bottom, mirror->flags, reinterpret_cast<uint32_t>(from), mirror->pitch, rowBytes);
                } else {
                    Call *call = callIn(record, reinterpret_cast<uint32_t>(&writeSurface), d3d::SURFACE_LOCK, 9);
                    const uint32_t copy = alignUp(reinterpret_cast<uint32_t>(call) + callBytes(9));
                    for (uint32_t row = 0; row < rows; row++) {
                        memcpy(reinterpret_cast<uint8_t *>(copy) + row * rowBytes, from + static_cast<size_t>(row) * mirror->pitch, rowBytes);
                    }
                    const uint32_t values[9] = {surface, static_cast<uint32_t>(r.left), static_cast<uint32_t>(r.top), static_cast<uint32_t>(r.right),
                                                static_cast<uint32_t>(r.bottom), mirror->flags, copy, rowBytes, rowBytes};
                    memcpy(call->values, values, sizeof(values));
                    render_queue::commit();
                    tally(tallies.recorded);
                }
            }
            if (!mirror->kept) {
                dropSurfaceMirror(surface);
            }
            skipResult = 0;
            return skippers[0];
        }

        // ---- render targets and the depth stencil surface: SetRenderTarget and SetDepthStencilSurface are recorded and the game thread
        // keeps what it set, so that GetRenderTarget and GetDepthStencilSurface hand it back (with a reference, as D3D9 does) without
        // a wait. Neither state blocks nor effects set them. The first Get of each waits and keeps what D3D9 handed back, and so does
        // a Get after a Reset (the back buffer and the automatic depth stencil surface again), another thread's or D3D9's / D3DX's own
        // Set, or a Set that cannot be right (a target index past the last, no surface for target 0).

        constexpr uint32_t MAX_TARGETS = 4;
        constexpr uint32_t SURFACE_ADD_REF = 1;

        struct Bound {
            bool known;
            uint32_t surface;
        };

        Bound targets[MAX_TARGETS];
        Bound depthStencil;

        void forgetBound() {
            for (Bound &target : targets) {
                target.known = false;
            }
            depthStencil.known = false;
        }

        // SetRenderTarget(device, index, surface), SetDepthStencilSurface(device, surface)
        uint32_t bind(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            if (method == d3d::SET_RENDER_TARGET) {
                const uint32_t index = frame[2];
                if (index >= MAX_TARGETS || (index == 0 && frame[3] == 0)) {
                    forgetBound();
                    return wait();
                }
                targets[index] = {true, frame[3]};
            } else {
                depthStencil = {true, frame[2]};
            }
            return record(method, frame, original);
        }

        // GetRenderTarget(device, index, &surface), GetDepthStencilSurface(device, &surface)
        uint32_t bound(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const bool target = method == d3d::GET_RENDER_TARGET;
            const uint32_t arguments = target ? 2 : 1;
            auto *out = reinterpret_cast<uint32_t *>(frame[arguments + 1]);
            Bound *slot = !target ? &depthStencil : frame[2] < MAX_TARGETS ? &targets[frame[2]] : nullptr;
            if (slot != nullptr && slot->known && slot->surface != 0 && out != nullptr) {
                unhooked<ULONG (__stdcall *)(uint32_t)>(slot->surface, SURFACE_ADD_REF)(slot->surface);
                *out = slot->surface;
                skipResult = 0;
                return skippers[arguments];
            }
            const uint32_t result = runNow(arguments, frame, original);
            if (slot != nullptr && out != nullptr && SUCCEEDED(static_cast<HRESULT>(skipResult))) {
                *slot = {true, *out};
            }
            return result;
        }

        // a write the mirrors would not see (another thread's lock, D3D9's or D3DX's own, a texture level's lock, GDI, the device
        // copying into a surface, ProcessVertices): the mirror goes first; and a target set or a Reset the game thread did not see.
        // A method handled here must be in handledApart.
        void beforeUnseenWrite(const uint32_t method, const uint32_t *frame) {
            switch (method) {
                case d3d::SET_RENDER_TARGET:
                case d3d::SET_DEPTH_STENCIL_SURFACE:
                case d3d::RESET:
                    forgetBound();
                    break;
                case d3d::VERTEX_BUFFER_LOCK:
                case d3d::INDEX_BUFFER_LOCK:
                    dropMirror(frame[1], method - BUFFER_LOCK);
                    break;
                case d3d::PROCESS_VERTICES:
                    if (frame[5] != 0) {
                        dropMirror(frame[5], VERTEX_BUFFER);
                    }
                    break;
                case d3d::SURFACE_LOCK:
                case d3d::SURFACE_GET_DC:
                    dropSurfaceMirror(frame[1]);
                    break;
                case d3d::GET_RENDER_TARGET_DATA:
                case d3d::GET_FRONT_BUFFER_DATA:
                    dropSurfaceMirror(frame[3]);
                    break;
                // the device copying into a default pool surface: UpdateSurface(source, rect, destination, point),
                // StretchRect(source, rect, destination, rect, filter), ColorFill(surface, rect, color), UpdateTexture(source,
                // destination)
                case d3d::UPDATE_SURFACE:
                case d3d::STRETCH_RECT:
                    if (gpuWritableMirrors != 0) {
                        dropSurfaceMirror(frame[4]);
                    }
                    break;
                case d3d::COLOR_FILL:
                    if (gpuWritableMirrors != 0) {
                        dropSurfaceMirror(frame[2]);
                    }
                    break;
                case d3d::UPDATE_TEXTURE:
                    if (gpuWritableMirrors != 0 && frame[3] != 0) {
                        dropTextureMirrors(frame[3]);
                    }
                    break;
                // LockRect(level, ...), LockRect(face, level, ...)
                case d3d::TEXTURE_LOCK:
                    dropLevelMirror(frame[1], 0xFFFFFFFF, frame[2]);
                    break;
                case d3d::CUBE_LOCK:
                    dropLevelMirror(frame[1], frame[2], frame[3]);
                    break;
                default:
                    break;
            }
        }

        // ---- Present: recorded, with at most one frame in flight: after recording frame N + 1's Present the game thread waits until
        // N's has run. The game gets the result of the last Present the worker ran, so a lost device shows one frame later (it draws
        // nothing either way).

        bool presentQueued = false;
        uint32_t previousPresent = 0;

        // Present(device, source, destination, window, dirty region) and the swap chain's Present(..., flags)
        uint32_t present(const uint32_t method, const uint32_t *frame, const uint32_t original) {
            const uint32_t arguments = method == d3d::PRESENT ? 4 : 5;
            if (frame[5] != 0) {
                return runNow(arguments, frame, original);
            }
            sendFloatState();
            render_queue::Record *record = render_queue::reserve(CALL, callBytes(1 + arguments) + 2 * 16 + 15);
            Call *call = callIn(record, original, method, 1 + arguments);
            memcpy(call->values, frame + 1, 4 * (1 + arguments));
            uint32_t at = alignUp(reinterpret_cast<uint32_t>(call) + callBytes(1 + arguments));
            __try {
                // the source and destination rectangles
                for (uint32_t value = 1; value <= 2; value++) {
                    if (frame[1 + value] != 0) {
                        memcpy(reinterpret_cast<void *>(at), reinterpret_cast<const void *>(frame[1 + value]), 16);
                        call->values[value] = at;
                        at += 16;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                record->tag = NOTHING;
                render_queue::commit();
                return runNow(arguments, frame, original);
            }
            render_queue::commit();
            tally(tallies.recorded);
            const uint32_t after = render_queue::position();
            if (presentQueued && render_queue::waitUntil(previousPresent)) {
                tally(tallies.presentWaits);
            }
            previousPresent = after;
            presentQueued = true;
            skipResult = static_cast<uint32_t>(presentResult);
            return skippers[arguments];
        }

        // Reset(device, parameters): waits and runs now; a device that is back forgets the lost Present
        uint32_t reset(const uint32_t *frame, const uint32_t original) {
            seenCount = 0;
            forgetBound();
            const uint32_t target = runNow(1, frame, original);
            if (SUCCEEDED(static_cast<HRESULT>(skipResult))) {
                presentResult = 0;
            }
            return target;
        }

        bool recordedHandWritten(const uint32_t method) {
            switch (method) {
                case d3d::SET_VERTEX_DECLARATION:
                case d3d::SET_STREAM_SOURCE:
                case d3d::SET_INDICES:
                // UpdateSurface: the source is read on the worker in its place, after the writes recorded before; the game's later
                // writes to it are recorded behind it or wait
                case d3d::UPDATE_SURFACE:
                // SetBool, SetInt, SetFloat, SetVector, SetMatrix, SetMatrixTranspose, SetTexture, SetRawValue
                case FX + 22:
                case FX + 26:
                case FX + 30:
                case FX + 34:
                case FX + 38:
                case FX + 44:
                case FX + 52:
                case FX + 78:
                // BeginParameterBlock: the Set calls up to EndParameterBlock land in the block on the worker (see the parameter
                // blocks above)
                case FX + 73:
                    return true;
                default:
                    return false;
            }
        }

        // the methods onCall handles before its switch by kind, and those beforeUnseenWrite acts on: they never take the short way
        bool handledApart(const uint32_t method) {
            switch (method) {
                case d3d::CREATE_TEXTURE:
                case d3d::CREATE_OFFSCREEN_PLAIN_SURFACE:
                case d3d::CREATE_VERTEX_BUFFER:
                case d3d::CREATE_INDEX_BUFFER:
                case d3d::PRESENT:
                case d3d::SWAP_CHAIN_PRESENT:
                case d3d::RESET:
                case d3d::VERTEX_BUFFER_LOCK:
                case d3d::INDEX_BUFFER_LOCK:
                case d3d::VERTEX_BUFFER_UNLOCK:
                case d3d::INDEX_BUFFER_UNLOCK:
                case d3d::SURFACE_LOCK:
                case d3d::SURFACE_UNLOCK:
                case d3d::SURFACE_GET_DC:
                case d3d::SET_RENDER_TARGET:
                case d3d::SET_DEPTH_STENCIL_SURFACE:
                case d3d::GET_RENDER_TARGET:
                case d3d::GET_DEPTH_STENCIL_SURFACE:
                case d3d::PROCESS_VERTICES:
                case d3d::GET_RENDER_TARGET_DATA:
                case d3d::GET_FRONT_BUFFER_DATA:
                case d3d::UPDATE_SURFACE:
                case d3d::STRETCH_RECT:
                case d3d::COLOR_FILL:
                case d3d::UPDATE_TEXTURE:
                case d3d::TEXTURE_LOCK:
                case d3d::CUBE_LOCK:
                    return true;
                default:
                    return false;
            }
        }

        // by method: recorded with nothing else to do (the short way through onCall); filled by start
        bool recordedAtOnce[d3d::METHODS];

        uint32_t __cdecl onCall(const uint32_t method, uint32_t *frame, const uint32_t original) {
            const uint32_t thread = __readfsdword(0x24);
            if (thread == render_queue::workerThread()) {
                if (method == d3d::BEGIN_STATE_BLOCK && counting) {
                    tallies.workerRecordings++;
                    tallies.workerRecordingMethod = worker.runningMethod;
                }
                return 0;
            }
            if (thread != d3d::gameThread()) {
                if (counting) {
                    InterlockedIncrement(&tallies.otherThreads);
                }
                // GetNPatchMode returns a float, which a locked call would not hand on
                if (!active || method >= d3d::METHODS || method == d3d::GET_NPATCH_MODE || d3d::layoutOf(method).arguments > MAX_ARGUMENTS) {
                    return 0;
                }
                beforeUnseenWrite(method, frame);
                // the game thread records nothing meanwhile (it waits for the renderer's lock this thread holds): what it recorded runs
                // first, as it ran before this thread got the lock
                render_queue::waitIdle();
                // what the game thread keeps about an effect follows this thread's calls too
                if (method == SET_TECHNIQUE) {
                    setTechnique(frame[1], frame[2]);
                }
                if (method == FX_RELEASE) {
                    render_queue::lock();
                    skipResult = invokers[1](original, frame + 1);
                    render_queue::unlock();
                    if (skipResult == 0) {
                        forget(frame[1]);
                    }
                    return skippers[0];
                }
                swapStandIn(method, frame);
                lockedTarget = original;
                return lockers[d3d::layoutOf(method).arguments];
            }
            if (!active) {
                return 0;
            }
            // D3D9's or D3DX's own call (D3DX's ValidateTechnique runs Begin, BeginPass, EndPass and End through the effect's
            // vtable, D3DXCreateTexture calls the device): part of a call that runs on the game thread now, after a wait
            if (d3d::fromD3d(frame[0])) {
                beforeUnseenWrite(method, frame);
                tally(tallies.direct);
                return 0;
            }
            if (counting) {
                handling = method;
                handlingFrom = frame[0];
            }
            if (method < d3d::METHODS && recordedAtOnce[method] && (annotationsMarked == 0 || !effectSetter(method))) {
                return record(method, frame, original);
            }
            // a method handled here before the switch must be in handledApart
            if (method == D3DX_CREATE_TEXTURE || method == d3d::CREATE_TEXTURE) {
                return createTexture(method, frame, original);
            }
            if (method == d3d::CREATE_OFFSCREEN_PLAIN_SURFACE) {
                return createSurface(frame, original);
            }
            if (method >= d3d::METHODS) {
                return wait();
            }
            if (method == d3d::PRESENT || method == d3d::SWAP_CHAIN_PRESENT) {
                return present(method, frame, original);
            }
            if (method == d3d::RESET) {
                return reset(frame, original);
            }
            if (method == d3d::VERTEX_BUFFER_LOCK || method == d3d::INDEX_BUFFER_LOCK) {
                return lockBuffer(method, frame);
            }
            if (method == d3d::VERTEX_BUFFER_UNLOCK || method == d3d::INDEX_BUFFER_UNLOCK) {
                return unlockBuffer(method, frame);
            }
            if (method == d3d::SURFACE_LOCK) {
                return lockSurface(frame);
            }
            if (method == d3d::SURFACE_UNLOCK) {
                return unlockSurface(frame);
            }
            if (method == d3d::SET_RENDER_TARGET || method == d3d::SET_DEPTH_STENCIL_SURFACE) {
                return bind(method, frame, original);
            }
            if (method == d3d::GET_RENDER_TARGET || method == d3d::GET_DEPTH_STENCIL_SURFACE) {
                return bound(method, frame, original);
            }
            if (method == d3d::CREATE_VERTEX_BUFFER || method == d3d::CREATE_INDEX_BUFFER) {
                return createBuffer(method, frame, original);
            }
            if (annotationsMarked != 0 && annotationValue(method) && annotationOf(frame[1], frame[2]) != nullptr) {
                return describe(method, frame, original);
            }
            if (annotationsMarked != 0 && effectSetter(method)) {
                if (Annotation *annotation = annotationOf(frame[1], frame[2])) {
                    annotation->lifetime = 0;
                    forgetDescribed(frame[1], frame[2]);
                }
            }
            beforeUnseenWrite(method, frame);
            switch (d3d::kindOf(method)) {
                case 'D':
                    tally(tallies.direct);
                    return 0;
                case 'L':
                    if (createsVideoMemory(method)) {
                        return wait();
                    }
                    tally(tallies.locked);
                    lockedTarget = original;
                    return lockers[d3d::layoutOf(method).arguments];
                case 'A':
                    return record(method, frame, original);
                case 'R':
                    return release(method, frame, original);
                case 'X':
                    if (recordedHandWritten(method)) {
                        return record(method, frame, original);
                    }
                    if (method == END_PARAMETER_BLOCK) {
                        return endParameterBlock(frame, original);
                    }
                    if (method == APPLY_PARAMETER_BLOCK || method == DELETE_PARAMETER_BLOCK) {
                        return onParameterBlock(method, frame, original);
                    }
                    if (method == SET_TECHNIQUE) {
                        setTechnique(frame[1], frame[2]);
                        return record(method, frame, original);
                    }
                    if (method == BEGIN) {
                        return begin(frame, original);
                    }
                    if (method == GET_DESC || method == GET_PARAMETER_DESC || method == GET_TECHNIQUE_DESC || method == GET_PASS_DESC) {
                        return describe(method, frame, original);
                    }
                    if (const Question *question = questionOf(method)) {
                        return answer(*question, frame, original);
                    }
                    if (method == FX_RELEASE) {
                        // the effect's answers are kept until it is gone
                        const uint32_t target = runNow(0, frame, original);
                        if (skipResult == 0) {
                            forget(frame[1]);
                        }
                        return target;
                    }
                    return wait();
                default:
                    return wait();
            }
        }
    }

    bool start() {
        if (started) {
            return false;
        }
        fillTables(std::make_index_sequence<MAX_ARGUMENTS + 1>{});
        if (!d3d::start(&onCall, &tallies.hooks) || !render_queue::start(&run, RING_BYTES)) {
            return false;
        }
        for (uint32_t method = 0; method < d3d::METHODS; method++) {
            recordedAtOnce[method] = (d3d::kindOf(method) == 'A' || recordedHandWritten(method)) && !handledApart(method);
        }
        started = true;
        return true;
    }

    void hookDevice(const uint32_t device) {
        if (!started || device == 0) {
            return;
        }
        if (tallies.hooks.vtables > 0) {
            // another device: its calls are not hooked, so nothing recorded may run behind them (the effects' calls would)
            if (active && !d3d::deviceHooked(device)) {
                wait();
                active = false;
                tally(tallies.deviceChanges);
            }
            return;
        }
        d3d::hookDevice(device);
        active = d3d::deviceHooked(device);
    }

    void hookEffect(const uint32_t vtable) {
        if (active) {
            d3d::hookEffect(vtable);
        }
    }

    const Counts &counts() {
        tallies.ran = worker.ran;
        tallies.workerTicks = render_queue::busyTicks();
        tallies.waitTicks = render_queue::waitTicks();
        return tallies;
    }
}
