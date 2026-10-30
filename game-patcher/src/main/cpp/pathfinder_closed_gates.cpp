#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "pathfinder_closed_gates.h"

// Paths through closed gates
//
// Problem: units ordered through an open gate walk through it after it has closed. Orders given after the close route around
// the gate.
// Fix: when a gate closes, every unit whose path runs through the gate plans a new path, as if it had been ordered again.
//
// Closing a gate for pathing (0x0089C9FA) updates the pathfinding map, but nothing checks the paths units already hold. The
// hook replaces its last call, which puts the gate back into the pathfinding map (0x0089CA78), with a call that does the same
// and then walks every object (TheGameLogic's object hash, in bucket order, which is the same on every machine). A unit whose
// path has a segment within the gate's bounding circle radius plus its own gets its path dropped (0x0066276B); its move state
// sees no path on its next update and plans one against the updated map. Units still waiting for a path are left alone (their
// request is planned against the new map anyway), and so are units in AI state 0x47, the one state that reads the path without
// checking it. The pathfinder also closes every gate of an enemy for pathing before it serves a player's requests and opens it
// right after (0x008ED700); that caller passes 0 as the function's argument, the real closes in the gate's update pass 1, and
// the walk only runs for 1. Derived from pySAGE's gate-close-repath (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t CLOSE_FOR_PATHING = 0x0089C9FA;
    constexpr uintptr_t CLOSE_ARGUMENT_TEST = 0x0089CA06;
    constexpr uintptr_t READD_SETUP = 0x0089CA6F;
    constexpr uintptr_t READD_CALL = 0x0089CA78;
    constexpr uintptr_t CLOSE_EPILOGUE = 0x0089CA7D;
    constexpr uintptr_t GATE_CLOSE_CALL = 0x0089CDFB;
    constexpr uintptr_t GATE_CLOSE_CALL_2 = 0x0089CE72;
    constexpr uintptr_t PATHING_BRACKET_CALL = 0x008ED700;
    // thiscall(pathfinder, object), ret 4
    constexpr uintptr_t PATHFINDER_ADD_OBJECT = 0x006E85E9;
    // thiscall(ai update): drops the unit's path
    constexpr uintptr_t DESTROY_PATH = 0x0066276B;
    // thiscall(ai update): the id of the AI's current state; keeps ecx
    constexpr uintptr_t CURRENT_STATE = 0x00660AC1;
    // the move state: no path makes it plan one; state 0x47 reads the path unchecked
    constexpr uintptr_t MOVE_NO_PATH = 0x00748F35;
    constexpr uintptr_t MOVE_STATE_TEST = 0x0074911B;
    constexpr uintptr_t MOVE_PATH_READ = 0x0074927C;
    // the layouts the walk reads: the object hash, path nodes, the bounding circle radius
    constexpr uintptr_t OBJECT_HASH_WALK = 0x006B4EA6;
    constexpr uintptr_t PATH_NODE_LINK = 0x00665480;
    constexpr uintptr_t PATH_DESTRUCTOR = 0x007666FF;
    constexpr uintptr_t RADIUS_READ = 0x0066DCC9;

    constexpr BYTE NO_PATH_UNSAFE_STATE = 0x47;
    constexpr BYTE JBE = 0x86;

    size_t shortJump(Cave &cave, const BYTE opcode) {
        const size_t offset = cave.size;
        cave.emit8(opcode);
        cave.emit8(0x00);
        return offset;
    }

    void resolveShortJump(Cave &cave, const size_t jumpOffset, const size_t targetOffset) {
        cave.base[jumpOffset + 1] = static_cast<BYTE>(targetOffset - (jumpOffset + 2));
    }

    size_t forwardJcc(Cave &cave, const BYTE condition) {
        const size_t offset = cave.size;
        cave.jcc(condition, 0);
        return offset;
    }
}

