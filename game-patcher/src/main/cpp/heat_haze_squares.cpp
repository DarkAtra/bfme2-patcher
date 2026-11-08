#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstring>

#include "heat_haze_squares.h"
#include "patch_set.h"

// Heat haze squares
//
// Problem: heat haze (particle systems with ParticleName = SMUDGE.tga, e.g. the Balrog summon and many fire effects) shows as
// square glitches on a grid. While smudges exist, the frame is drawn into a texture of the back buffer's size (0x00474DC9; the
// smudge filter's pre-render 0x004FD5E8 makes it the render target with 0x004754C3), which the filter's post-render (0x004FD613)
// puts on the screen with 0x00475031. The smudges themselves are drawn in the middle of the scene (0x00470F44 calls 0x0044C3FE,
// W3DSmudgeManager's render 0x004A67FA): into that texture while sampling it. Direct3D 9 leaves reading a texture that is being
// rendered to undefined, and current graphics cards show it as blocks. Also, the quads take their corners' texture coordinates
// from the projected corners, (x + 1) / 2 and (1 - y) / 2 times view size / texture size, without the half texel Direct3D 9 needs,
// while 0x00475031 draws at pixel - 0.5 so that every screen pixel reads its own texel: a quad samples half a texel off.
// Fix: the render gets a copy of the frame drawn so far to sample (its call of the render texture getter 0x00475721 at
// 0x004A68D6 returns the copy whenever the render target is the render texture, StretchRect into a texture of the same size and
// format), and the smudge vertices get half a texel added to both texture coordinates when they are copied into the vertex buffer.
// The copy lives in the default pool, so it is released with the render texture (0x0047603D, called at 0x004760F9 when the shader
// manager releases its resources, e.g. before a device reset) and made again on the next use.
//
// The texture's size comes from its GetLevelDesc (D3DSURFACE_DESC at [ebp - 0x11C], Width at +0x18, Height at +0x1C), which the
// render reads into esi and edi at 0x004A690A; the hook there also keeps 0.5 / width and 0.5 / height. The copy loop
// (0x004A6FE1, esi the vertex buffer entry of 0x2C bytes, edi the smudge's vertex of 0x14 bytes) copies u (+0x0C) and v (+0x10)
// at 0x004A700A. xmm1 and xmm2 are free at both places: the render uses no SSE register but a zeroed xmm0 after 0x004A6E80, and
// at 0x004A690A it loads both again before their next use (0x004A6A6B), behind a call.

using namespace patching;

namespace {

    constexpr uintptr_t TEXTURE_SIZE = 0x004A690A;
    constexpr uintptr_t TEXTURE_SIZE_RESUME = 0x004A6916;
    constexpr uintptr_t UV_COPY = 0x004A700A;
    constexpr uintptr_t UV_COPY_RESUME = 0x004A7016;
    constexpr uintptr_t RENDER_TEXTURE_CALL = 0x004A68D6;
    constexpr uintptr_t RENDER_TEXTURE_GETTER = 0x00475721;
    constexpr uintptr_t RELEASE_CALL = 0x004760F9;
    constexpr uintptr_t RELEASE = 0x0047603D;
    constexpr uintptr_t THE_DEVICE = 0x00DD3474;
    constexpr uintptr_t RENDER_TEXTURE = 0x00DC79A8;

    IDirect3DTexture9 *copy = nullptr;
    IDirect3DSurface9 *copySurface = nullptr;
    D3DSURFACE_DESC copyDesc{};

    void releaseCopy() {
        if (copySurface != nullptr) {
            copySurface->Release();
            copySurface = nullptr;
        }
        if (copy != nullptr) {
            copy->Release();
            copy = nullptr;
        }
    }

    void __cdecl releaseResources() {
        releaseCopy();
        reinterpret_cast<void (__cdecl *)()>(RELEASE)();
    }

    bool makeCopy(IDirect3DDevice9 *device, const D3DSURFACE_DESC &desc) {
        if (copy != nullptr && copyDesc.Width == desc.Width && copyDesc.Height == desc.Height && copyDesc.Format == desc.Format) {
            return true;
        }
        releaseCopy();
        if (FAILED(device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &copy, nullptr))) {
            copy = nullptr;
            return false;
        }
        if (FAILED(copy->GetSurfaceLevel(0, &copySurface))) {
            copySurface = nullptr;
            releaseCopy();
            return false;
        }
        copyDesc = desc;
        return true;
    }

    IDirect3DTexture9 *__cdecl smudgeSource() {
        const auto original = *reinterpret_cast<IDirect3DTexture9 **>(RENDER_TEXTURE);
        const auto device = *reinterpret_cast<IDirect3DDevice9 **>(THE_DEVICE);
        if (original == nullptr || device == nullptr) {
            return original;
        }
        IDirect3DSurface9 *target = nullptr;
        if (FAILED(device->GetRenderTarget(0, &target)) || target == nullptr) {
            return original;
        }
        IDirect3DTexture9 *result = original;
        IDirect3DSurface9 *level = nullptr;
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(original->GetSurfaceLevel(0, &level)) && level == target && SUCCEEDED(original->GetLevelDesc(0, &desc)) &&
            makeCopy(device, desc) && SUCCEEDED(device->StretchRect(target, nullptr, copySurface, nullptr, D3DTEXF_NONE))) {
            result = copy;
        }
        if (level != nullptr) {
            level->Release();
        }
        target->Release();
        return result;
    }
}

