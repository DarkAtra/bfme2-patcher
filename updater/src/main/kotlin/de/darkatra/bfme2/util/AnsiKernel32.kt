package de.darkatra.bfme2.util

import com.sun.jna.Native
import com.sun.jna.platform.win32.Kernel32
import com.sun.jna.platform.win32.WinDef.HMODULE
import com.sun.jna.platform.win32.WinDef.LPVOID
import com.sun.jna.win32.W32APIOptions

interface AnsiKernel32 : Kernel32 {

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
        lpProcName: String
    ): LPVOID?

    companion object {
        val INSTANCE: AnsiKernel32 = Native.load("kernel32", AnsiKernel32::class.java, W32APIOptions.ASCII_OPTIONS)
    }
}
