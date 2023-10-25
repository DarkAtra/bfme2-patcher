package de.darkatra.bfme2.util

import com.sun.jna.Memory
import com.sun.jna.platform.win32.BaseTSD
import com.sun.jna.platform.win32.WinNT
import com.sun.jna.platform.win32.WinNT.MEM_COMMIT
import com.sun.jna.platform.win32.WinNT.MEM_RESERVE
import com.sun.jna.platform.win32.WinNT.PAGE_EXECUTE_READWRITE
import com.sun.jna.platform.win32.WinNT.PROCESS_CREATE_THREAD
import com.sun.jna.platform.win32.WinNT.PROCESS_QUERY_INFORMATION
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_OPERATION
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_READ
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_WRITE
import de.darkatra.bfme2.LOGGER
import java.nio.charset.StandardCharsets
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object InjectionUtils {

    fun injectDll(processName: String, dllPath: Path): Boolean {

        // 1. get the process id by name
        val processId = ProcessUtils.findProcessId(processName)
        if (processId == null) {
            LOGGER.severe("Could not find process for name '$processName'")
            return false
        }

        // 2. get the handle to the process
        val handle = Kernel32.INSTANCE.OpenProcess(
            PROCESS_CREATE_THREAD or
                PROCESS_QUERY_INFORMATION or
                PROCESS_VM_OPERATION or
                PROCESS_VM_READ or
                PROCESS_VM_WRITE,
            false,
            Math.toIntExact(processId)
        )
        if (handle == null) {
            LOGGER.severe("Could not OpenProcess with pid '${processId}': ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // 3. get the address of LoadLibraryW
        val loadLibraryPointer = Kernel32.INSTANCE.GetProcAddress(
            Kernel32.INSTANCE.GetModuleHandle("kernel32.dll"),
            970 // ordinal for LoadLibraryW
        )
        if (loadLibraryPointer == null) {
            LOGGER.severe("Failed to get address for LoadLibraryW: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // 4. allocate memory for the dll path string
        val dllPathString = dllPath.absolutePathString()
        val dllPathStringLength = dllPath.absolutePathString().length + 1L
        val dllMemoryPointer = Kernel32.INSTANCE.VirtualAllocEx(
            handle,
            null,
            BaseTSD.SIZE_T(dllPathStringLength),
            MEM_RESERVE or MEM_COMMIT,
            PAGE_EXECUTE_READWRITE
        )
        if (dllMemoryPointer == null) {
            LOGGER.severe("Failed to allocate memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // 5. write the dll path string to the allocated memory
        val successful = Kernel32.INSTANCE.WriteProcessMemory(
            handle,
            dllMemoryPointer,
            Memory(dllPathStringLength).apply {
                setString(0, dllPathString, StandardCharsets.UTF_8.name())
            },
            Math.toIntExact(dllPathStringLength),
            null
        )
        if (!successful) {
            LOGGER.severe("Failed to write to memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // 6. load the dll via remote thread
        val remoteThread = Kernel32.INSTANCE.CreateRemoteThread(
            handle,
            null,
            0,
            loadLibraryPointer,
            dllMemoryPointer,
            WinNT.CREATE_SUSPENDED,
            null
        )
        if (remoteThread == null) {
            LOGGER.severe("Failed to create remote process: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        val remoteThreadId = Kernel32.INSTANCE.GetThreadId(remoteThread).toLong()
        if (remoteThreadId == 0L) {
            LOGGER.severe("Failed to get remote thread id: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }
        LOGGER.info("Remote thread id: $remoteThreadId")

        Kernel32.INSTANCE.CloseHandle(handle)
        Kernel32.INSTANCE.CloseHandle(remoteThread)

        return true
    }
}

fun main() {

    println(
        InjectionUtils.injectDll("game.dat", Path.of("C:\\Users\\DarkAtra\\Desktop\\defender-excluded\\bfme\\game-patcher.dll"))
    )
}
