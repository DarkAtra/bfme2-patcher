#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game_memory.h"
#include "instrumentation/auto_skirmish.h"
#include "patch_set.h"

// Auto skirmish (debug mode, with -autoskirmish on the game's command line; launch-test.ps1 -AutoSkirmish adds -file <map>)
//
// Starts the skirmish set up last without the menus, through the engine's own command line start. Derived from pySAGE's
// command-line-skirmish (https://github.com/ClementJ18/pySAGE, sage_patch/docs/game-info.md and headless.md, section 3d): the
// stock -file <map> (handler 0x007BACA4) makes GameEngine::init build a skirmish GameInfo (TheSkirmishGameInfo, 0x00DE8930) and
// send MSG_NEW_GAME itself (0x0063CA19 to 0x0063CBAE), and the shell then shows no menu (0x0075E43C). Stock, that game has one seat
// with a random faction and no TheGameInfo, and crashes. Right before the auto-start appends the game mode (0x0063CB7B), a hook
// fills the GameInfo with the lobby the main menu's Skirmish button opens: the skirmish screen's own preferences (constructor
// 0x008221F5 with index 0, which AptMainMenu::update passes for that button at 0x0091C41E) read Skirmish.ini and hand its
// ":0:<user>:GameInfo" line to the lobby parser (0x00821C29, ParseAsciiStringToGameInfo 0x00802DBA). As pySAGE does, it then puts
// back the map -file chose (path, CRC and size from the map cache) and the auto-start's seed, binds every seat with a start position
// to the map's "Player_<start position + 1>" (the parser leaves + 0x34 empty) and points TheGameInfo (0x00DE892C) at it; as the
// skirmish screen's start does (0x009287D9), it calls the GameInfo's startGame(0) (vtable + 0x2C) and seeds the logic's random
// generator from it (InitGameLogicRandom, 0x006D3261). The loading screen's progress update (0x0081C64A) dereferences a window that
// a start without menus never creates: it is skipped while that window is missing.
// Random colours, factions or start positions are left as the lobby has them (the skirmish screen resolves them); a lobby the parser
// refuses leaves the stock game.

using namespace patching;

namespace {

    constexpr uintptr_t AUTO_START_MODE = 0x0063CB7B;
    constexpr uintptr_t AUTO_START_RESUME = 0x0063CB84;
    constexpr uintptr_t APPEND_INTEGER = 0x007111E5;
    constexpr uintptr_t LOADING_PROGRESS = 0x0081C64A;
    constexpr uintptr_t LOADING_PROGRESS_RESUME = 0x0081C662;
    constexpr uintptr_t LOADING_PROGRESS_REPORT = 0x0062648E;
    constexpr uintptr_t THE_GAME_INFO = 0x00DE892C;
    constexpr uintptr_t THE_SKIRMISH_GAME_INFO = 0x00DE8930;
    constexpr uintptr_t PREFERENCES_CONSTRUCTOR = 0x008221F5;
    constexpr uintptr_t PREFERENCES_INDEX_STORE = 0x00822221;
    constexpr uintptr_t PREFERENCES_LOAD_GAME_INFO = 0x00821C29;
    constexpr uintptr_t PREFERENCES_DESTRUCTOR = 0x00822353;
    constexpr uintptr_t STRING_COPY = 0x00435F30;
    constexpr uintptr_t STRING_RELEASE = 0x00435D50;
    constexpr uintptr_t STRING_SET = 0x004050E6;
    constexpr uintptr_t SET_MAP = 0x00801C46;
    constexpr uintptr_t SET_MAP_CRC = 0x0080298E;
    constexpr uintptr_t SET_MAP_SIZE = 0x00802A49;
    constexpr uintptr_t SET_SEED = 0x00955CAB;
    constexpr uintptr_t INIT_GAME_LOGIC_RANDOM = 0x006D3261;

    constexpr uint32_t GAME_INFO_SLOTS = 0x18;
    constexpr uint32_t GAME_INFO_MAP = 0x40;
    constexpr uint32_t GAME_INFO_MAP_CRC = 0x44;
    constexpr uint32_t GAME_INFO_MAP_SIZE = 0x48;
    constexpr uint32_t GAME_INFO_SEED = 0x50;
    constexpr uint32_t START_GAME_SLOT = 0x2C;
    constexpr uint32_t SLOT_STATE = 0x04;
    constexpr uint32_t SLOT_START_POSITION = 0x10;
    constexpr uint32_t SLOT_MAP_PLAYER = 0x34;
    constexpr int SLOT_COUNT = 8;
    constexpr int STATE_EASY_AI = 2;
    constexpr int STATE_LOCAL_HUMAN = 6;
    constexpr int MAIN_MENU_LOBBY = 0;
    // the skirmish screen holds its preferences in 0x20 bytes (AptSkirmish + 0x698, its next field at + 0x6B8)
    constexpr size_t PREFERENCES_BYTES = 0x20;

