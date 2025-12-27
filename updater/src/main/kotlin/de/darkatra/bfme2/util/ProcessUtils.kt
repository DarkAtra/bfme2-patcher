package de.darkatra.bfme2.util

import com.sun.jna.platform.win32.WinBase
import com.sun.jna.platform.win32.WinDef
import de.darkatra.bfme2.LOGGER
import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.name

object ProcessUtils {

    fun findProcess(name: String, ignoreCase: Boolean = true): ProcessHandle? {
        return ProcessHandle.allProcesses()
            .filter { process -> process.info().command().isPresent }
            .filter { process -> process.info().command().get().endsWith(name, ignoreCase = ignoreCase) }
            .findFirst()
            .orElse(null)
    }

    fun runBypassingDebugger(executable: Path, args: Array<String> = emptyArray()): Boolean {

        // we need to use jna here to set the DEBUG_ONLY_THIS_PROCESS flag when we create the process
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
            "$applicationName $commandLine",
            null,
            null,
            false,
            WinDef.DWORD(WinBase.DEBUG_ONLY_THIS_PROCESS.toLong()),
            null,
            executable.parent.absolutePathString(),
            startupInfo,
            processInformation
        )

        if (!successful) {
            throw RuntimeException("Could not run '${executable.absolutePathString()}', error code: ${Kernel32.INSTANCE.GetLastError()}")
        }

        // stop debugging the new process (it will be suspended otherwise)
        Kernel32.INSTANCE.DebugActiveProcessStop(processInformation.dwProcessId)

        Kernel32.INSTANCE.CloseHandle(processInformation.hThread)
        Kernel32.INSTANCE.CloseHandle(processInformation.hProcess)

        return true
    }

    fun run(executable: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf(executable.absolutePathString(), *args),
            executable.parent
        )
    }

    fun runJar(jar: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf("java", "-jar", jar.name, *args),
            jar.parent
        )
    }

    private fun run(command: List<String>, workingDirectory: Path): Process {
        return ProcessBuilder(command).apply {
            directory(workingDirectory.toFile())
        }.start()
    }
}