bool applyHeatHazeSquares() {

    BYTE *caveMemory = allocateCave();
    if (caveMemory == nullptr) {
        return false;
    }

    Cave cave(caveMemory);
    const uintptr_t half = cave.here();
    const float halfValue = 0.5f;
    uint32_t halfBits;
    memcpy(&halfBits, &halfValue, sizeof(halfBits));
    cave.emit32(halfBits);
    const uintptr_t texelU = cave.here();
    cave.emit32(0);
    const uintptr_t texelV = cave.here();
    cave.emit32(0);

    const uintptr_t textureSize = cave.here();
    const BYTE sizes[12] = {
        0x8B, 0xB5, 0xFC, 0xFE, 0xFF, 0xFF, // mov esi, [ebp - 0x104]: Width
        0x8B, 0xBD, 0x00, 0xFF, 0xFF, 0xFF // mov edi, [ebp - 0x100]: Height
    };
    cave.emit(sizes);
    const BYTE widthToFloat[4] = {0xF3, 0x0F, 0x2A, 0xCE}; // cvtsi2ss xmm1, esi
    const BYTE heightToFloat[4] = {0xF3, 0x0F, 0x2A, 0xCF}; // cvtsi2ss xmm1, edi
    const BYTE loadHalf[4] = {0xF3, 0x0F, 0x10, 0x15}; // movss xmm2, [half]
    const BYTE divide[4] = {0xF3, 0x0F, 0x5E, 0xD1}; // divss xmm2, xmm1
    const BYTE store[4] = {0xF3, 0x0F, 0x11, 0x15}; // movss [texel], xmm2
    cave.emit(widthToFloat);
    cave.emit(loadHalf);
    cave.emit32(static_cast<uint32_t>(half));
    cave.emit(divide);
    cave.emit(store);
    cave.emit32(static_cast<uint32_t>(texelU));
    cave.emit(heightToFloat);
    cave.emit(loadHalf);
    cave.emit32(static_cast<uint32_t>(half));
    cave.emit(divide);
    cave.emit(store);
    cave.emit32(static_cast<uint32_t>(texelV));
    cave.jmp(TEXTURE_SIZE_RESUME);

    const uintptr_t uvCopy = cave.here();
    const BYTE loadU[5] = {0xF3, 0x0F, 0x10, 0x4F, 0x0C}; // movss xmm1, [edi + 0x0C]
    const BYTE loadV[5] = {0xF3, 0x0F, 0x10, 0x4F, 0x10}; // movss xmm1, [edi + 0x10]
    const BYTE addTexel[4] = {0xF3, 0x0F, 0x58, 0x0D}; // addss xmm1, [texel]
    const BYTE storeU[5] = {0xF3, 0x0F, 0x11, 0x4E, 0x1C}; // movss [esi + 0x1C], xmm1
    const BYTE storeV[5] = {0xF3, 0x0F, 0x11, 0x4E, 0x20}; // movss [esi + 0x20], xmm1
    const BYTE lastLoad[3] = {0x8B, 0x4F, 0x10}; // mov ecx, [edi + 0x10]: ecx as the copy leaves it
    cave.emit(loadU);
    cave.emit(addTexel);
    cave.emit32(static_cast<uint32_t>(texelU));
    cave.emit(storeU);
    cave.emit(loadV);
    cave.emit(addTexel);
    cave.emit32(static_cast<uint32_t>(texelV));
    cave.emit(storeV);
    cave.emit(lastLoad);
    cave.jmp(UV_COPY_RESUME);

    static PatchSet set;
    set.reset();

    // the render texture: mov eax, [0x00DC79A8] / ret, called by the render at 0x004A68D6
    const BYTE getter[6] = {0xA1, 0xA8, 0x79, 0xDC, 0x00, 0xC3};
    set.expect(RENDER_TEXTURE_GETTER, getter);
    const BYTE getterCall[5] = {0xE8, 0x46, 0xEE, 0xFC, 0xFF};
    set.redirectCall(RENDER_TEXTURE_CALL, getterCall, reinterpret_cast<uintptr_t>(&smudgeSource));
    // xor ebx, ebx / cmp eax, ebx / ... / cmp byte [0x00DC79A0], bl / ... / mov ecx, [eax] / lea edx, [ebp - 0x11C] / push edx /
    // push ebx / push eax / call [ecx + 0x44] (GetLevelDesc) / mov ecx, [TheTacticalView] / mov eax, [ecx]
    const BYTE levelDesc[47] = {
        0x33, 0xDB, 0x3B, 0xC3, 0x89, 0x45, 0xEC, 0x0F, 0x84, 0x90, 0x08, 0x00, 0x00, 0x38, 0x1D, 0xA0, 0x79, 0xDC, 0x00, 0x0F, 0x84, 0x84,
        0x08, 0x00, 0x00, 0x8B, 0x08, 0x8D, 0x95, 0xE4, 0xFE, 0xFF, 0xFF, 0x52, 0x53, 0x50, 0xFF, 0x51, 0x44, 0x8B, 0x0D, 0x7C, 0x44, 0xDE,
        0x00, 0x8B, 0x01};
    set.expectBytes(RENDER_TEXTURE_CALL + 5, levelDesc);

    // the shader manager's release (push esi / push edi / call 0x0047603D), which releases the render texture
    const BYTE releaseHead[2] = {0x56, 0x57};
    set.expect(RELEASE_CALL - 2, releaseHead);
    const BYTE releaseCall[5] = {0xE8, 0x3F, 0xFF, 0xFF, 0xFF};
    set.redirectCall(RELEASE_CALL, releaseCall, reinterpret_cast<uintptr_t>(&releaseResources));
    // mov eax, [0x00DC79A8] / cmp eax, esi / jz / mov ecx, [eax] / push eax / call [ecx + 8] (Release) / mov [0x00DC79A8], esi
    const BYTE releaseTexture[21] = {0xA1, 0xA8, 0x79, 0xDC, 0x00, 0x3B, 0xC6, 0x74, 0x0C, 0x8B, 0x08, 0x50, 0xFF, 0x51, 0x08, 0x89, 0x35, 0xA8,
                                     0x79, 0xDC, 0x00};
    set.expect(0x0047609B, releaseTexture);
    // ... / leave / ret: no arguments
    const BYTE releaseEnd[2] = {0xC9, 0xC3};
    set.expect(0x004760F5, releaseEnd);
    set.hook(TEXTURE_SIZE, sizes, textureSize);
    // call [eax + 0x3C] (the view's width): the call behind which xmm1 and xmm2 are free
    const BYTE viewWidth[3] = {0xFF, 0x50, 0x3C};
    set.expect(TEXTURE_SIZE_RESUME, viewWidth);

    // the copy loop up to u and v: position (+0 .. +8), a zero normal (+0x0C .. +0x14), the colour (+0x18)
    const BYTE copyHead[41] = {
        0x8B, 0x0F, 0x0F, 0x57, 0xC0, 0x89, 0x0E, 0x8B, 0x4F, 0x04, 0x89, 0x4E, 0x04, 0x8B, 0x4F, 0x08, 0x89, 0x4E, 0x08, 0x8B, 0x4C, 0x85,
        0xBC, 0xF3, 0x0F, 0x11, 0x46, 0x0C, 0xF3, 0x0F, 0x11, 0x46, 0x10, 0xF3, 0x0F, 0x11, 0x46, 0x14, 0x89, 0x4E, 0x18};
    set.expectBytes(0x004A6FE1, copyHead);
    // mov ecx, [edi + 0x0C] / mov [esi + 0x1C], ecx / mov ecx, [edi + 0x10] / mov [esi + 0x20], ecx
    const BYTE uv[12] = {0x8B, 0x4F, 0x0C, 0x89, 0x4E, 0x1C, 0x8B, 0x4F, 0x10, 0x89, 0x4E, 0x20};
    set.hook(UV_COPY, uv, uvCopy);
    // movss [esi + 0x24], xmm0 / movss [esi + 0x28], xmm0 / add esi, 0x2C / add edi, 0x14 / inc eax / cmp eax, 5 / jl: five vertices
    const BYTE copyTail[24] = {0xF3, 0x0F, 0x11, 0x46, 0x24, 0xF3, 0x0F, 0x11, 0x46, 0x28, 0x83, 0xC6, 0x2C, 0x83, 0xC7, 0x14, 0x40, 0x83,
                               0xF8, 0x05, 0x7C, 0xB5, 0x8B, 0x45};
    set.expect(UV_COPY_RESUME, copyTail);

    // the device smudgeSource reads, in 0x004754C3 (the render texture made the render target): mov eax, [THE_DEVICE] / cmp eax, ebx /
    // je / mov ecx, [eax] / push eax / call [ecx + 0x0C] (TestCooperativeLevel)
    const BYTE deviceRead[15] = {0xA1, 0x74, 0x34, 0xDD, 0x00, 0x3B, 0xC3, 0x74, 0x0E, 0x8B, 0x08, 0x50, 0xFF, 0x51, 0x0C};
    set.expect(0x004754F0, deviceRead);

    if (!commit(set, cave)) {
        VirtualFree(caveMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}