    typedef void (__fastcall *StringCopy)(void *string, void *, const void *source);
    typedef void (__fastcall *StringRelease)(void *string, void *);
    typedef void (__fastcall *StringSet)(void *string, void *, const char *text);
    // the AsciiString arrives by value, and the callee releases it
    typedef void (__fastcall *SetMap)(void *gameInfo, void *, void *map);
    typedef void (__fastcall *SetInt)(void *gameInfo, void *, int value);
    typedef void (__fastcall *PreferencesConstructor)(void *preferences, void *, int index);
    typedef bool (__fastcall *LoadGameInfo)(void *preferences, void *);
    typedef void (__fastcall *Destructor)(void *object, void *);
    typedef void (__cdecl *InitRandom)(int seed);

    alignas(4) BYTE preferences[PREFERENCES_BYTES];

    void bindMapPlayers(BYTE *gameInfo) {
        for (int index = 0; index < SLOT_COUNT; index++) {
            BYTE *slot = field<BYTE *>(gameInfo, GAME_INFO_SLOTS + index * 4);
            if (slot == nullptr) {
                continue;
            }
            const int state = field<int>(slot, SLOT_STATE);
            const int startPosition = field<int>(slot, SLOT_START_POSITION);
            if (state < STATE_EASY_AI || state > STATE_LOCAL_HUMAN || startPosition < 0 || startPosition >= SLOT_COUNT) {
                continue;
            }
            char name[16];
            sprintf_s(name, "Player_%d", startPosition + 1);
            reinterpret_cast<StringSet>(STRING_SET)(slot + SLOT_MAP_PLAYER, nullptr, name);
        }
    }

    void __cdecl fillGameInfo() {
        BYTE *gameInfo = read<BYTE *>(THE_SKIRMISH_GAME_INFO);
        if (gameInfo == nullptr) {
            return;
        }
        void *map = nullptr;
        reinterpret_cast<StringCopy>(STRING_COPY)(&map, nullptr, gameInfo + GAME_INFO_MAP);
        const int mapCrc = field<int>(gameInfo, GAME_INFO_MAP_CRC);
        const int mapSize = field<int>(gameInfo, GAME_INFO_MAP_SIZE);
        const int seed = field<int>(gameInfo, GAME_INFO_SEED);

        memset(preferences, 0, sizeof(preferences));
        reinterpret_cast<PreferencesConstructor>(PREFERENCES_CONSTRUCTOR)(preferences, nullptr, MAIN_MENU_LOBBY);
        const bool loaded = reinterpret_cast<LoadGameInfo>(PREFERENCES_LOAD_GAME_INFO)(preferences, nullptr);
        reinterpret_cast<Destructor>(PREFERENCES_DESTRUCTOR)(preferences, nullptr);
        if (!loaded) {
            reinterpret_cast<StringRelease>(STRING_RELEASE)(&map, nullptr);
            return;
        }

        reinterpret_cast<SetMap>(SET_MAP)(gameInfo, nullptr, map);
        reinterpret_cast<SetInt>(SET_MAP_CRC)(gameInfo, nullptr, mapCrc);
        reinterpret_cast<SetInt>(SET_MAP_SIZE)(gameInfo, nullptr, mapSize);
        reinterpret_cast<SetInt>(SET_SEED)(gameInfo, nullptr, seed);
        bindMapPlayers(gameInfo);
        *reinterpret_cast<BYTE **>(THE_GAME_INFO) = gameInfo;
        slot<SetInt>(gameInfo, START_GAME_SLOT)(gameInfo, nullptr, 0);
        reinterpret_cast<InitRandom>(INIT_GAME_LOGIC_RANDOM)(field<int>(gameInfo, GAME_INFO_SEED));
    }
}

