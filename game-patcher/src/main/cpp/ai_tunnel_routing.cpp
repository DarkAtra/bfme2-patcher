#include <windows.h>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

#include "ai_tunnel_routing.h"
#include "game_memory.h"
#include "patch_set.h"

// AI tunnel routing
//
// Problem: the pathfinder knows nothing of tunnel networks (the Dwarven and Wild mine shafts), so a skirmish AI army ordered
// across the map walks the whole way past the shafts its player owns, where a player would send it in at one shaft and take it
// out at another.
// Fix: a move or attack-move to a computer player's unit goes through that player's network when that is clearly shorter: the
// unit enters the shaft nearest to it, comes out at the shaft nearest its goal and then carries on with its order.
//
// The order: in front of aiDoCommand's command switch (0x0066AA09) a computer player's unit outside the network that gets a move
// or attack-move (from its AI or from its player: the skirmish AI orders its armies as the player) is routed when walking to A,
// the usable shaft nearest the unit, plus TRANSIT_COST plus walking from B, the usable shaft nearest the goal, is shorter than the
// straight line (all in the ground plane, so only a long trip is routed), and A's contain takes the unit (its filter, not its
// room). Usable means alive and not under construction. The network holds MaxTunnelCapacity units across all shafts (GlobalData
// + 0xA98, 5 in 2.02; TunnelTracker + 0x18 counts them, 0x008FA240). A route is only started while the units inside, those walking
// in and those waiting stay below twice that. With room left the unit enters A at once (aiEnter); otherwise it walks to A and
// waits there for a place. The order is recorded and the call ends at the switch's common tail, as a handled command does. A move
// to a unit already in the network only records where it is going. The AI repeats its orders: the same order again to a unit on
// its way in is absorbed. Any other order (except idle) to a unit on its way in detaches it: it is no longer sent in, waited for
// or rerouted; but a battalion that still enters (they get such orders on their way in) is taken out as usual, without getting
// its order again afterwards. A builder (porter) with a pending task is never routed, and one that gets a task on its way in is
// detached: its tasks run in its dozer machine without an order through aiDoCommand, and its AI state stays idle meanwhile.
//
// Every logic frame (TunnelContain::update's vtable slot, run by every shaft every frame once its player has a tunnel network;
// the pass runs once per logic frame): a waiting unit enters while the network has room left after the units already walking in; a
// unit in the network is taken out after SETTLE_SECONDS (the exit a shaft's Evacuate gives it, again every EXIT_RETRY_SECONDS, from
// the third attempt on at the shaft it is in) at the shaft nearest its goal, and LEAVE_SECONDS after it is out it gets its order
// again (a move the AI gives it meanwhile replaces the recorded one; after any other order but idle it gets nothing, nor does a
// builder that got a task meanwhile). A unit whose enter fails or that is not in after ENTER_SECONDS gets its order back and is
// not routed again for REROUTE_PAUSE_SECONDS; when its enter failed because the network filled up, it waits for a place
// instead (until ENTER_SECONDS are up). A unit that is not in after ENTER_SECONDS and is busy with something else (neither idle
// nor with its shaft as its AI's goal object) only loses its route. An order given back is the recorded one with its source
// (aiDoCommand treats an order from the player and one from the AI differently); the enters, walks and exits of the route itself
// come from the AI.
//
// All of it is logic state of the computer players, which every client simulates: the table holds object ids and is walked in slot
// order, distances are single precision SSE arithmetic, times are logic frames, and the table is emptied whenever the logic is
// reset (GameLogic::reset's two vtable slots) and when the logic frame goes back. Derived from pySAGE's ai-tunnel-routing
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    // aiDoCommand: esi is the AICommandParms, ebx its source, [ebp - 0x10] the AI interface (AIUpdate + 0x20), [ebp - 0x14] the
    // owning Object
    constexpr uintptr_t DO_COMMAND = 0x0066A7EC;
    constexpr uintptr_t DO_COMMAND_SETUP = 0x0066A7FE;
    constexpr uintptr_t DO_COMMAND_MODULE = 0x0066A81C;
    constexpr uintptr_t DO_COMMAND_OWNER = 0x0066A85E;
    constexpr uintptr_t DO_COMMAND_DISPATCH = 0x0066AA09;
    constexpr uintptr_t DO_COMMAND_SWITCH = 0x0066AA0F;
    constexpr uintptr_t DO_COMMAND_TAIL = 0x0066B03F;
    // thiscall(AI interface, Object *container, source), ret 8
    constexpr uintptr_t AI_ENTER = 0x00770EDE;
    constexpr uintptr_t AI_EXIT = 0x007716C1;
    // thiscall(AI interface, const Coord3D *, source), ret 8
    constexpr uintptr_t AI_MOVE_TO_POSITION = 0x0066C4CA;
    // thiscall(AI interface, const Coord3D *, int shots, source), ret 0xC
    constexpr uintptr_t AI_ATTACK_MOVE_TO_POSITION = 0x00696266;
    // thiscall(AIUpdate): the id of the AI state machine's current state
    constexpr uintptr_t AI_CURRENT_STATE_ID = 0x00660AC1;
    // thiscall(GameLogic, ObjectID), ret 4
    constexpr uintptr_t FIND_OBJECT_BY_ID = 0x00449681;
    constexpr uintptr_t FIND_OBJECT_BY_ID_RETURN = 0x004496A3;
    constexpr uintptr_t GET_CONTROLLING_PLAYER = 0x0068B678;
    // thiscall(object, bit): whether the bit is set in the object's status bits (Object + 0x94)
    constexpr uintptr_t STATUS_TEST = 0x0044DDEC;
    // the player's tunnel nearest a point: walks the tracker's list of tunnel ids the way nearestTunnel does
    constexpr uintptr_t PLAYER_NEAREST_TUNNEL = 0x0094C84F;
    // the tunnel-network guard: what contains its owner
    constexpr uintptr_t GUARD_CONTAINED_TEST = 0x0094CFAD;
    // TunnelTracker::isValidContainerFor: the room test against MaxTunnelCapacity
    constexpr uintptr_t TRACKER_IS_VALID_CONTAINER_FOR = 0x008FA240;
    // the contain interface's vtable, stored at module + 0x20 by the TunnelContain constructor
    constexpr uintptr_t TUNNEL_CONTAIN_VTABLE = 0x00C5DCE0;
    constexpr uintptr_t TUNNEL_CONTAIN_VTABLE_STORE = 0x00880DA4;
    constexpr uintptr_t TUNNEL_IS_VALID_CONTAINER_FOR = 0x0088103B;
    constexpr uint32_t IS_VALID_CONTAINER_FOR_SLOT = 0x98;
    // fastcall(module): the update, its sleep in eax
    constexpr uintptr_t TUNNEL_CONTAIN_UPDATE = 0x008812B0;
    constexpr uintptr_t TUNNEL_CONTAIN_UPDATE_SLOT = 0x00C5DE58;
    constexpr uintptr_t TUNNEL_CONTAIN_UPDATE_FRAME = 0x0088131A;
    // stdcall(Object *): the object's contain's horde interface, through the contain's vtable + 0x7C
    constexpr uintptr_t OBJECT_HORDE_INTERFACE = 0x00990C50;
    constexpr uint32_t GET_HORDE_INTERFACE_SLOT = 0x7C;
    constexpr uintptr_t HORDE_CONTAIN_HORDE_INTERFACE = 0x00872A6F;
    // thiscall(horde interface, Object *container, source), ret 8: the whole battalion leaves
    constexpr uintptr_t HORDE_EXIT = 0x00875EFD;
    constexpr uintptr_t HORDE_EXIT_SLOT = 0x00C5B27C;
    constexpr uint32_t HORDE_EXIT_SLOT_OFFSET = 0x84;
    // mov eax, [edi + 0x74]: an object's id
    constexpr uintptr_t OBJECT_ID_READ = 0x00875F23;
    // aiAttackMoveToPosition filling its parms: the position at + 8, the shots at + 0x34
    constexpr uintptr_t ATTACK_MOVE_PARMS = 0x0069628B;
    // aiDoCommand's case 5 (idle): the AIUpdate's vtable + 0x60 with the source
    constexpr uintptr_t DO_COMMAND_IDLE_CASE = 0x0066AB31;
    constexpr uintptr_t DO_COMMAND_IDLE_CASE_SLOT = 0x0066B090;
    // whether a builder is busy, as the AI asks it: KindOf byte + 0x109 bit 0x40 / the AIUpdate's getDozerAIInterface (vtable
    // + 0x174) / its isAnyTaskPending (+ 0x20)
    constexpr uintptr_t BUILDER_TEST = 0x00668514;
    constexpr uintptr_t BUILDER_TASK_TEST = 0x00668540;
    // thiscall(StateMachine): the goal object, from the ObjectID at + 0x20
    constexpr uintptr_t STATE_MACHINE_GOAL_OBJECT = 0x008DBACE;
    // the AI state machine's idle state is defined with id ebx, zeroed at the top of the function
    constexpr uintptr_t IDLE_STATE_ZERO = 0x0074D333;
    constexpr uintptr_t IDLE_STATE_DEFINITION = 0x0074D3D7;
    // the AI producer picker's dead test
    constexpr uintptr_t DEAD_TEST = 0x009A076C;
    // Player::setType: mov [esi + 0x5C], edi / ... / cmp edi, 1 (the AI)
    constexpr uintptr_t PLAYER_SET_TYPE_STORE = 0x006AA46C;
    constexpr uintptr_t PLAYER_SET_TYPE_COMPUTER = 0x006AA47D;
    constexpr uintptr_t GAME_LOGIC_RESET = 0x0062D232;
    constexpr uintptr_t GAME_LOGIC_RESET_SLOTS[2] = {0x00BD85B4, 0x00BFDB8C};

    constexpr uintptr_t THE_GAME_LOGIC = 0x00DE412C;
    constexpr uintptr_t GLOBAL_DATA = 0x00DE4364;
    constexpr uintptr_t LOGIC_FRAMES_PER_SECOND = 0x00D9F608;

    constexpr uint32_t GAME_LOGIC_FRAME = 0x40;
    constexpr uint32_t OBJECT_TEMPLATE = 0x04;
    constexpr uint32_t TEMPLATE_BUILDER_KIND = 0x109;
    constexpr uint8_t BUILDER_KIND_BIT = 0x40;
    constexpr uint32_t GET_DOZER_INTERFACE_SLOT = 0x174;
    constexpr uint32_t IS_ANY_TASK_PENDING_SLOT = 0x20;
    constexpr uint32_t MAX_TUNNEL_CAPACITY = 0xA98;
    constexpr uint32_t OBJECT_POSITION = 0x38;
    constexpr uint32_t OBJECT_ID = 0x74;
    constexpr uint32_t OBJECT_CONTAIN = 0x258;
    constexpr uint32_t OBJECT_AI_UPDATE = 0x260;
    constexpr uint32_t OBJECT_CONTAINED_BY = 0x27C;
    constexpr uint32_t OBJECT_DEAD_FLAGS = 0x458;
    constexpr uint32_t AI_INTERFACE = 0x20;
    constexpr uint32_t AI_STATE_MACHINE = 0x30;
    constexpr uint32_t STATE_MACHINE_GOAL_OBJECT_ID = 0x20;
    constexpr uint32_t PLAYER_TYPE = 0x5C;
    constexpr uint32_t PLAYER_TUNNEL_TRACKER = 0x308;
    constexpr uint32_t TRACKER_TUNNELS = 0x08;
    constexpr uint32_t TRACKER_PASSENGER_COUNT = 0x18;
    constexpr uint32_t LIST_NODE_VALUE = 2;
    constexpr uint32_t PARMS_POSITION = 0x08;
    constexpr uint32_t PARMS_SHOTS = 0x34;

    constexpr uint32_t PLAYER_COMPUTER = 1;
    // CMD_FROM_PLAYER, the source of the skirmish AI's army orders (pySAGE counted them in a running game)
    constexpr uint32_t FROM_PLAYER = 0;
    constexpr uint32_t FROM_AI = 2;
    constexpr uint32_t MOVE_TO_POSITION = 0x00;
    constexpr uint32_t IDLE = 0x05;
    constexpr uint32_t ATTACK_MOVE_TO_POSITION = 0x0F;
    constexpr uint32_t STATE_IDLE = 0;
    constexpr uint32_t UNDER_CONSTRUCTION = 2;

    // world units a trip through the network is charged on top of the two walks (a cell is ten units), so short trips walk
    constexpr float TRANSIT_COST = 200.0f;
    constexpr uint32_t ENTER_SECONDS = 60;
    // an exit at another shaft ordered before a unit has fully entered takes it out where it went in (seen in game)
    constexpr uint32_t SETTLE_SECONDS = 5;
    // time for a unit to walk out of the shaft before it gets its order again
    constexpr uint32_t LEAVE_SECONDS = 2;
    constexpr uint32_t EXIT_RETRY_SECONDS = 10;
    constexpr uint32_t REROUTE_PAUSE_SECONDS = 60;
    // from this exit attempt (counting from 0) on, the unit is taken out at the shaft it is in
    constexpr uint8_t FALLBACK_ATTEMPT = 2;
    // logic frames in a row a unit on its way in has to be idle before its enter counts as failed
    constexpr uint8_t FAILED_ENTER_IDLE_PASSES = 2;
    constexpr size_t SLOTS = 64;

    enum Phase : uint8_t {
        ENTERING = 1, // ordered into its first shaft
        QUEUED,       // walking to its first shaft, waiting for room in the network
        INSIDE,       // in the network, settling before its exit is ordered
        EXITING,      // in the network, its exit ordered
        WALKING,      // its route failed: it walks, and is not routed again before the deadline
        DETACHED,     // given another order or task on its way in: left alone, taken out only if it still enters before the deadline
        LEAVING       // out of the network, walking out of the shaft; gets its (latest) order at the deadline
    };

    struct Route {
        uint32_t unit; // ObjectID, 0 for a free slot
        uint32_t command;
        uint32_t source;
        float goal[3];
        int32_t shots;
        uint32_t tunnel; // ObjectID of the shaft it goes in at
        uintptr_t player;
        uint32_t deadline; // logic frame
        Phase phase;
        uint8_t attempts;
        uint8_t idlePasses;
        bool detached; // detached on its way in: does not get its order again after its exit
    };

    struct Network {
        int capacity;
        int inside;
        int entering;
        int queued;
    };

    typedef void *(__fastcall *FindObjectById)(void *logic, void *, uint32_t id);
    typedef void *(__fastcall *GetControllingPlayer)(void *object, void *);
    typedef bool (__fastcall *StatusTest)(void *object, void *, uint32_t bit);
    typedef uint32_t (__fastcall *CurrentStateId)(void *aiUpdate, void *);
    typedef bool (__fastcall *IsValidContainerFor)(void *contain, void *, void *object, bool checkCapacity, uint32_t);
    typedef void *(__fastcall *GetHordeInterface)(void *contain, void *);
    typedef void (__fastcall *ContainerOrder)(void *ai, void *, void *container, uint32_t source);
    typedef void (__fastcall *MoveOrder)(void *ai, void *, const float *position, uint32_t source);
    typedef void (__fastcall *AttackMoveOrder)(void *ai, void *, const float *position, int32_t shots, uint32_t source);
    typedef uint32_t (__fastcall *Update)(void *module, void *);
    typedef void *(__fastcall *GetDozerInterface)(void *aiUpdate, void *);
    typedef bool (__fastcall *IsAnyTaskPending)(void *dozer, void *);

    Route routes[SLOTS];
    // set while an order of this patch goes through aiDoCommand, which then passes it to the switch
    bool issuing;
    uint32_t lastPassFrame = 0xFFFFFFFF;
    uint32_t lastSeenFrame;

    void forgetRoutes() {
        memset(routes, 0, sizeof(routes));
        issuing = false;
        lastPassFrame = 0xFFFFFFFF;
        lastSeenFrame = 0;
    }

    void __cdecl onLogicReset() {
        forgetRoutes();
    }

    uint32_t currentFrame() {
        const uint32_t frame = field<uint32_t>(read<void *>(THE_GAME_LOGIC), GAME_LOGIC_FRAME);
        if (frame < lastSeenFrame) {
            forgetRoutes();
        }
        lastSeenFrame = frame;
        return frame;
    }

    uint32_t framesFor(const uint32_t seconds) {
        return read<uint32_t>(LOGIC_FRAMES_PER_SECOND) * seconds;
    }

    uint8_t *findObject(const uint32_t id) {
        return static_cast<uint8_t *>(reinterpret_cast<FindObjectById>(FIND_OBJECT_BY_ID)(read<void *>(THE_GAME_LOGIC), nullptr, id));
    }

    const float *position(const uint8_t *object) {
        return reinterpret_cast<const float *>(object + OBJECT_POSITION);
    }

    uint32_t goalObject(const void *aiUpdate) {
        return field<uint32_t>(field<uint8_t *>(aiUpdate, AI_STATE_MACHINE), STATE_MACHINE_GOAL_OBJECT_ID);
    }

    void *aiOf(const uint8_t *unit) {
        return field<uint8_t *>(unit, OBJECT_AI_UPDATE) + AI_INTERFACE;
    }

    bool isTunnelContain(const void *contain) {
        return contain != nullptr && *static_cast<const uintptr_t *>(contain) == TUNNEL_CONTAIN_VTABLE;
    }

    uint8_t *tunnelOf(const uint8_t *unit) {
        uint8_t *container = field<uint8_t *>(unit, OBJECT_CONTAINED_BY);
        if (container == nullptr || !isTunnelContain(field<void *>(container, OBJECT_CONTAIN))) {
            return nullptr;
        }
        return container;
    }

    bool usableTunnel(uint8_t *tunnel) {
        return tunnel != nullptr
            && (field<uint8_t>(tunnel, OBJECT_DEAD_FLAGS) & 1) == 0
            && !reinterpret_cast<StatusTest>(STATUS_TEST)(tunnel, nullptr, UNDER_CONSTRUCTION)
            && isTunnelContain(field<void *>(tunnel, OBJECT_CONTAIN));
    }

    uint8_t *controllingPlayer(uint8_t *object) {
        return static_cast<uint8_t *>(reinterpret_cast<GetControllingPlayer>(GET_CONTROLLING_PLAYER)(object, nullptr));
    }

    // the unit's controlling player, if that is a computer player with a tunnel network
    uint8_t *networkPlayer(uint8_t *unit) {
        uint8_t *player = controllingPlayer(unit);
        if (player == nullptr || field<uint32_t>(player, PLAYER_TYPE) != PLAYER_COMPUTER || field<void *>(player, PLAYER_TUNNEL_TRACKER) == nullptr) {
            return nullptr;
        }
        return player;
    }

    __m128 squaredDistance(const float *a, const float *b) {
        const __m128 dx = _mm_sub_ss(_mm_load_ss(a), _mm_load_ss(b));
        const __m128 dy = _mm_sub_ss(_mm_load_ss(a + 1), _mm_load_ss(b + 1));
        return _mm_add_ss(_mm_mul_ss(dx, dx), _mm_mul_ss(dy, dy));
    }

    __m128 distance(const float *a, const float *b) {
        return _mm_sqrt_ss(squaredDistance(a, b));
    }

    // the usable tunnel of the player's network nearest the point; the first of equally near ones in the tracker's list
    uint8_t *nearestTunnel(const uint8_t *player, const float *point) {
        const uint8_t *tracker = field<uint8_t *>(player, PLAYER_TUNNEL_TRACKER);
        if (tracker == nullptr) {
            return nullptr;
        }
        const auto *head = field<uint32_t *>(tracker, TRACKER_TUNNELS);
        uint8_t *nearest = nullptr;
        __m128 nearestDistance = _mm_setzero_ps();
        for (auto *node = reinterpret_cast<uint32_t *>(head[0]); node != head; node = reinterpret_cast<uint32_t *>(node[0])) {
            uint8_t *tunnel = findObject(node[LIST_NODE_VALUE]);
            if (!usableTunnel(tunnel)) {
                continue;
            }
            const __m128 d = squaredDistance(position(tunnel), point);
            if (nearest == nullptr || _mm_comilt_ss(d, nearestDistance)) {
                nearest = tunnel;
                nearestDistance = d;
            }
        }
        return nearest;
    }

    bool shorterThroughNetwork(const float *from, const float *goal, const uint8_t *entry, const uint8_t *exit) {
        const __m128 direct = distance(goal, from);
        const __m128 through = _mm_add_ss(_mm_add_ss(distance(position(entry), from), distance(goal, position(exit))), _mm_set_ss(TRANSIT_COST));
        return _mm_comigt_ss(direct, through) != 0;
    }

    Network network(const uint8_t *player) {
        Network n = {};
        n.capacity = field<int>(read<uint8_t *>(GLOBAL_DATA), MAX_TUNNEL_CAPACITY);
        const uint8_t *tracker = field<uint8_t *>(player, PLAYER_TUNNEL_TRACKER);
        n.inside = tracker != nullptr ? field<int>(tracker, TRACKER_PASSENGER_COUNT) : 0;
        for (const Route &route : routes) {
            if (route.unit == 0 || route.player != reinterpret_cast<uintptr_t>(player)) {
                continue;
            }
            if (route.phase == ENTERING) {
                n.entering++;
            } else if (route.phase == QUEUED) {
                n.queued++;
            }
        }
        return n;
    }

    Route *findRoute(const uint32_t unit) {
        for (Route &route : routes) {
            if (route.unit == unit) {
                return &route;
            }
        }
        return nullptr;
    }

    Route *freeRoute() {
        return findRoute(0);
    }

    void record(Route &route, const uint8_t *unit, const uint8_t *parms, const uint32_t source, const uint8_t *player) {
        route.unit = field<uint32_t>(unit, OBJECT_ID);
        route.command = field<uint32_t>(parms, 0);
        route.source = source;
        memcpy(route.goal, parms + PARMS_POSITION, sizeof(route.goal));
        route.shots = field<int32_t>(parms, PARMS_SHOTS);
        route.player = reinterpret_cast<uintptr_t>(player);
        route.detached = false;
    }

    bool sameOrder(const Route &route, const uint8_t *parms) {
        return route.command == field<uint32_t>(parms, 0) && memcmp(route.goal, parms + PARMS_POSITION, sizeof(route.goal)) == 0;
    }

    void enterTunnel(void *ai, uint8_t *tunnel) {
        issuing = true;
        reinterpret_cast<ContainerOrder>(AI_ENTER)(ai, nullptr, tunnel, FROM_AI);
        issuing = false;
    }

    void walkTo(void *ai, const float *point) {
        issuing = true;
        reinterpret_cast<MoveOrder>(AI_MOVE_TO_POSITION)(ai, nullptr, point, FROM_AI);
        issuing = false;
    }

    // a builder's tasks (build, repair) run in its dozer machine, not through aiDoCommand, and leave its AI state idle
    bool builderBusy(const uint8_t *unit) {
        if ((field<uint8_t>(field<uint8_t *>(unit, OBJECT_TEMPLATE), TEMPLATE_BUILDER_KIND) & BUILDER_KIND_BIT) == 0) {
            return false;
        }
        void *aiUpdate = field<void *>(unit, OBJECT_AI_UPDATE);
        void *dozer = aiUpdate != nullptr ? slot<GetDozerInterface>(aiUpdate, GET_DOZER_INTERFACE_SLOT)(aiUpdate, nullptr) : nullptr;
        return dozer != nullptr && slot<IsAnyTaskPending>(dozer, IS_ANY_TASK_PENDING_SLOT)(dozer, nullptr);
    }

    void *hordeInterface(const uint8_t *unit) {
        void *contain = field<void *>(unit, OBJECT_CONTAIN);
        return contain != nullptr ? slot<GetHordeInterface>(contain, GET_HORDE_INTERFACE_SLOT)(contain, nullptr) : nullptr;
    }

    // the order Evacuate gives this passenger: a battalion leaves through its horde interface, anything else through aiExit
    void exitAt(uint8_t *unit, uint8_t *tunnel) {
        issuing = true;
        void *horde = hordeInterface(unit);
        if (horde != nullptr) {
            slot<ContainerOrder>(horde, HORDE_EXIT_SLOT_OFFSET)(horde, nullptr, tunnel, FROM_AI);
        } else {
            reinterpret_cast<ContainerOrder>(AI_EXIT)(aiOf(unit), nullptr, tunnel, FROM_AI);
        }
        issuing = false;
    }

    void reissue(uint8_t *unit, const Route &route) {
        issuing = true;
        if (route.command == ATTACK_MOVE_TO_POSITION) {
            reinterpret_cast<AttackMoveOrder>(AI_ATTACK_MOVE_TO_POSITION)(aiOf(unit), nullptr, route.goal, route.shots, route.source);
        } else {
            reinterpret_cast<MoveOrder>(AI_MOVE_TO_POSITION)(aiOf(unit), nullptr, route.goal, route.source);
        }
        issuing = false;
    }

    void settle(Route &route, const uint32_t now) {
        route.phase = INSIDE;
        route.attempts = 0;
        route.deadline = now + framesFor(SETTLE_SECONDS);
    }

    void fallBack(uint8_t *unit, Route &route, const uint32_t now) {
        reissue(unit, route);
        route.phase = WALKING;
        route.deadline = now + framesFor(REROUTE_PAUSE_SECONDS);
    }

    // Called in front of aiDoCommand's switch. True when the order was taken over: the call then ends at the common tail.
    bool __cdecl onCommand(const uint8_t *parms, const uint32_t source, void *ai, uint8_t *unit) {
        if (issuing) {
            return false;
        }
        const uint32_t now = currentFrame();
        const uint32_t command = field<uint32_t>(parms, 0);
        Route *route = findRoute(field<uint32_t>(unit, OBJECT_ID));
        const bool contained = field<void *>(unit, OBJECT_CONTAINED_BY) != nullptr;

        if ((source != FROM_AI && source != FROM_PLAYER) || (command != MOVE_TO_POSITION && command != ATTACK_MOVE_TO_POSITION)) {
            if (route != nullptr && command != IDLE && !contained) {
                if (route->phase == ENTERING || route->phase == QUEUED) {
                    route->phase = DETACHED;
                    route->detached = true;
                    route->deadline = now + framesFor(ENTER_SECONDS);
                } else if (route->phase == LEAVING) {
                    route->unit = 0;
                }
            }
            return false;
        }

        if (contained) {
            if (tunnelOf(unit) == nullptr) {
                return false;
            }
            uint8_t *player = networkPlayer(unit);
            if (player == nullptr) {
                return false;
            }
            if (route == nullptr) {
                route = freeRoute();
                if (route == nullptr) {
                    return false;
                }
                settle(*route, now);
            } else if (route->phase != INSIDE && route->phase != EXITING) {
                settle(*route, now);
            }
            record(*route, unit, parms, source, player);
            return true;
        }

        if (route != nullptr) {
            const bool onItsWay = route->phase == ENTERING || route->phase == QUEUED;
            if (onItsWay && now < route->deadline && sameOrder(*route, parms) && usableTunnel(findObject(route->tunnel))) {
                return true;
            }
            if ((route->phase == WALKING || route->phase == DETACHED) && now < route->deadline) {
                return false;
            }
            if (route->phase == LEAVING && now < route->deadline) {
                record(*route, unit, parms, source, reinterpret_cast<const uint8_t *>(route->player));
                return true;
            }
            route->unit = 0;
        }

        if (builderBusy(unit)) {
            return false;
        }
        uint8_t *player = networkPlayer(unit);
        if (player == nullptr) {
            return false;
        }
        const auto *goal = reinterpret_cast<const float *>(parms + PARMS_POSITION);
        uint8_t *entry = nearestTunnel(player, position(unit));
        uint8_t *exit = entry != nullptr ? nearestTunnel(player, goal) : nullptr;
        if (exit == nullptr || exit == entry || !shorterThroughNetwork(position(unit), goal, entry, exit)) {
            return false;
        }
        void *contain = field<void *>(entry, OBJECT_CONTAIN);
        if (!slot<IsValidContainerFor>(contain, IS_VALID_CONTAINER_FOR_SLOT)(contain, nullptr, unit, false, 0)) {
            return false;
        }
        const Network n = network(player);
        if (n.inside + n.entering + n.queued >= 2 * n.capacity) {
            return false;
        }
        route = freeRoute();
        if (route == nullptr) {
            return false;
        }
        record(*route, unit, parms, source, player);
        route->tunnel = field<uint32_t>(entry, OBJECT_ID);
        route->deadline = now + framesFor(ENTER_SECONDS);
        route->attempts = 0;
        route->idlePasses = 0;
        if (n.inside + n.entering < n.capacity) {
            route->phase = ENTERING;
            enterTunnel(ai, entry);
        } else {
            route->phase = QUEUED;
            walkTo(ai, position(entry));
        }
        return true;
    }

    void advance(Route &route, const uint32_t now) {
        uint8_t *unit = findObject(route.unit);
        if (unit == nullptr || field<void *>(unit, OBJECT_AI_UPDATE) == nullptr) {
            route.unit = 0;
            return;
        }
        // the stock game idles a unit that changes owner (Object::onCapture, 0x00696F0A): it keeps neither its route nor its order
        if (field<void *>(unit, OBJECT_CONTAINED_BY) == nullptr && reinterpret_cast<uintptr_t>(controllingPlayer(unit)) != route.player) {
            route.unit = 0;
            return;
        }
        if (route.phase == WALKING) {
            if (now >= route.deadline) {
                route.unit = 0;
            }
            return;
        }

        if (field<void *>(unit, OBJECT_CONTAINED_BY) != nullptr) {
            uint8_t *tunnel = tunnelOf(unit);
            if (tunnel == nullptr) {
                route.unit = 0;
                return;
            }
            if (route.phase != INSIDE && route.phase != EXITING) {
                settle(route, now);
                return;
            }
            if (now < route.deadline) {
                return;
            }
            if (route.phase == EXITING) {
                route.attempts++;
            }
            route.phase = EXITING;
            route.deadline = now + framesFor(EXIT_RETRY_SECONDS);
            if (route.attempts < FALLBACK_ATTEMPT) {
                // the network the unit is in, also when it changed owner inside
                uint8_t *player = controllingPlayer(tunnel);
                uint8_t *exit = player != nullptr ? nearestTunnel(player, route.goal) : nullptr;
                if (exit != nullptr) {
                    tunnel = exit;
                }
            }
            exitAt(unit, tunnel);
            return;
        }

        if (route.phase == INSIDE || route.phase == EXITING) {
            route.phase = LEAVING;
            route.deadline = now + framesFor(LEAVE_SECONDS);
            return;
        }
        if (route.phase == LEAVING) {
            if (now >= route.deadline) {
                if (!route.detached && !builderBusy(unit)) {
                    reissue(unit, route);
                }
                route.unit = 0;
            }
            return;
        }
        if (route.phase == DETACHED) {
            if (now >= route.deadline) {
                route.unit = 0;
            }
            return;
        }
        if (builderBusy(unit)) {
            route.phase = DETACHED;
            route.detached = true;
            route.deadline = now + framesFor(ENTER_SECONDS);
            return;
        }

        uint8_t *entry = findObject(route.tunnel);
        void *aiUpdate = field<void *>(unit, OBJECT_AI_UPDATE);
        const uint32_t state = reinterpret_cast<CurrentStateId>(AI_CURRENT_STATE_ID)(aiUpdate, nullptr);
        if (now >= route.deadline || !usableTunnel(entry)) {
            const bool busyElsewhere = route.phase == ENTERING && state != STATE_IDLE && goalObject(aiUpdate) != route.tunnel;
            if (busyElsewhere) {
                route.unit = 0;
            } else {
                fallBack(unit, route, now);
            }
            return;
        }
        const Network n = network(reinterpret_cast<const uint8_t *>(route.player));
        if (route.phase == QUEUED) {
            if (n.inside + n.entering < n.capacity) {
                route.phase = ENTERING;
                route.idlePasses = 0;
                enterTunnel(aiOf(unit), entry);
            }
            return;
        }
        // a battalion's own AI stays idle while its members walk in
        if (state != STATE_IDLE || hordeInterface(unit) != nullptr) {
            route.idlePasses = 0;
            return;
        }
        if (++route.idlePasses < FAILED_ENTER_IDLE_PASSES) {
            return;
        }
        if (n.inside >= n.capacity) {
            route.phase = QUEUED;
            walkTo(aiOf(unit), position(entry));
        } else {
            fallBack(unit, route, now);
        }
    }

    uint32_t __fastcall onTunnelUpdate(void *module, void *) {
        const uint32_t sleep = reinterpret_cast<Update>(TUNNEL_CONTAIN_UPDATE)(module, nullptr);
        const uint32_t now = currentFrame();
        if (now != lastPassFrame) {
            lastPassFrame = now;
            for (Route &route : routes) {
                if (route.unit != 0) {
                    advance(route, now);
                }
            }
        }
        return sleep;
    }
}

