#include <windows.h>
#include <cstdint>
#include <cstring>

#include "patch_set.h"
#include "tooltip_kill_count.h"

// Tooltip kill count
//
// Problem: the game counts every unit's kills but shows the number nowhere.
// Fix: the hover tooltip of a unit gets a last line with its kills, formatted from the string TOOLTIP:Kills (one %d, e.g.
// "Kills: %d"). A battalion member shows the battalion's total. Nothing is shown at zero kills or while the mod's string table
// has no TOOLTIP:Kills.
//
// The kill scorer (0x006955BC) counts each kill at Object + 0x48C, on the killer and on its battalion (0x00695701). The world
// tooltip builder adds one line per record, e.g. TOOLTIP:BattlesFought from Object + 0x488 (0x008EC052): fetch the string,
// format the number into an empty temporary (UnicodeString::format, 0x00ADF7E0, replaces its destination), add the line
// (0x008EBC3B). The hook sits on the release of the description temporary (0x008EC120), after the last line, does the same for
// the kills and then makes the release it replaced. Only the client's tooltip changes. Derived from pySAGE's kill-count-tooltip
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t KILL_SCORER_COUNT = 0x00695701;
    constexpr uintptr_t KILL_SCORER_BATTALION_TEST = 0x00695713;
    constexpr uintptr_t BATTLES_FOUGHT_LINE = 0x008EC052;
    constexpr uintptr_t BATTLES_FOUGHT_FETCH = 0x008EC06A;
    constexpr uintptr_t DESCRIPTION_RELEASE = 0x008EC120;
    // cdecl(lines, line)
    constexpr uintptr_t ADD_LINE = 0x008EBC3B;
    // cdecl(destination, format, ...)
    constexpr uintptr_t UNICODE_STRING_FORMAT = 0x00ADF7E0;
    // thiscall(string)
    constexpr uintptr_t UNICODE_STRING_RELEASE = 0x004367B0;
    constexpr uintptr_t THE_GAME_TEXT = 0x00DE4B04;

    constexpr char LABEL[] = "TOOLTIP:Kills";
    constexpr size_t CODE_OFFSET = 0x10;
}

