package de.darkatra.bfme2.util

import com.sun.jna.Memory
import com.sun.jna.Pointer
import com.sun.jna.platform.win32.BaseTSD
import com.sun.jna.platform.win32.WinNT.MEM_COMMIT
import com.sun.jna.platform.win32.WinNT.MEM_RESERVE
import com.sun.jna.platform.win32.WinNT.PAGE_EXECUTE_READWRITE
import com.sun.jna.platform.win32.WinNT.PROCESS_CREATE_THREAD
import com.sun.jna.platform.win32.WinNT.PROCESS_QUERY_INFORMATION
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_OPERATION
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_READ
import com.sun.jna.platform.win32.WinNT.PROCESS_VM_WRITE
import com.sun.jna.ptr.IntByReference
import de.darkatra.bfme2.LOGGER
import java.nio.charset.StandardCharsets
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object InjectionUtils {

    // TODO: split up into functions per operation
    fun injectDll(processName: String, dllPath: Path): Boolean {

        // get the process id by name
        val processId = ProcessUtils.findProcessId(processName)
        if (processId == null) {
            LOGGER.severe("Could not find process for name '$processName'")
            return false
        }

        // get the handle to the process
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

        // get the address of LoadLibraryA
//        val loadLibraryPointer = AnsiKernel32.INSTANCE.GetProcAddress(
//            Kernel32.INSTANCE.GetModuleHandle("kernel32.dll"),
//            "LoadLibraryA"
//        )?.pointer
//        if (loadLibraryPointer == null) {
//            LOGGER.severe("Failed to get address for LoadLibraryA: ${Kernel32.INSTANCE.GetLastError()}")
//            return false
//        }
        // FIXME: the above code resolves to the wrong address for some reason
        val loadLibraryPointer = Pointer.createConstant(0x76C60E70)

        // allocate memory for the dll path string
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

        // write the dll path string to the allocated memory
        val writeToMemorySuccessful = Kernel32.INSTANCE.WriteProcessMemory(
            handle,
            dllMemoryPointer,
            Memory(dllPathStringLength).apply {
                setString(0, dllPathString, StandardCharsets.UTF_8.name())
            },
            Math.toIntExact(dllPathStringLength),
            null
        )
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

        // TODO: move this to a separate function
        val certPatchAddress = Pointer.createConstant(0x00a8d096)
        val bytesToWrite = byteArrayOf(0xEB.toByte(), 0x46.toByte(), 0x90.toByte(), 0x90.toByte())
        val certPatchSuccessful = Kernel32.INSTANCE.WriteProcessMemory(
            handle,
            certPatchAddress,
            Memory(bytesToWrite.size.toLong()).apply {
                bytesToWrite.forEachIndexed { index, byte ->
                    setByte(index.toLong(), byte)
                }
            },
            4,
            IntByReference()
        )
        if (!certPatchSuccessful) {
            LOGGER.severe("Failed to patch cert validation: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        Kernel32.INSTANCE.CloseHandle(handle)

        return true
    }
}

// FIXME: remove me
fun main() {

    println(
        InjectionUtils.injectDll("game.dat", Path.of("C:\\Users\\DarkAtra\\Desktop\\defender-excluded\\bfme\\game-patcher.dll"))
    )
}