bool applyAiTunnelRouting() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // In place of mov eax, [esi] / or dword [ebp + 8], -1: does both, then asks onCommand(parms, source, AI interface, owner)
    Cave cave(caveMemory);
    const uintptr_t dispatch = cave.here();
    const BYTE displaced[6] = {0x8B, 0x06, 0x83, 0x4D, 0x08, 0xFF};
    cave.emit(displaced);
    cave.emit8(0x60); // pushad
    const BYTE arguments[8] = {0xFF, 0x75, 0xEC, 0xFF, 0x75, 0xF0, 0x53, 0x56}; // push [ebp - 0x14] / push [ebp - 0x10] / push ebx / push esi
    cave.emit(arguments);
    cave.call(reinterpret_cast<uintptr_t>(&onCommand));
    const BYTE afterCall[5] = {0x83, 0xC4, 0x10, 0x84, 0xC0}; // add esp, 0x10 / test al, al
    cave.emit(afterCall);
    cave.emit8(0x61); // popad
    cave.jcc(JNE, DO_COMMAND_TAIL);
    cave.jmp(DO_COMMAND_SWITCH);

    // GameLogic::reset: forget every route, then the stock reset
    const uintptr_t reset = cave.here();
    cave.emit8(0x51); // push ecx
    cave.call(reinterpret_cast<uintptr_t>(&onLogicReset));
    cave.emit8(0x59); // pop ecx
    cave.jmp(GAME_LOGIC_RESET);

    static PatchSet set;
    set.reset();

    // mov eax, the exception frame's handler / call / sub esp, 0xC8
    const BYTE doCommandHead[16] = {0xB8, 0xD8, 0x72, 0xB8, 0x00, 0xE8, 0xFA, 0x26, 0x3D, 0x00, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00};
    set.expect(DO_COMMAND, doCommandHead);
    // mov esi, [ebp + 8]: the parms / mov ebx, [esi + 4]: the source / ... / mov [ebp - 0x10], ecx: the AI interface
    const BYTE doCommandSetup[23] = {
        0x8B, 0x75, 0x08, 0x8B, 0x5E, 0x04, 0x83, 0xA1, 0x84, 0x01, 0x00, 0x00, 0x00, 0x80, 0x3D, 0x48, 0x41, 0xDE, 0x00, 0x00,
        0x89, 0x4D, 0xF0
    };
    set.expect(DO_COMMAND_SETUP, doCommandSetup);
    // mov edi, ecx / add edi, -0x20: the AIUpdate
    const BYTE doCommandModule[5] = {0x8B, 0xF9, 0x83, 0xC7, 0xE0};
    set.expect(DO_COMMAND_MODULE, doCommandModule);
    // mov eax, [eax - 0x18]: the owner / test byte [eax + 0x1C9], 1 / mov [ebp - 0x14], eax
    const BYTE doCommandOwner[13] = {0x8B, 0x40, 0xE8, 0xF6, 0x80, 0xC9, 0x01, 0x00, 0x00, 0x01, 0x89, 0x45, 0xEC};
    set.expect(DO_COMMAND_OWNER, doCommandOwner);
    set.hook(DO_COMMAND_DISPATCH, displaced, dispatch);
    // cmp eax, 0x54 / ja / jmp [eax * 4 + table]
    const BYTE doCommandSwitch[16] = {0x83, 0xF8, 0x54, 0x0F, 0x87, 0x27, 0x06, 0x00, 0x00, 0xFF, 0x24, 0x85, 0x7C, 0xB0, 0x66, 0x00};
    set.expect(DO_COMMAND_SWITCH, doCommandSwitch);
    // mov ecx, [ebp - 0x14] / push 0x46 / call the status test / test al, al / je / cmp ebx, 2 (from the AI): where handled
    // commands end
    const BYTE doCommandTail[17] = {0x8B, 0x4D, 0xEC, 0x6A, 0x46, 0xE8, 0xA3, 0x2D, 0xDE, 0xFF, 0x84, 0xC0, 0x74, 0x1E, 0x83, 0xFB, 0x02};
    set.expect(DO_COMMAND_TAIL, doCommandTail);
    set.expectSlot(DO_COMMAND_IDLE_CASE_SLOT, DO_COMMAND_IDLE_CASE);
    // mov eax, [edi] / push ebx / mov ecx, edi / call [eax + 0x60] / jmp the tail
    const BYTE idleCase[13] = {0x8B, 0x07, 0x53, 0x8B, 0xCF, 0xFF, 0x50, 0x60, 0xE9, 0x01, 0x05, 0x00, 0x00};
    set.expect(DO_COMMAND_IDLE_CASE, idleCase);
    // mov eax, [esi + 4]: the template / test byte [eax + 0x109], 0x40 / je / mov eax, [ebx] / mov ecx, ebx / call [eax + 0x174]
    const BYTE builderTest[22] = {
        0x8B, 0x46, 0x04, 0xF6, 0x80, 0x09, 0x01, 0x00, 0x00, 0x40, 0x74, 0x2F, 0x8B, 0x03, 0x8B, 0xCB, 0xFF, 0x90, 0x74, 0x01, 0x00, 0x00
    };
    set.expect(BUILDER_TEST, builderTest);
    // mov eax, [edi] / mov ecx, edi / call [eax + 0x20] / test al, al
    const BYTE builderTaskTest[9] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x50, 0x20, 0x84, 0xC0};
    set.expect(BUILDER_TASK_TEST, builderTaskTest);
    // push [ecx + 0x20] / mov ecx, [TheGameLogic] / call findObjectById / ret
    const BYTE stateMachineGoalObject[15] = {0xFF, 0x71, 0x20, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0xE8, 0xA5, 0xDB, 0xB6, 0xFF, 0xC3};
    set.expect(STATE_MACHINE_GOAL_OBJECT, stateMachineGoalObject);
    const BYTE idleStateZero[2] = {0x33, 0xDB}; // xor ebx, ebx
    set.expect(IDLE_STATE_ZERO, idleStateZero);
    // push ebx: the id / mov ecx, esi / mov [ebp - 4], bl / call defineState
    const BYTE idleStateDefinition[11] = {0x53, 0x8B, 0xCE, 0x88, 0x5D, 0xFC, 0xE8, 0x3B, 0xEA, 0x18, 0x00};
    set.expect(IDLE_STATE_DEFINITION, idleStateDefinition);

    // the AI interface's orders: each builds its parms (push the type) and hands them to aiDoCommand
    const BYTE aiEnter[16] = {0xB8, 0x67, 0x2B, 0xB9, 0x00, 0xE8, 0x08, 0xC0, 0x2C, 0x00, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
    set.expect(AI_ENTER, aiEnter);
    const BYTE aiEnterType[2] = {0x6A, 0x42};
    set.expect(AI_ENTER + 0x16, aiEnterType);
    const BYTE aiEnterReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(AI_ENTER + 0x62, aiEnterReturn);
    const BYTE aiExit[24] = {
        0xB8, 0x67, 0x2B, 0xB9, 0x00, 0xE8, 0x25, 0xB8, 0x2C, 0x00, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x56, 0xFF, 0x75, 0x0C,
        0x8B, 0xF1, 0x6A, 0x1A
    };
    set.expect(AI_EXIT, aiExit);
    const BYTE aiExitReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(AI_EXIT + 0x62, aiExitReturn);
    const BYTE aiMove[24] = {
        0xB8, 0x11, 0x73, 0xB8, 0x00, 0xE8, 0x1C, 0x0A, 0x3D, 0x00, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57, 0xFF,
        0x75, 0x0C, 0x8B, 0xD9
    };
    set.expect(AI_MOVE_TO_POSITION, aiMove);
    const BYTE aiMoveType[2] = {0x6A, 0x00};
    set.expect(AI_MOVE_TO_POSITION + 0x18, aiMoveType);
    const BYTE aiMoveReturn[3] = {0xC2, 0x08, 0x00};
    set.expect(AI_MOVE_TO_POSITION + 0x69, aiMoveReturn);
    const BYTE aiAttackMove[24] = {
        0xB8, 0xC2, 0x8A, 0xB8, 0x00, 0xE8, 0x80, 0x6C, 0x3A, 0x00, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57, 0xFF,
        0x75, 0x10, 0x8B, 0xD9
    };
    set.expect(AI_ATTACK_MOVE_TO_POSITION, aiAttackMove);
    const BYTE aiAttackMoveType[2] = {0x6A, 0x0F};
    set.expect(AI_ATTACK_MOVE_TO_POSITION + 0x18, aiAttackMoveType);
    // mov esi, [ebp + 8]: the position / mov eax, [ebp + 0xC]: the shots / ... / movs x3 into the parms (ebp - 0xCC) + 8 / ...
    // mov [ebp - 0x98], eax: + 0x34
    const BYTE attackMoveParms[31] = {
        0x8B, 0x75, 0x08, 0x8B, 0x45, 0x0C, 0x83, 0x65, 0xFC, 0x00, 0x8D, 0xBD, 0x3C, 0xFF, 0xFF, 0xFF, 0xA5, 0xA5, 0x8D, 0x8D,
        0x34, 0xFF, 0xFF, 0xFF, 0xA5, 0x89, 0x85, 0x68, 0xFF, 0xFF, 0xFF
    };
    set.expectBytes(ATTACK_MOVE_PARMS, attackMoveParms);
    const BYTE aiAttackMoveReturn[3] = {0xC2, 0x0C, 0x00};
    set.expect(AI_ATTACK_MOVE_TO_POSITION + 0x72, aiAttackMoveReturn);
    // mov eax, [ecx + 0x30] / mov eax, [eax + 4]: the current state / ... / mov eax, [eax + 4]: its id / ret
    const BYTE currentStateId[20] = {
        0x8B, 0x41, 0x30, 0x8B, 0x40, 0x04, 0x85, 0xC0, 0x74, 0x04, 0x8B, 0x40, 0x04, 0xC3, 0xB8, 0x3F, 0x42, 0x0F, 0x00, 0xC3
    };
    set.expect(AI_CURRENT_STATE_ID, currentStateId);

    const BYTE findObjectById[8] = {0x83, 0x7C, 0x24, 0x04, 0x00, 0x74, 0x14, 0x8D};
    set.expect(FIND_OBJECT_BY_ID, findObjectById);
    const BYTE findObjectByIdReturn[3] = {0xC2, 0x04, 0x00};
    set.expect(FIND_OBJECT_BY_ID_RETURN, findObjectByIdReturn);
    const BYTE controllingPlayerCode[18] = {
        0x8B, 0x89, 0x1C, 0x03, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x05, 0xE9, 0xE8, 0x46, 0x11, 0x00, 0x33, 0xC0, 0xC3
    };
    set.expect(GET_CONTROLLING_PLAYER, controllingPlayerCode);
    // mov edx, [esp + 4] / xor eax, eax / push esi / mov esi, ecx
    const BYTE statusTest[9] = {0x8B, 0x54, 0x24, 0x04, 0x33, 0xC0, 0x56, 0x8B, 0xF1};
    set.expect(STATUS_TEST, statusTest);
    // test byte [edi + 0x458], 1 / jne the next candidate
    const BYTE deadTest[8] = {0xF6, 0x87, 0x58, 0x04, 0x00, 0x00, 0x01, 0x75};
    set.expect(DEAD_TEST, deadTest);
    // mov [esi + 0x5C], edi: the player type / ... / cmp edi, 1: a computer player gets an AI
    const BYTE playerTypeStore[3] = {0x89, 0x7E, 0x5C};
    set.expect(PLAYER_SET_TYPE_STORE, playerTypeStore);
    const BYTE playerTypeComputer[3] = {0x83, 0xFF, 0x01};
    set.expect(PLAYER_SET_TYPE_COMPUTER, playerTypeComputer);

    // the player's tunnel nearest a point: the tracker (Player + 0x308), its list of tunnel ids (+ 8, nodes {next, prev, id}),
    // findObjectById, and the squared distance in the ground plane from Object + 0x38
    const BYTE nearestTunnelCode[129] = {
        0x55, 0x8B, 0xEC, 0x51, 0x51, 0x85, 0xC0, 0x75, 0x02, 0xC9, 0xC3, 0x8B, 0x80, 0x08, 0x03, 0x00, 0x00, 0x83, 0x65, 0xFC,
        0x00, 0x0F, 0x57, 0xC0, 0x56, 0x83, 0xC0, 0x08, 0x57, 0x8B, 0x38, 0x8B, 0x37, 0xF3, 0x0F, 0x11, 0x45, 0xF8, 0xEB, 0x4E,
        0xFF, 0x76, 0x08, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0xE8, 0xFC, 0xCD, 0xAF, 0xFF, 0x85, 0xC0, 0x74, 0x3A, 0x83, 0x7D,
        0xFC, 0x00, 0xF3, 0x0F, 0x10, 0x40, 0x38, 0xF3, 0x0F, 0x5C, 0x03, 0xF3, 0x0F, 0x10, 0x48, 0x3C, 0xF3, 0x0F, 0x5C, 0x4B,
        0x04, 0x0F, 0x28, 0xD0, 0xF3, 0x0F, 0x59, 0xC9, 0xF3, 0x0F, 0x59, 0xD0, 0xF3, 0x0F, 0x58, 0xCA, 0x74, 0x0A, 0xF3, 0x0F,
        0x10, 0x45, 0xF8, 0x0F, 0x2F, 0xC1, 0x76, 0x08, 0xF3, 0x0F, 0x11, 0x4D, 0xF8, 0x89, 0x45, 0xFC, 0x8B, 0x36, 0x3B, 0xF7,
        0x75, 0xAE, 0x8B, 0x45, 0xFC, 0x5F, 0x5E, 0xC9, 0xC3
    };
    set.expectBytes(PLAYER_NEAREST_TUNNEL, nearestTunnelCode);
    // mov ecx, [ebx + 0x14]: the guard's owner / cmp dword [ecx + 0x27C], 0: what contains it
    const BYTE guardContainedTest[10] = {0x8B, 0x4B, 0x14, 0x83, 0xB9, 0x7C, 0x02, 0x00, 0x00, 0x00};
    set.expect(GUARD_CONTAINED_TEST, guardContainedTest);
    // the tracker's room test: [tracker + 0x18] < [GlobalData + 0xA98] (MaxTunnelCapacity)
    const BYTE trackerIsValidContainerFor[76] = {
        0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x08, 0x85, 0xC9, 0x74, 0x3B, 0x8B, 0x41, 0x04, 0xF6, 0x80, 0x09, 0x01, 0x00, 0x00,
        0x10, 0x75, 0x2F, 0x80, 0x7C, 0x24, 0x0C, 0x00, 0x74, 0x24, 0x6A, 0x26, 0xE8, 0x87, 0x3B, 0xB5, 0xFF, 0x84, 0xC0, 0x75,
        0x19, 0xA1, 0x64, 0x43, 0xDE, 0x00, 0x8B, 0x80, 0x98, 0x0A, 0x00, 0x00, 0x8B, 0x4E, 0x18, 0x33, 0xD2, 0x3B, 0xC8, 0x0F,
        0x9C, 0xC2, 0x8A, 0xC2, 0xEB, 0x06, 0xB0, 0x01, 0xEB, 0x02, 0x32, 0xC0, 0x5E, 0xC2, 0x08, 0x00
    };
    set.expectBytes(TRACKER_IS_VALID_CONTAINER_FOR, trackerIsValidContainerFor);
    // mov dword [ecx + 0x20], the contain interface's vtable
    const BYTE tunnelContainVtableStore[7] = {0xC7, 0x41, 0x20, 0xE0, 0xDC, 0xC5, 0x00};
    set.expect(TUNNEL_CONTAIN_VTABLE_STORE, tunnelContainVtableStore);
    const BYTE objectIdRead[3] = {0x8B, 0x47, 0x74};
    set.expect(OBJECT_ID_READ, objectIdRead);
    // TunnelContain::isValidContainerFor(object, checkCapacity, ...), ret 0xC: the contain's own tests, then the tracker's
    set.expectSlot(TUNNEL_CONTAIN_VTABLE + IS_VALID_CONTAINER_FOR_SLOT, TUNNEL_IS_VALID_CONTAINER_FOR);
    const BYTE tunnelIsValidContainerFor[55] = {
        0x56, 0xFF, 0x74, 0x24, 0x10, 0x8B, 0xF1, 0x6A, 0x00, 0xFF, 0x74, 0x24, 0x10, 0xE8, 0x93, 0xC0, 0xFF, 0xFF, 0x84, 0xC0,
        0x74, 0x1D, 0x8B, 0x4E, 0xE8, 0xE8, 0x1F, 0xA6, 0xE0, 0xFF, 0xFF, 0x74, 0x24, 0x0C, 0x8B, 0x80, 0x08, 0x03, 0x00, 0x00,
        0xFF, 0x74, 0x24, 0x0C, 0x8B, 0xC8, 0xE8, 0xD2, 0x91, 0x07, 0x00, 0x5E, 0xC2, 0x0C, 0x00
    };
    set.expectBytes(TUNNEL_IS_VALID_CONTAINER_FOR, tunnelIsValidContainerFor);

    // an object's horde interface: [object + 0x258] (its contain), vtable + 0x7C
    const BYTE objectHordeInterface[30] = {
        0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x11, 0x8B, 0x88, 0x58, 0x02, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x07, 0x8B, 0x01,
        0xFF, 0x50, 0x7C, 0xEB, 0x02, 0x33, 0xC0, 0xC2, 0x04, 0x00
    };
    set.expectBytes(OBJECT_HORDE_INTERFACE, objectHordeInterface);
    // HordeContain's: lea eax, [ecx - 0x20] / add ecx, 0xFC / neg eax / sbb eax, eax
    const BYTE hordeContainHordeInterface[10] = {0x8D, 0x41, 0xE0, 0x81, 0xC1, 0xFC, 0x00, 0x00, 0x00, 0xF7};
    set.expect(HORDE_CONTAIN_HORDE_INTERFACE, hordeContainHordeInterface);
    // the horde interface's exit: the horde's own AIUpdate (+ 0x260) exits the container
    set.expectSlot(HORDE_EXIT_SLOT, HORDE_EXIT);
    const BYTE hordeExit[27] = {
        0x8B, 0x81, 0xEC, 0xFE, 0xFF, 0xFF, 0x8B, 0x80, 0x60, 0x02, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x08, 0x8D, 0x48, 0x20, 0xE9,
        0xE6, 0xFB, 0xEF, 0xFF, 0xC2, 0x08, 0x00
    };
    set.expectBytes(HORDE_EXIT, hordeExit);

    set.replaceSlot(TUNNEL_CONTAIN_UPDATE_SLOT, TUNNEL_CONTAIN_UPDATE, reinterpret_cast<uintptr_t>(&onTunnelUpdate));
    // TunnelContain::update: push ecx / push esi / push edi / mov esi, ecx / call; ... its plain ret
    const BYTE tunnelUpdate[10] = {0x51, 0x56, 0x57, 0x8B, 0xF1, 0xE8, 0x25, 0xB6, 0xFF, 0xFF};
    set.expect(TUNNEL_CONTAIN_UPDATE, tunnelUpdate);
    const BYTE tunnelUpdateReturn[5] = {0x40, 0x5F, 0x5E, 0x59, 0xC3}; // inc eax / pop edi / pop esi / pop ecx / ret
    set.expect(TUNNEL_CONTAIN_UPDATE + 0xB2, tunnelUpdateReturn);
    // mov eax, [TheGameLogic] / ... / mov ebp, [eax + 0x40]: the logic frame / ... / add eax, [the logic frames per second]
    const BYTE tunnelUpdateFrame[22] = {
        0xA1, 0x2C, 0x41, 0xDE, 0x00, 0x55, 0x8B, 0x68, 0x40, 0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x44, 0x03, 0x05, 0x08, 0xF6,
        0xD9, 0x00
    };
    set.expect(TUNNEL_CONTAIN_UPDATE_FRAME, tunnelUpdateFrame);

    // GameLogic::reset: mov eax, the exception frame's handler / call / push ecx / push ecx / push ebx / push esi / mov esi, ecx
    const BYTE logicReset[16] = {0xB8, 0x3F, 0x41, 0xB8, 0x00, 0xE8, 0xB4, 0xFC, 0x40, 0x00, 0x51, 0x51, 0x53, 0x56, 0x8B, 0xF1};
    set.expect(GAME_LOGIC_RESET, logicReset);
    for (const uintptr_t resetSlot : GAME_LOGIC_RESET_SLOTS) {
        set.replaceSlot(resetSlot, GAME_LOGIC_RESET, reset);
    }

    forgetRoutes();

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