bool applyTooltipKillCount() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    static_assert(sizeof(LABEL) <= CODE_OFFSET, "the label must fit in front of the code");
    memcpy(caveMemory, LABEL, sizeof(LABEL));
    // Entered by the call that released the description, with ecx the description and ebp the builder's frame: the hovered
    // object at [ebp - 0x18], the tooltip's lines at [ebp - 0x1C].
    Cave cave(caveMemory);
    cave.size = CODE_OFFSET;
    const BYTE object[6] = {
        0x60, // pushad
        0x8B, 0x75, 0xE8, // mov esi, [ebp - 0x18]: the hovered object
        0x85, 0xF6 // test esi, esi
    };
    cave.emit(object);
    const size_t noObject = cave.size;
    const BYTE noObjectJump[2] = {0x74, 0x00}; // jz done
    cave.emit(noObjectJump);
    const BYTE battalion[24] = {
        0x8B, 0x86, 0x7C, 0x02, 0x00, 0x00, // mov eax, [esi + 0x27C]: its container
        0x85, 0xC0, // test eax, eax
        0x74, 0x0E, // jz own
        0x8B, 0x50, 0x04, // mov edx, [eax + 4]: the container's template
        0xF6, 0x82, 0x15, 0x01, 0x00, 0x00, 0x20, // test byte [edx + 0x115], 0x20: KindOf HORDE
        0x74, 0x02, // jz own
        0x8B, 0xF0 // mov esi, eax: the battalion
    };
    cave.emit(battalion);
    const BYTE kills[8] = {
        0x8B, 0x86, 0x8C, 0x04, 0x00, 0x00, // own: mov eax, [esi + 0x48C]: the kills
        0x85, 0xC0 // test eax, eax
    };
    cave.emit(kills);
    const size_t noKills = cave.size;
    const BYTE noKillsJump[2] = {0x7E, 0x00}; // jle done
    cave.emit(noKillsJump);
    const BYTE locals[16] = {
        0x83, 0xEC, 0x08, // sub esp, 8: [esp] whether the string exists, [esp + 4] the line
        0x83, 0x24, 0x24, 0x00, // and dword [esp], 0
        0x83, 0x64, 0x24, 0x04, 0x00, // and dword [esp + 4], 0
        0x8B, 0xDC, // mov ebx, esp
        0x50, // push eax: the kills, for the format
        0x53 // push ebx
    };
    cave.emit(locals);
    cave.emit8(0x68); // push LABEL
    cave.emit32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(caveMemory)));
    cave.emit8(0x8B); // mov ecx, [THE_GAME_TEXT]
    cave.emit8(0x0D);
    cave.emit32(static_cast<uint32_t>(THE_GAME_TEXT));
    const BYTE fetchCall[8] = {
        0x8B, 0x11, // mov edx, [ecx]
        0xFF, 0x52, 0x44, // call [edx + 0x44]: fetch, ret 8
        0x83, 0x3B, 0x00 // cmp dword [ebx], 0
    };
    cave.emit(fetchCall);
    const size_t missingString = cave.size;
    cave.jcc(JE, 0);
    const BYTE format[5] = {
        0x50, // push eax: the format
        0x8D, 0x43, 0x04, // lea eax, [ebx + 4]
        0x50 // push eax: the line
    };
    cave.emit(format);
    cave.call(UNICODE_STRING_FORMAT);
    const BYTE addLine[11] = {
        0x83, 0xC4, 0x0C, // add esp, 0xC: line, format, kills
        0x8D, 0x43, 0x04, // lea eax, [ebx + 4]
        0x50, // push eax
        0x8D, 0x45, 0xE4, // lea eax, [ebp - 0x1C]: the lines
        0x50 // push eax
    };
    cave.emit(addLine);
    cave.call(ADD_LINE);
    const BYTE release[6] = {
        0x83, 0xC4, 0x08, // add esp, 8
        0x8D, 0x4B, 0x04 // lea ecx, [ebx + 4]
    };
    cave.emit(release);
    cave.call(UNICODE_STRING_RELEASE);
    const size_t formatted = cave.size;
    cave.jmp(0);
    const size_t dropKills = cave.size;
    cave.emit8(0x58); // pop eax: the kills the fetch left on the stack
    const size_t freeLocalsAt = cave.size;
    const BYTE freeLocals[3] = {0x83, 0xC4, 0x08}; // add esp, 8
    cave.emit(freeLocals);
    const size_t done = cave.size;
    cave.emit8(0x61); // popad
    cave.jmp(UNICODE_STRING_RELEASE);

    cave.resolveShortJump(noObject, done);
    cave.resolveShortJump(noKills, done);
    cave.resolveJcc(missingString, reinterpret_cast<uintptr_t>(caveMemory) + dropKills);
    cave.resolveJmp(formatted, reinterpret_cast<uintptr_t>(caveMemory) + freeLocalsAt);

    static PatchSet set;
    set.reset();

    // inc [esi + 0x490] / mov eax, [esi + 0x27C] / inc [esi + 0x48C]: the killer's kills, then its battalion's
    const BYTE killScorer[18] = {0xFF, 0x86, 0x90, 0x04, 0x00, 0x00, 0x8B, 0x86, 0x7C, 0x02, 0x00, 0x00, 0xFF, 0x86, 0x8C, 0x04, 0x00, 0x00};
    set.expect(KILL_SCORER_COUNT, killScorer);
    // test eax, eax / je / mov ecx, [eax + 4] / test byte [ecx + 0x115], 0x20 (KindOf HORDE)
    const BYTE battalionTest[14] = {0x85, 0xC0, 0x74, 0x18, 0x8B, 0x48, 0x04, 0xF6, 0x81, 0x15, 0x01, 0x00, 0x00, 0x20};
    set.expect(KILL_SCORER_BATTALION_TEST, battalionTest);

    // mov eax, [esi + 0x488] / cmp eax, ebx / pop edi / jle / mov [ebp - 0x2C], ebx / mov ecx, [THE_GAME_TEXT] / push eax /
    // mov eax, [ecx] / push ebx
    const BYTE battlesFought[24] = {
        0x8B, 0x86, 0x88, 0x04, 0x00, 0x00, 0x3B, 0xC3, 0x5F, 0x7E, 0x3F, 0x89, 0x5D, 0xD4, 0x8B, 0x0D, 0x04, 0x4B, 0xDE, 0x00,
        0x50, 0x8B, 0x01, 0x53
    };
    set.expect(BATTLES_FOUGHT_LINE, battlesFought);
    // push "TOOLTIP:BattlesFought" / mov byte [ebp - 4], 0xB / call [eax + 0x44] / push eax / lea eax, [ebp - 0x2C] / push eax /
    // call UNICODE_STRING_FORMAT / ... / lea eax, [ebp - 0x1C] / push eax / call ADD_LINE
    const BYTE battlesFoughtLine[24] = {
        0x68, 0x00, 0x92, 0xC7, 0x00, 0xC6, 0x45, 0xFC, 0x0B, 0xFF, 0x50, 0x44, 0x50, 0x8D, 0x45, 0xD4, 0x50, 0xE8, 0x60, 0x37,
        0x1F, 0x00, 0x8D, 0x45
    };
    set.expect(BATTLES_FOUGHT_FETCH, battlesFoughtLine);

    const BYTE descriptionRelease[5] = {0xE8, 0x8B, 0xA6, 0xB4, 0xFF};
    set.redirectCall(DESCRIPTION_RELEASE, descriptionRelease, reinterpret_cast<uintptr_t>(caveMemory) + CODE_OFFSET);

    // mov ecx, [esp + 4] / call / test al, al
    const BYTE addLineHead[11] = {0x8B, 0x4C, 0x24, 0x04, 0xE8, 0x4C, 0x94, 0xB4, 0xFF, 0x84, 0xC0};
    set.expect(ADD_LINE, addLineHead);
    // mov eax, [esp + 8] / mov eax, [eax]: the format
    const BYTE formatHead[6] = {0x8B, 0x44, 0x24, 0x08, 0x8B, 0x00};
    set.expect(UNICODE_STRING_FORMAT, formatHead);
    const BYTE releaseHead[7] = {0x6A, 0xFF, 0x68, 0xF8, 0x0E, 0xB7, 0x00};
    set.expect(UNICODE_STRING_RELEASE, releaseHead);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
