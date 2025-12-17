package de.darkatra.bfme2.util

import com.sun.jna.Native
import com.sun.jna.platform.win32.WinDef.DWORD
import com.sun.jna.win32.W32APIOptions

interface Kernel32 : com.sun.jna.platform.win32.Kernel32 {

    /**
     * See: https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocessstop
     *
     * BOOL DebugActiveProcessStop(
     *   [in] DWORD dwProcessId
     * );
     */
    @Suppress("FunctionName")
    fun DebugActiveProcessStop(
        dwProcessId: DWORD
    ): Boolean

    companion object {
        val INSTANCE: Kernel32 = Native.load("kernel32", Kernel32::class.java, W32APIOptions.DEFAULT_OPTIONS)
    }
}
