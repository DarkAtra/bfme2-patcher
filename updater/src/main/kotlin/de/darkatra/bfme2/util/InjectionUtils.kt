package de.darkatra.bfme2.util

import com.sun.jna.Memory
import com.sun.jna.Native
import com.sun.jna.Pointer
import com.sun.jna.platform.win32.BaseTSD
import com.sun.jna.platform.win32.WinDef.HMODULE
import com.sun.jna.platform.win32.WinNT.HANDLE
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

    // 0x3c into the dll - RVA of PE signature
    private const val OFFSET_TO_PE_SIGNATURE_POINTER = 0x3c

    // 0x78 bytes after PE signature - RVA of Export Table
    private const val OFFSET_TO_EXPORT_TABLE_FROM_SIGNATURE = 0x78

    // 0x18 into the Export Table - Number of function names exported by a module
    private const val OFFSET_TO_NUMBER_OF_EXPORTED_FUNCTION_NAMES_FROM_EXPORT_TABLE = 0x18

    // 0x1c into the Export Table - RVA of Address Table - addresses of exported functions
    private const val OFFSET_TO_EXPORTED_FUNCTIONS_FROM_EXPORT_TABLE = 0x1c

    // 0x20 into the Export Table - RVA of Name Pointer Table - addresses of exported function names
    private const val OFFSET_TO_EXPORTED_FUNCTION_NAMES_FROM_EXPORT_TABLE = 0x20

    // 0x24 into the Export Table - RVA of Ordinal Table - function order number as listed in the table
    private const val OFFSET_TO_EXPORTED_FUNCTION_ORDINALS_FROM_EXPORT_TABLE = 0x24

    fun injectDll(processId: Long, dllPath: Path): Boolean {

        val dllPathString = dllPath.absolutePathString()

        // get the handle to the process
        val processHandle = openHandleToProcess(processId)
        if (processHandle == null) {
            LOGGER.severe("Could not OpenProcess with pid '${processId}': ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        val loadLibraryPointer = getRemoteProcAddress(
            processHandle,
            getRemoteModuleHandle(processHandle, "kernel32.dll")!!,
            "LoadLibraryA"
        )
        if (loadLibraryPointer == null) {
            LOGGER.severe("Failed to get address for LoadLibraryA.")
            return false
        }

        // allocate memory for the dll path string
        val dllMemoryPointer = allocateMemoryForString(processHandle, dllPathString)
        if (dllMemoryPointer == null) {
            LOGGER.severe("Failed to allocate memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // write the dll path string to the allocated memory
        val writeToMemorySuccessful = writeStringToMemory(processHandle, dllMemoryPointer, dllPathString)
        if (!writeToMemorySuccessful) {
            LOGGER.severe("Failed to write to memory: ${Kernel32.INSTANCE.GetLastError()}")
            return false
        }

        // load the dll via remote thread
        val remoteThread = Kernel32.INSTANCE.CreateRemoteThread(
            processHandle,
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
        Kernel32.INSTANCE.CloseHandle(processHandle)

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

    private fun allocateMemoryForString(processHandle: HANDLE, string: String): Pointer? {

        return Kernel32.INSTANCE.VirtualAllocEx(
            processHandle,
            null,
            BaseTSD.SIZE_T(string.length + 1L),
            MEM_RESERVE or MEM_COMMIT,
            PAGE_EXECUTE_READWRITE
        )
    }

    private fun writeStringToMemory(processHandle: HANDLE, memoryPointer: Pointer, string: String): Boolean {

        val stringLength = string.length + 1L
        return Memory(stringLength).use { memory ->
            Kernel32.INSTANCE.WriteProcessMemory(
                processHandle,
                memoryPointer,
                memory.apply {
                    setString(0, string, StandardCharsets.UTF_8.name())
                },
                Math.toIntExact(stringLength),
                null
            )
        }
    }

    private fun getRemoteModuleHandle(processHandle: HANDLE, name: String): HMODULE? {

        val lphModule = arrayOfNulls<HMODULE>(1024)
        val lpcbNeededs = IntByReference()

        val successful = Psapi.INSTANCE.EnumProcessModulesEx(
            processHandle,
            lphModule,
            lphModule.size,
            lpcbNeededs,
            Psapi.LIST_MODULES_32BIT
        )
        if (!successful) {
            LOGGER.severe("Failed to enumerate process modules: ${Kernel32.INSTANCE.GetLastError()}")
            return null
        }

        return lphModule.filterNotNull().firstOrNull { module ->

            val moduleName = try {
                getModuleName(processHandle, module)
            } catch (e: Exception) {
                LOGGER.severe(e.message)
                return@firstOrNull false
            }

            moduleName.equals(name, true)
        }
    }

    private fun getModuleName(processHandle: HANDLE, module: HMODULE): String {

        val lpImageFileName = ByteArray(256)
        val successful = Psapi.INSTANCE.GetModuleBaseNameA(
            processHandle,
            module,
            lpImageFileName,
            lpImageFileName.size
        ) != 0

        if (!successful) {
            throw RuntimeException("Failed to get module name: ${Kernel32.INSTANCE.GetLastError()}")
        }

        return Native.toString(lpImageFileName)
    }

    private fun getRemoteProcAddress(processHandle: HANDLE, module: HMODULE, name: String): Pointer? {

        val moduleBase = getModuleBaseAddress(processHandle, module)
        val moduleBaseAddress = Pointer.nativeValue(moduleBase.pointer)

        val offsetToPESignature = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + OFFSET_TO_PE_SIGNATURE_POINTER),
            4
        ) {
            it.getInt(0)
        }

        val offsetToExportTable = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToPESignature + OFFSET_TO_EXPORT_TABLE_FROM_SIGNATURE),
            4
        ) {
            it.getInt(0)
        }

        val numberOfExportedFunctions = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportTable + OFFSET_TO_NUMBER_OF_EXPORTED_FUNCTION_NAMES_FROM_EXPORT_TABLE),
            4
        ) {
            it.getInt(0)
        }

        val offsetToExportedFunctionNamesTable = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportTable + OFFSET_TO_EXPORTED_FUNCTION_NAMES_FROM_EXPORT_TABLE),
            4
        ) {
            it.getInt(0)
        }

        val functionIndex = (0 until numberOfExportedFunctions)
            .map { i ->

                val offsetToFunctionName = readProcessMemory(
                    processHandle,
                    Pointer.createConstant(moduleBaseAddress + offsetToExportedFunctionNamesTable + i * 4),
                    4
                ) {
                    it.getInt(0)
                }

                val maxFunctionNameLength = name.length
                val functionName = readProcessMemory(processHandle, Pointer.createConstant(moduleBaseAddress + offsetToFunctionName), maxFunctionNameLength) {
                    it.getString(0)
                }
                Pair(i, functionName)
            }
            .firstOrNull { (_, functionName) ->
                name.equals(functionName, true)
            }
            ?.first
            ?: return null

        val offsetToExportedFunctionOrdinalsTable = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportTable + OFFSET_TO_EXPORTED_FUNCTION_ORDINALS_FROM_EXPORT_TABLE),
            4
        ) {
            it.getInt(0)
        }

        val functionOrdinal = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportedFunctionOrdinalsTable + functionIndex * 2),
            2
        ) {
            it.getShort(0)
        }

        val offsetToExportedFunctionAddressTable = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportTable + OFFSET_TO_EXPORTED_FUNCTIONS_FROM_EXPORT_TABLE),
            4
        ) {
            it.getInt(0)
        }

        val functionAddress = readProcessMemory(
            processHandle,
            Pointer.createConstant(moduleBaseAddress + offsetToExportedFunctionAddressTable + functionOrdinal * 4),
            4
        ) {
            it.getInt(0)
        }

        return Pointer.createConstant(moduleBaseAddress + functionAddress)
    }

    private fun getModuleBaseAddress(processHandle: HANDLE, module: HMODULE): HANDLE {

        val lpmodinfo = LPMODULEINFO()
        val successful = Psapi.INSTANCE.GetModuleInformation(
            processHandle,
            module,
            lpmodinfo,
            lpmodinfo.size()
        )

        if (!successful) {
            throw RuntimeException("Failed to get module info: ${Kernel32.INSTANCE.GetLastError()}")
        }

        return lpmodinfo.lpBaseOfDll!!
    }

    private fun <T> readProcessMemory(processHandle: HANDLE, address: Pointer, bytesToRead: Int, mappingFunction: (Memory) -> T): T {

        return Memory(bytesToRead.toLong()).use { memory ->
            val success = Kernel32.INSTANCE.ReadProcessMemory(
                processHandle,
                address,
                memory,
                bytesToRead,
                null
            )
            if (!success) {
                throw RuntimeException("Failed to read process memory: ${Kernel32.INSTANCE.GetLastError()}")
            }

            mappingFunction(memory)
        }
    }
}
