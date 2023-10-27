package de.darkatra.bfme2.util

import com.sun.jna.Native
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder
import com.sun.jna.platform.win32.WinDef.HMODULE
import com.sun.jna.platform.win32.WinNT.HANDLE
import com.sun.jna.ptr.IntByReference
import com.sun.jna.win32.StdCallLibrary
import com.sun.jna.win32.W32APIOptions

interface Psapi : StdCallLibrary {

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocessmodulesex
     *
     * BOOL EnumProcessModulesEx(
     *   [in]  HANDLE  hProcess,
     *   [out] HMODULE *lphModule,
     *   [in]  DWORD   cb,
     *   [out] LPDWORD lpcbNeeded,
     *   [in]  DWORD   dwFilterFlag
     * );
     */
    fun EnumProcessModulesEx(
        hProcess: HANDLE,
        lphModule: Array<HMODULE?>,
        cb: Int,
        lpcbNeeded: IntByReference,
        dwFilterFlag: Int
    ): Boolean

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getmodulebasenamea
     *
     * DWORD GetModuleBaseNameA(
     *   [in]           HANDLE  hProcess,
     *   [in, optional] HMODULE hModule,
     *   [out]          LPSTR   lpBaseName,
     *   [in]           DWORD   nSize
     * );
     */
    fun GetModuleBaseNameA(
        hProcess: HANDLE,
        hModule: HMODULE,
        lpImageFileName: ByteArray,
        nSize: Int
    ): Int

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getmoduleinformation
     *
     * BOOL GetModuleInformation(
     *   [in]  HANDLE       hProcess,
     *   [in]  HMODULE      hModule,
     *   [out] LPMODULEINFO lpmodinfo,
     *   [in]  DWORD        cb
     * );
     */
    fun GetModuleInformation(
        hProcess: HANDLE,
        hModule: HMODULE,
        lpmodinfo: LPMODULEINFO,
        cb: Int
    ): Boolean

    companion object {

        const val LIST_MODULES_DEFAULT = 0x00
        const val LIST_MODULES_32BIT = 0x01
        const val LIST_MODULES_64BIT = 0x02
        const val LIST_MODULES_ALL = 0x03

        val INSTANCE: Psapi = Native.load("psapi", Psapi::class.java, W32APIOptions.DEFAULT_OPTIONS)
    }
}

/**
 * See: https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-moduleinfo
 */
@FieldOrder("lpBaseOfDll", "SizeOfImage", "EntryPoint")
class LPMODULEINFO : Structure() {
    @JvmField
    var lpBaseOfDll: HANDLE? = null

    @JvmField
    var SizeOfImage = 0

    @JvmField
    var EntryPoint: HANDLE? = null
}
