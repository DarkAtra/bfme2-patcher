package de.darkatra.bfme2.util

import com.sun.jna.Native
import com.sun.jna.Pointer
import com.sun.jna.Structure.*
import com.sun.jna.platform.win32.WinDef.DWORD
import com.sun.jna.platform.win32.WinDef.HMODULE
import com.sun.jna.platform.win32.WinNT.HANDLE
import com.sun.jna.win32.W32APIOptions

interface Kernel32 : com.sun.jna.platform.win32.Kernel32 {

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocessstop
     *
     * BOOL DebugActiveProcessStop(
     *   [in] DWORD dwProcessId
     * );
     */
    fun DebugActiveProcessStop(
        dwProcessId: DWORD
    ): Boolean

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress
     *
     * FARPROC GetProcAddress(
     *   [in] HMODULE hModule,
     *   [in] LPCSTR  lpProcName
     * );
     */
    fun GetProcAddress(
        hModule: HMODULE,
        lpProcName: Pointer
    ): Pointer?

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadid
     *
     * DWORD GetThreadId(
     *   [in] HANDLE Thread
     * );
     */
    fun GetThreadId(
        thread: HANDLE
    ): DWORD

    companion object {
        val INSTANCE: Kernel32 = Native.load("kernel32", Kernel32::class.java, W32APIOptions.DEFAULT_OPTIONS)
    }
}