bool applyPathfinderClosedGates() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // Entered by a call in place of the re-add: [esp + 4] is the gate, ecx the pathfinder, and past the four registers the close
    // saved and its return address, [esp + 0x1C] is the close's own argument. Leaves by ret 4 like the re-add. In the walk ebp is
    // the gate, esi and edi the bucket cursor and end, ebx the hash entry, edx the path node, xmm7 the squared reach.
    Cave cave(caveMemory);
    const BYTE readd[4] = {0xFF, 0x74, 0x24, 0x04}; // push dword [esp + 4]
    cave.emit(readd);
    cave.call(PATHFINDER_ADD_OBJECT);
    const BYTE closing[10] = {
        0x80, 0x7C, 0x24, 0x1C, 0x00, // cmp byte [esp + 0x1C], 0
        0x75, 0x03, // jne walk
        0xC2, 0x04, 0x00 // ret 4
    };
    cave.emit(closing);
    const BYTE walk[25] = {
        0x53, 0x56, 0x57, 0x55, // push ebx / push esi / push edi / push ebp
        0x8B, 0x6C, 0x24, 0x14, // mov ebp, [esp + 0x14]: the gate
        0xA1, 0x2C, 0x41, 0xDE, 0x00, // mov eax, [0x00DE412C]: TheGameLogic
        0x8B, 0xB0, 0xB8, 0x00, 0x00, 0x00, // mov esi, [eax + 0xB8]: the first bucket
        0x8B, 0xB8, 0xBC, 0x00, 0x00, 0x00 // mov edi, [eax + 0xBC]: the end of the buckets
    };
    cave.emit(walk);

    const uintptr_t nextBucket = cave.here();
    const BYTE bucketTest[2] = {0x3B, 0xF7}; // cmp esi, edi
    cave.emit(bucketTest);
    const size_t allBuckets = forwardJcc(cave, JAE);
    const BYTE bucket[5] = {0x8B, 0x1E, 0x83, 0xC6, 0x04}; // mov ebx, [esi] / add esi, 4
    cave.emit(bucket);

    const uintptr_t chain = cave.here();
    const BYTE entryTest[2] = {0x85, 0xDB}; // test ebx, ebx
    cave.emit(entryTest);
    cave.jcc(JE, nextBucket);
    const BYTE object[5] = {0x8B, 0x43, 0x08, 0x85, 0xC0}; // mov eax, [ebx + 8]: the object / test eax, eax
    cave.emit(object);
    const size_t noObject = forwardJcc(cave, JE);
    const BYTE notTheGate[2] = {0x3B, 0xC5}; // cmp eax, ebp
    cave.emit(notTheGate);
    const size_t theGate = forwardJcc(cave, JE);
    const BYTE aiUpdate[8] = {0x8B, 0x88, 0x60, 0x02, 0x00, 0x00, 0x85, 0xC9}; // mov ecx, [eax + 0x260] / test ecx, ecx
    cave.emit(aiUpdate);
    const size_t noAi = forwardJcc(cave, JE);
    const BYTE waitingForPath[7] = {0x80, 0xB9, 0xB1, 0x03, 0x00, 0x00, 0x00}; // cmp byte [ecx + 0x3B1], 0
    cave.emit(waitingForPath);
    const size_t waiting = forwardJcc(cave, JNE);
    const BYTE path[8] = {0x8B, 0x91, 0x40, 0x01, 0x00, 0x00, 0x85, 0xD2}; // mov edx, [ecx + 0x140]: the path / test edx, edx
    cave.emit(path);
    const size_t noPath = forwardJcc(cave, JE);
    const BYTE firstNode[5] = {0x8B, 0x52, 0x04, 0x85, 0xD2}; // mov edx, [edx + 4] / test edx, edx
    cave.emit(firstNode);
    const size_t noNode = forwardJcc(cave, JE);

    const BYTE reach[30] = {
        0xF3, 0x0F, 0x10, 0xBD, 0xB8, 0x00, 0x00, 0x00, // movss xmm7, [ebp + 0xB8]: the gate's radius
        0xF3, 0x0F, 0x58, 0xB8, 0xB8, 0x00, 0x00, 0x00, // addss xmm7, [eax + 0xB8]: plus the unit's
        0xF3, 0x0F, 0x59, 0xFF, // mulss xmm7, xmm7
        0xF3, 0x0F, 0x10, 0x42, 0x0C, // movss xmm0, [edx + 0xC]: P0
        0xF3, 0x0F, 0x10, 0x4A, 0x10 // movss xmm1, [edx + 0x10]
    };
    cave.emit(reach);

    // for each segment P0 -> P1 (the last node is tested as a point, P1 = P0)
    const uintptr_t segment = cave.here();
    const BYTE nextNode[8] = {
        0x8B, 0x02, // mov eax, [edx]: the next node
        0x85, 0xC0, // test eax, eax
        0x75, 0x02, // jne haveNext
        0x8B, 0xC2 // mov eax, edx
    };
    cave.emit(nextNode);
    const BYTE distance[72] = {
        0xF3, 0x0F, 0x10, 0x50, 0x0C, // haveNext: movss xmm2, [eax + 0xC]: P1
        0xF3, 0x0F, 0x10, 0x58, 0x10, // movss xmm3, [eax + 0x10]
        0xF3, 0x0F, 0x10, 0x65, 0x38, // movss xmm4, [ebp + 0x38]: w = gate - P0
        0xF3, 0x0F, 0x5C, 0xE0, // subss xmm4, xmm0
        0xF3, 0x0F, 0x10, 0x6D, 0x3C, // movss xmm5, [ebp + 0x3C]
        0xF3, 0x0F, 0x5C, 0xE9, // subss xmm5, xmm1
        0xF3, 0x0F, 0x5C, 0xD0, // subss xmm2, xmm0: d = P1 - P0
        0xF3, 0x0F, 0x5C, 0xD9, // subss xmm3, xmm1
        0x0F, 0x28, 0xF4, // movaps xmm6, xmm4: wd = w . d
        0xF3, 0x0F, 0x59, 0xF2, // mulss xmm6, xmm2
        0x0F, 0x28, 0xC5, // movaps xmm0, xmm5
        0xF3, 0x0F, 0x59, 0xC3, // mulss xmm0, xmm3
        0xF3, 0x0F, 0x58, 0xF0, // addss xmm6, xmm0
        0xF3, 0x0F, 0x59, 0xE4, // mulss xmm4, xmm4: ww = w . w
        0xF3, 0x0F, 0x59, 0xED, // mulss xmm5, xmm5
        0xF3, 0x0F, 0x58, 0xE5, // addss xmm4, xmm5
        0x0F, 0x57, 0xC9, // xorps xmm1, xmm1
        0x0F, 0x2F, 0xF1 // comiss xmm6, xmm1
    };
    cave.emit(distance);
    const size_t startClosest = shortJump(cave, 0x76); // jbe compare: P0 is closest, the distance is ww
    const BYTE segmentLength[15] = {
        0xF3, 0x0F, 0x59, 0xD2, // mulss xmm2, xmm2: dd = d . d
        0xF3, 0x0F, 0x59, 0xDB, // mulss xmm3, xmm3
        0xF3, 0x0F, 0x58, 0xD3, // addss xmm2, xmm3
        0x0F, 0x2F, 0xF2 // comiss xmm6, xmm2
    };
    cave.emit(segmentLength);
    const size_t endClosest = shortJump(cave, 0x73); // jae pastEnd: P1 is closest
    const BYTE inside[12] = {
        0xF3, 0x0F, 0x59, 0xF6, // mulss xmm6, xmm6: the distance is ww - wd * wd / dd
        0xF3, 0x0F, 0x5E, 0xF2, // divss xmm6, xmm2
        0xF3, 0x0F, 0x5C, 0xE6 // subss xmm4, xmm6
    };
    cave.emit(inside);
    const size_t insideDone = shortJump(cave, 0xEB); // jmp compare
    const size_t pastEnd = cave.size;
    const BYTE endPoint[32] = {
        0xF3, 0x0F, 0x10, 0x65, 0x38, // movss xmm4, [ebp + 0x38]: |gate - P1|
        0xF3, 0x0F, 0x5C, 0x60, 0x0C, // subss xmm4, [eax + 0xC]
        0xF3, 0x0F, 0x59, 0xE4, // mulss xmm4, xmm4
        0xF3, 0x0F, 0x10, 0x6D, 0x3C, // movss xmm5, [ebp + 0x3C]
        0xF3, 0x0F, 0x5C, 0x68, 0x10, // subss xmm5, [eax + 0x10]
        0xF3, 0x0F, 0x59, 0xED, // mulss xmm5, xmm5
        0xF3, 0x0F, 0x58, 0xE5 // addss xmm4, xmm5
    };
    cave.emit(endPoint);
    const size_t compare = cave.size;
    const BYTE withinReach[3] = {0x0F, 0x2F, 0xE7}; // comiss xmm4, xmm7 (a NaN counts as within reach: planning again is safe)
    cave.emit(withinReach);
    const size_t throughGate = forwardJcc(cave, JBE);
    const BYTE wasLast[2] = {0x3B, 0xC2}; // cmp eax, edx
    cave.emit(wasLast);
    const size_t pathDone = forwardJcc(cave, JE);
    const BYTE advance[12] = {
        0x8B, 0xD0, // mov edx, eax: P0 = P1
        0xF3, 0x0F, 0x10, 0x42, 0x0C, // movss xmm0, [edx + 0xC]
        0xF3, 0x0F, 0x10, 0x4A, 0x10 // movss xmm1, [edx + 0x10]
    };
    cave.emit(advance);
    cave.jmp(segment);

    // the path runs through the gate: drop it, unless the AI's state reads it unchecked
    const uintptr_t dropPath = cave.here();
    const BYTE unitAi[9] = {0x8B, 0x43, 0x08, 0x8B, 0x88, 0x60, 0x02, 0x00, 0x00}; // mov eax, [ebx + 8] / mov ecx, [eax + 0x260]
    cave.emit(unitAi);
    cave.call(CURRENT_STATE);
    const BYTE unsafeState[3] = {0x83, 0xF8, NO_PATH_UNSAFE_STATE}; // cmp eax, 0x47
    cave.emit(unsafeState);
    const size_t unsafe = forwardJcc(cave, JE);
    cave.call(DESTROY_PATH);

    const uintptr_t nextEntry = cave.here();
    const BYTE nextInChain[2] = {0x8B, 0x1B}; // mov ebx, [ebx]
    cave.emit(nextInChain);
    cave.jmp(chain);

    const uintptr_t done = cave.here();
    const BYTE epilogue[7] = {0x5D, 0x5F, 0x5E, 0x5B, 0xC2, 0x04, 0x00}; // pop ebp / pop edi / pop esi / pop ebx / ret 4
    cave.emit(epilogue);

    cave.resolveJcc(allBuckets, done);
    cave.resolveJcc(noObject, nextEntry);
    cave.resolveJcc(theGate, nextEntry);
    cave.resolveJcc(noAi, nextEntry);
    cave.resolveJcc(waiting, nextEntry);
    cave.resolveJcc(noPath, nextEntry);
    cave.resolveJcc(noNode, nextEntry);
    resolveShortJump(cave, startClosest, compare);
    resolveShortJump(cave, endClosest, pastEnd);
    resolveShortJump(cave, insideDone, compare);
    cave.resolveJcc(throughGate, dropPath);
    cave.resolveJcc(pathDone, nextEntry);
    cave.resolveJcc(unsafe, nextEntry);

    static PatchSet set;
    set.reset();

    // push esi / push edi / xor edi, edi / mov esi, ecx / inc edi / cmp [esi + 0x2C], edi / je
    const BYTE closeForPathing[12] = {0x56, 0x57, 0x33, 0xFF, 0x8B, 0xF1, 0x47, 0x39, 0x7E, 0x2C, 0x74, 0x79};
    set.expect(CLOSE_FOR_PATHING, closeForPathing);
    // cmp byte [esp + 0xC], 0 / je / call: the close's own argument
    const BYTE closeArgumentTest[12] = {0x80, 0x7C, 0x24, 0x0C, 0x00, 0x74, 0x05, 0xE8, 0x47, 0xFD, 0xFF, 0xFF};
    set.expect(CLOSE_ARGUMENT_TEST, closeArgumentTest);
    // mov eax, [TheAI] / mov ecx, [eax + 0x10] / push ebx: the pathfinder and the gate
    const BYTE readdSetup[9] = {0xA1, 0x40, 0x4B, 0xDE, 0x00, 0x8B, 0x48, 0x10, 0x53};
    set.expect(READD_SETUP, readdSetup);
    const BYTE readdCall[5] = {0xE8, 0x6C, 0xBB, 0xE4, 0xFF};
    set.redirectCall(READD_CALL, readdCall, reinterpret_cast<uintptr_t>(caveMemory));
    // pop ebp / pop ebx / pop edi / pop esi / ret 4: four saved registers between the re-add and the argument
    const BYTE closeEpilogue[7] = {0x5D, 0x5B, 0x5F, 0x5E, 0xC2, 0x04, 0x00};
    set.expect(CLOSE_EPILOGUE, closeEpilogue);

    // push 1 / lea ecx, [esi - 0x14] / call CLOSE_FOR_PATHING: the gate's update closes it
    const BYTE gateCloseCall[10] = {0x6A, 0x01, 0x8D, 0x4E, 0xEC, 0xE8, 0xF5, 0xFB, 0xFF, 0xFF};
    set.expect(GATE_CLOSE_CALL, gateCloseCall);
    const BYTE gateCloseCall2[10] = {0x6A, 0x01, 0x8D, 0x4E, 0xEC, 0xE8, 0x7E, 0xFB, 0xFF, 0xFF};
    set.expect(GATE_CLOSE_CALL_2, gateCloseCall2);
    // test eax, eax / jne / push eax (0) / mov ecx, edi / call: the pathfinder's bracket closes with 0
    const BYTE pathingBracketCall[12] = {0x85, 0xC0, 0x75, 0x08, 0x50, 0x8B, 0xCF, 0xE8, 0xEE, 0xF2, 0xFA, 0xFF};
    set.expect(PATHING_BRACKET_CALL, pathingBracketCall);

    // push 0 / push 0 / push 1 / push [esp + 0x10] / call / ret 4
    const BYTE addObject[18] = {0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x01, 0xFF, 0x74, 0x24, 0x10, 0xE8, 0x85, 0xE5, 0x24, 0x00, 0xC2, 0x04, 0x00};
    set.expect(PATHFINDER_ADD_OBJECT, addObject);
    // push ebx / push esi / mov esi, ecx / push edi / lea edi, [esi + 0x140] (the path) / mov ebx, [edi] / ... and the flags it
    // clears, waiting for a path (+0x3B1) among them
    const BYTE destroyPath[24] = {
        0x53, 0x56, 0x8B, 0xF1, 0x57, 0x8D, 0xBE, 0x40, 0x01, 0x00, 0x00, 0x8B, 0x1F, 0x85, 0xDB, 0x74, 0x0E, 0x8B, 0xCB, 0xE8,
        0x7C, 0x3F, 0x10, 0x00
    };
    set.expect(DESTROY_PATH, destroyPath);
    const BYTE destroyPathFlags[24] = {
        0x53, 0xE8, 0x17, 0xCF, 0xDC, 0xFF, 0x59, 0x83, 0x27, 0x00, 0x8B, 0x06, 0x5F, 0xC6, 0x86, 0xB1, 0x03, 0x00, 0x00, 0x00,
        0xC6, 0x86, 0xB8, 0x03
    };
    set.expect(DESTROY_PATH + 24, destroyPathFlags);
    // mov eax, [ecx + 0x30] / mov eax, [eax + 4] / test eax, eax / je / mov eax, [eax + 4] / ret / mov eax, 999999 / ret
    const BYTE currentState[20] = {
        0x8B, 0x41, 0x30, 0x8B, 0x40, 0x04, 0x85, 0xC0, 0x74, 0x04, 0x8B, 0x40, 0x04, 0xC3, 0xB8, 0x3F, 0x42, 0x0F, 0x00, 0xC3
    };
    set.expect(CURRENT_STATE, currentState);

    const BYTE moveNoPath[14] = {0x83, 0x7D, 0xF8, 0x00, 0xC6, 0x45, 0xFF, 0x00, 0x75, 0x04, 0xC6, 0x45, 0xFF, 0x01};
    set.expect(MOVE_NO_PATH, moveNoPath);
    // mov ecx, [ebx + 0x260] / call CURRENT_STATE / cmp eax, 0x47
    const BYTE moveStateTest[14] = {0x8B, 0x8B, 0x60, 0x02, 0x00, 0x00, 0xE8, 0x9B, 0x79, 0xF1, 0xFF, 0x83, 0xF8, 0x47};
    set.expect(MOVE_STATE_TEST, moveStateTest);
    // mov eax, [esi + 0x140] / movss xmm0, [edi + 0x20] / mov eax, [eax + 8]
    const BYTE movePathRead[14] = {0x8B, 0x86, 0x40, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x47, 0x20, 0x8B, 0x40, 0x08};
    set.expect(MOVE_PATH_READ, movePathRead);

    // an object hash lookup: entries are {next, id, object}
    const BYTE objectHashWalk[19] = {0x8B, 0x41, 0x04, 0x8B, 0x04, 0x90, 0xEB, 0x07, 0x39, 0x70, 0x04, 0x74, 0x06, 0x8B, 0x00, 0x85, 0xC0, 0x75, 0xF5};
    set.expect(OBJECT_HASH_WALK, objectHashWalk);
    // a path appends a node: nodes start with their next link, the path holds its first node at +4
    const BYTE pathNodeLink[12] = {0x8B, 0x4E, 0x08, 0x85, 0xC9, 0x74, 0x05, 0x89, 0x01, 0x89, 0x48, 0x04};
    set.expect(PATH_NODE_LINK, pathNodeLink);
    const BYTE pathDestructor[23] = {
        0x8B, 0x41, 0x04, 0x85, 0xC0, 0x74, 0x11, 0x56, 0x8B, 0x30, 0x50, 0xE8, 0x99, 0xEB, 0xEF, 0xFF, 0x85, 0xF6, 0x59, 0x8B,
        0xC6, 0x75, 0xF1
    };
    set.expect(PATH_DESTRUCTOR, pathDestructor);
    // movss xmm3, [eax + 0xB8]: an object's bounding circle radius
    const BYTE radiusRead[8] = {0xF3, 0x0F, 0x10, 0x98, 0xB8, 0x00, 0x00, 0x00};
    set.expect(RADIUS_READ, radiusRead);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
