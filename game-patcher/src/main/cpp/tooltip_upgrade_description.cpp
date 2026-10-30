#include <windows.h>
#include <cstdint>

#include "patch_set.h"
#include "tooltip_upgrade_description.h"

// Tooltip upgrade description
//
// Problem: once an upgrade is researched, the tooltip of a button without its own PurchasedLabel shows only the generic
// "already researched" text and loses the upgrade's description.
// Fix: for such buttons the description stays, and the "already researched" line is added below it.
//
// The control bar builds a button's description into a UnicodeString at [ebp - 0x18] of the tooltip builder. For an upgrade
// the player already holds, it fetches the button's PurchasedLabel, or TOOLTIP:AlreadyUpgradedDefault when the button has none
// (0x0080833B), and assigns it over the description (0x00808375, UnicodeString::operator=). The hook appends the default text
// instead (UnicodeString::concat, 0x004065FE), after a newline when the description is not empty. A PurchasedLabel is still
// assigned as before: in 2.02 it repeats the button's description followed by the purchased note, so appending it would show
// the description twice. Only the client's tooltip text changes. Derived from pySAGE's upgrade-description
// (https://github.com/ClementJ18/pySAGE).

using namespace patching;

namespace {

    constexpr uintptr_t PURCHASED_LABEL_TEST = 0x0080833B;
    constexpr uintptr_t PURCHASED_FETCH = 0x00808364;
    // thiscall(string): whether an AsciiString is empty; changes only eax
    constexpr uintptr_t ASCII_STRING_IS_EMPTY = 0x00401E64;
    constexpr uintptr_t PURCHASED_ASSIGN = 0x00808371;
    constexpr uintptr_t PURCHASED_RESUME = 0x0080837A;
    // thiscall(string, const UnicodeString &), ret 4
    constexpr uintptr_t UNICODE_STRING_ASSIGN = 0x00436A90;
    constexpr uintptr_t UNICODE_STRING_APPEND = 0x004065FE;
    // thiscall(string, const wchar_t *), ret 4
    constexpr uintptr_t UNICODE_STRING_APPEND_WIDE = 0x00405183;
    // L"\n"
    constexpr uintptr_t WIDE_NEWLINE = 0x00BDBC40;
}

bool applyTooltipUpgradeDescription() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    // eax is the fetched message, esi the button's PurchasedLabel, ebp the builder's frame with the description at [ebp - 0x18]
    Cave cave(caveMemory);
    const BYTE purchasedLabel[3] = {0x50, 0x8B, 0xCE}; // push eax / mov ecx, esi
    cave.emit(purchasedLabel);
    cave.call(ASCII_STRING_IS_EMPTY);
    const BYTE noPurchasedLabel[5] = {
        0x84, 0xC0, // test al, al
        0x58, // pop eax
        0x75, 0x0E // jne defaultText
    };
    cave.emit(noPurchasedLabel);
    const BYTE assignLabel[4] = {0x8D, 0x4D, 0xE8, 0x50}; // lea ecx, [ebp - 0x18] / push eax
    cave.emit(assignLabel);
    cave.call(UNICODE_STRING_ASSIGN);
    cave.jmp(PURCHASED_RESUME);
    const BYTE emptyDescription[15] = { // defaultText:
        0x50, // push eax: the message, for the append below
        0x8B, 0x45, 0xE8, // mov eax, [ebp - 0x18]
        0x85, 0xC0, // test eax, eax
        0x74, 0x14, // je append
        0x66, 0x83, 0x78, 0x04, 0x00, // cmp word [eax + 4], 0: the description's length
        0x74, 0x0D // je append
    };
    cave.emit(emptyDescription);
    const BYTE newline[8] = {0x68, 0x40, 0xBC, 0xBD, 0x00, 0x8D, 0x4D, 0xE8}; // push L"\n" / lea ecx, [ebp - 0x18]
    cave.emit(newline);
    cave.call(UNICODE_STRING_APPEND_WIDE);
    const BYTE description[3] = {0x8D, 0x4D, 0xE8}; // append: lea ecx, [ebp - 0x18]
    cave.emit(description);
    cave.call(UNICODE_STRING_APPEND);
    cave.jmp(PURCHASED_RESUME);

    static PatchSet set;
    set.reset();

    // mov esi, [esi + 0xC] / add esi, 0x70 / mov ecx, esi / call ASCII_STRING_IS_EMPTY: the button's PurchasedLabel
    const BYTE purchasedLabelTest[13] = {0x8B, 0x76, 0x0C, 0x83, 0xC6, 0x70, 0x8B, 0xCE, 0xE8, 0x1C, 0x9B, 0xBF, 0xFF};
    set.expect(PURCHASED_LABEL_TEST, purchasedLabelTest);
    // ... / jne default / push esi / push edx / call [eax + 0x38]: fetch the label, and esi stays the label
    const BYTE purchasedLabelFetch[11] = {0x8D, 0x55, 0xB4, 0x6A, 0x00, 0x75, 0x0B, 0x56, 0x52, 0xFF, 0x50};
    set.expect(PURCHASED_LABEL_TEST + 0x17, purchasedLabelFetch);

    // push "TOOLTIP:AlreadyUpgradedDefault"
    const BYTE purchasedFetch[5] = {0x68, 0xE4, 0xEE, 0xC4, 0x00};
    set.expect(PURCHASED_FETCH, purchasedFetch);

    // mov eax, [ecx] / test eax, eax / je / cmp word [eax + 4], 0
    const BYTE isEmpty[11] = {0x8B, 0x01, 0x85, 0xC0, 0x74, 0x0A, 0x66, 0x83, 0x78, 0x04, 0x00};
    set.expect(ASCII_STRING_IS_EMPTY, isEmpty);

    // lea ecx, [ebp - 0x18] / push eax / call UNICODE_STRING_ASSIGN
    const BYTE assign[9] = {0x8D, 0x4D, 0xE8, 0x50, 0xE8, 0x16, 0xE7, 0xC2, 0xFF};
    set.hook(PURCHASED_ASSIGN, assign, reinterpret_cast<uintptr_t>(caveMemory));

    // lea ecx, [ebp - 0x4C] / mov byte [ebp - 4], 6 / call: the message's destructor
    const BYTE resume[7] = {0x8D, 0x4D, 0xB4, 0xC6, 0x45, 0xFC, 0x06};
    set.expect(PURCHASED_RESUME, resume);

    const BYTE assignHead[4] = {0x6A, 0xFF, 0x68, 0xF8};
    set.expect(UNICODE_STRING_ASSIGN, assignHead);
    // mov eax, [esp + 4] / mov eax, [eax] / test eax, eax
    const BYTE appendHead[8] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x00, 0x85, 0xC0};
    set.expect(UNICODE_STRING_APPEND, appendHead);
    // push esi / mov esi, [esp + 8] / test esi, esi / push edi / mov edi, ecx
    const BYTE appendWideHead[10] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x57, 0x8B, 0xF9};
    set.expect(UNICODE_STRING_APPEND_WIDE, appendWideHead);

    const BYTE newlineText[4] = {0x0A, 0x00, 0x00, 0x00};
    set.expect(WIDE_NEWLINE, newlineText);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
