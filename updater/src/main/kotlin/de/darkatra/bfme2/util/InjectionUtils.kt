package de.darkatra.bfme2.util

import com.sun.jna.Memory
import com.sun.jna.Pointer
import com.sun.jna.platform.win32.BaseTSD
import com.sun.jna.platform.win32.WinNT.HANDLE
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

    fun injectDll(processId: Long, dllPath: Path): Boolean {

        val dllPathString = dllPath.absolutePathString()

        // get the handle to the process
        val handle = openHandleToProcess(processId)
        if (handle == null) {
            LOGGER.severe("Could not OpenProcess with pid '${processId}': ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // val loadLibraryPointer = AnsiKernel32.INSTANCE.GetProcAddress(
        //     Kernel32.INSTANCE.GetModuleHandle("kernel32.dll"),
        //     "LoadLibraryA"
        // )?.pointer
        // if (loadLibraryPointer == null) {
        //     LOGGER.severe("Failed to get address for LoadLibraryA: ${Kernel32.INSTANCE.GetLastError()}")
        //     return false
        // }
        // FIXME: resolve the address to LoadLibraryA dynamically (something similar to the code above)
        val loadLibraryPointer = Pointer.createConstant(0x76C60E70)

        // allocate memory for the dll path string
        val dllMemoryPointer = allocateMemoryForString(handle, dllPathString)
        if (dllMemoryPointer == null) {
            LOGGER.severe("Failed to allocate memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // write the dll path string to the allocated memory
        val writeToMemorySuccessful = writeStringToMemory(handle, dllMemoryPointer, dllPathString)
        if (!writeToMemorySuccessful) {
            LOGGER.severe("Failed to write to memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // load the dll via remote thread
        val remoteThread = Kernel32.INSTANCE.CreateRemoteThread(
            handle,
            null,
            0,
            loadLibraryPointer,
            dllMemoryPointer,
            0,
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

        Kernel32.INSTANCE.CloseHandle(remoteThread)
        Kernel32.INSTANCE.CloseHandle(handle)

        return true
    }

    private fun openHandleToProcess(processId: Long): HANDLE? {

        return Kernel32.INSTANCE.OpenProcess(
            PROCESS_CREATE_THREAD or
                PROCESS_QUERY_INFORMATION or
                PROCESS_VM_OPERATION or
                PROCESS_VM_READ or
                PROCESS_VM_WRITE,
            false,
            Math.toIntExact(processId)
        )
    }

    private fun allocateMemoryForString(handle: HANDLE, string: String): Pointer? {

        return Kernel32.INSTANCE.VirtualAllocEx(
            handle,
            null,
            BaseTSD.SIZE_T(string.length + 1L),
            MEM_RESERVE or MEM_COMMIT,
            PAGE_EXECUTE_READWRITE
        )
    }

    private fun writeStringToMemory(handle: HANDLE, memoryPointer: Pointer, string: String): Boolean {

        val stringLength = string.length + 1L
        return Kernel32.INSTANCE.WriteProcessMemory(
            handle,
            memoryPointer,
            Memory(stringLength).apply {
                setString(0, string, StandardCharsets.UTF_8.name())
            },
            Math.toIntExact(stringLength),
            null
        )
    }
}
