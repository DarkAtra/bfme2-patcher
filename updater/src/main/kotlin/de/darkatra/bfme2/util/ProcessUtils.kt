package de.darkatra.bfme2.util

import com.sun.jna.platform.win32.WinBase
import com.sun.jna.platform.win32.WinDef
import de.darkatra.bfme2.LOGGER
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object ProcessUtils {

    fun runSuspendedBypassingDebugger(
        executable: Path,
        args: Array<String> = emptyArray(),
        beforeResume: (gameProcessId: Long) -> Unit
    ): ProcessHandle {

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
            Kernel32.INSTANCE.DebugActiveProcessStop(processInformation.dwProcessId)

            // resolve the process before resuming it, since it might exit right away
            val processHandle = ProcessHandle.of(processInformation.dwProcessId.toLong()).orElseThrow()

            beforeResume(processHandle.pid())

            if (Kernel32.INSTANCE.ResumeThread(processInformation.hThread) == -1) {
                throw IllegalStateException("Could not resume '$applicationName', error code: ${Kernel32.INSTANCE.GetLastError()}")
            }

            return processHandle
        } catch (e: Throwable) {
            Kernel32.INSTANCE.TerminateProcess(processInformation.hProcess, 1)
            throw IllegalStateException("Dll injection caused an unexpected error for '$applicationName'", e)
        } finally {
            Kernel32.INSTANCE.CloseHandle(processInformation.hThread)
            Kernel32.INSTANCE.CloseHandle(processInformation.hProcess)
        }
    }

    fun run(executable: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf(executable.absolutePathString(), *args),
            executable.parent
        )
    }

    private fun run(command: List<String>, workingDirectory: Path): Process {
        return ProcessBuilder(command).apply {
            directory(workingDirectory.toFile())
        }.start()
    }
}