bool instrumentation::applyAutoSkirmish() {
    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }
    Cave cave(caveMemory);

    static PatchSet set;
    set.reset();

    // pushad / pushfd / call fillGameInfo / popfd / popad, then the displaced push 2 / mov ecx, edi / call appendIntegerArgument
    const uintptr_t autoStart = cave.here();
    const BYTE save[2] = {0x60, 0x9C};
    cave.emit(save);
    cave.call(reinterpret_cast<uintptr_t>(&fillGameInfo));
    const BYTE restore[2] = {0x9D, 0x61};
    cave.emit(restore);
    const BYTE modeArgument[4] = {0x6A, 0x02, 0x8B, 0xCF};
    cave.emit(modeArgument);
    cave.call(APPEND_INTEGER);
    cave.jmp(AUTO_START_RESUME);
    // push 2 / mov ecx, edi / call appendIntegerArgument: game mode 2, skirmish
    const BYTE autoStartMode[9] = {0x6A, 0x02, 0x8B, 0xCF, 0xE8, 0x61, 0x46, 0x0D, 0x00};
    set.hook(AUTO_START_MODE, autoStartMode, autoStart);
    // lea ecx, [ebp - 0x224]: the auto-start goes on there
    const BYTE autoStartResume[6] = {0x8D, 0x8D, 0xDC, 0xFD, 0xFF, 0xFF};
    set.expect(AUTO_START_RESUME, autoStartResume);

    // mov ecx, [esi + 0x88] (the progress window) / test ecx, ecx / je resume, then the displaced update
    const uintptr_t progress = cave.here();
    const BYTE window[8] = {0x8B, 0x8E, 0x88, 0x00, 0x00, 0x00, 0x85, 0xC9};
    cave.emit(window);
    const size_t noWindow = cave.size;
    const BYTE skip[2] = {0x74, 0x00};
    cave.emit(skip);
    // mov eax, [ecx] / push edi / call [eax + 0x34] / mov ecx, [0x00DE412C] / push eax / call 0x0062648E
    const BYTE update[13] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x34, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x50};
    cave.emit(update);
    cave.call(LOADING_PROGRESS_REPORT);
    cave.resolveShortJump(noWindow, cave.size);
    cave.jmp(LOADING_PROGRESS_RESUME);
    const BYTE loadingProgress[24] = {
        0x8B, 0x8E, 0x88, 0x00, 0x00, 0x00, 0x8B, 0x01, 0x57, 0xFF, 0x50, 0x34, 0x8B, 0x0D, 0x2C, 0x41, 0xDE, 0x00, 0x50, 0xE8, 0x2C, 0x9E,
        0xE0, 0xFF
    };
    set.hook(LOADING_PROGRESS, loadingProgress, progress);

    // the functions fillGameInfo calls, by their first instructions
    const BYTE stringCopy[4] = {0x56, 0x57, 0x8B, 0xF9};
    set.expect(STRING_COPY, stringCopy);
    const BYTE stringRelease[7] = {0x6A, 0xFF, 0x68, 0xF8, 0x0E, 0xB7, 0x00};
    set.expect(STRING_RELEASE, stringRelease);
    const BYTE stringSet[7] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6};
    set.expect(STRING_SET, stringSet);
    const BYTE setMap[5] = {0xB8, 0xA9, 0x8E, 0xB9, 0x00};
    set.expect(SET_MAP, setMap);
    const BYTE setMapCrc[10] = {0xB8, 0x04, 0x90, 0xB9, 0x00, 0xE8, 0x58, 0xA5, 0x23, 0x00};
    set.expect(SET_MAP_CRC, setMapCrc);
    const BYTE setMapSize[10] = {0xB8, 0x04, 0x90, 0xB9, 0x00, 0xE8, 0x9D, 0xA4, 0x23, 0x00};
    set.expect(SET_MAP_SIZE, setMapSize);
    // mov eax, [esp + 4] / mov [ecx + 0x50], eax / ret 4
    const BYTE setSeed[10] = {0x8B, 0x44, 0x24, 0x04, 0x89, 0x41, 0x50, 0xC2, 0x04, 0x00};
    set.expect(SET_SEED, setSeed);
    const BYTE preferencesConstructor[5] = {0xB8, 0x7A, 0xB0, 0xB9, 0x00};
    set.expect(PREFERENCES_CONSTRUCTOR, preferencesConstructor);
    // mov [esi + 0x14], eax: the lobby's index
    const BYTE preferencesIndex[3] = {0x89, 0x46, 0x14};
    set.expect(PREFERENCES_INDEX_STORE, preferencesIndex);
    // mov eax, the exception frame's handler / call / sub esp, 0x14 / cmp dword [TheSkirmishGameInfo], 0
    const BYTE loadGameInfo[20] = {
        0xB8, 0xB8, 0xAF, 0xB9, 0x00, 0xE8, 0xBD, 0xB2, 0x21, 0x00, 0x83, 0xEC, 0x14, 0x83, 0x3D, 0x30, 0x89, 0xDE, 0x00, 0x00
    };
    set.expect(PREFERENCES_LOAD_GAME_INFO, loadGameInfo);
    const BYTE preferencesDestructor[5] = {0xB8, 0x97, 0xB0, 0xB9, 0x00};
    set.expect(PREFERENCES_DESTRUCTOR, preferencesDestructor);
    // mov eax, [GlobalData] / test eax, eax
    const BYTE initGameLogicRandom[7] = {0xA1, 0x64, 0x43, 0xDE, 0x00, 0x85, 0xC0};
    set.expect(INIT_GAME_LOGIC_RANDOM, initGameLogicRandom);
    // push esi / mov esi, ecx
    const BYTE progressReport[3] = {0x56, 0x8B, 0xF1};
    set.expect(LOADING_PROGRESS_REPORT, progressReport);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
