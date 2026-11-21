package de.darkatra.bfme2.util

import com.sun.jna.platform.win32.WinBase
import com.sun.jna.platform.win32.WinDef
import com.sun.jna.ptr.IntByReference
import de.darkatra.bfme2.LOGGER
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object ProcessUtils {

    fun runSuspendedBypassingDebugger(
        executable: Path,
        args: List<String> = emptyList(),
        beforeResume: (gameProcessId: Long) -> Unit
    ): Int {

        // we need to use jna here to set the DEBUG_ONLY_THIS_PROCESS and CREATE_SUSPENDED flags when we create the process
        val startupInfo = WinBase.STARTUPINFO()
        val processInformation = WinBase.PROCESS_INFORMATION.ByReference()

        val applicationName = executable.absolutePathString()
        val commandLine = when (args.isNotEmpty()) {
            true -> args.joinToString(" ")
            false -> null
        }

        LOGGER.fine("Run '$applicationName' with args '$commandLine'")

        // create the process with debug flag to bypass IFEO
        val successful = Kernel32.INSTANCE.CreateProcess(
            null,
            buildString {
                append('"')
                append(applicationName)
                append('"')
                if (commandLine != null) {
                    append(' ')
                    append(commandLine)
                }
            },
            null,
            null,
            false,
            WinDef.DWORD((WinBase.DEBUG_ONLY_THIS_PROCESS or WinBase.CREATE_SUSPENDED).toLong()),
            null,
            executable.parent.absolutePathString(),
            startupInfo,
            processInformation
        )

        if (!successful) {
            // TODO: find a way to catch all errors using jb-compose error handler
            LOGGER.severe("Could not run '${executable.absolutePathString()}', error code: ${Kernel32.INSTANCE.GetLastError()}")
            throw IllegalStateException("Could not run '${executable.absolutePathString()}', error code: ${Kernel32.INSTANCE.GetLastError()}")
        }

        try {
            try {
                Kernel32.INSTANCE.DebugActiveProcessStop(processInformation.dwProcessId)

                beforeResume(processInformation.dwProcessId.toLong())

                if (Kernel32.INSTANCE.ResumeThread(processInformation.hThread) == -1) {
                    throw IllegalStateException("Could not resume '$applicationName', error code: ${Kernel32.INSTANCE.GetLastError()}")
                }
            } catch (e: Throwable) {
                Kernel32.INSTANCE.TerminateProcess(processInformation.hProcess, 1)
                throw IllegalStateException("Dll injection caused an unexpected error for '$applicationName'", e)
            }

            Kernel32.INSTANCE.WaitForSingleObject(processInformation.hProcess, WinBase.INFINITE)
            val exitCode = IntByReference()
            if (!Kernel32.INSTANCE.GetExitCodeProcess(processInformation.hProcess, exitCode)) {
                throw IllegalStateException("Could not read the exit code of '$applicationName', error code: ${Kernel32.INSTANCE.GetLastError()}")
            }
            return exitCode.value
        } finally {
            Kernel32.INSTANCE.CloseHandle(processInformation.hThread)
            Kernel32.INSTANCE.CloseHandle(processInformation.hProcess)
        }
    }

    fun run(executable: Path, args: List<String> = emptyList()): Process {
        return run(
            listOf(executable.absolutePathString()) + args,
            executable.parent
        )
    }

    private fun run(command: List<String>, workingDirectory: Path): Process {
        return ProcessBuilder(command).apply {
            directory(workingDirectory.toFile())
        }.start()
    }
}
