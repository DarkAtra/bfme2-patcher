package de.darkatra.bfme2.util

import com.sun.jna.platform.win32.WinBase
import com.sun.jna.platform.win32.WinDef
import de.darkatra.bfme2.LOGGER
import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.name

object ProcessUtils {

    fun runBypassingDebuggerAndWait(executable: Path, args: Array<String> = emptyArray()) {

        // we need to use jna here to set the DEBUG_ONLY_THIS_PROCESS flag when we create the process
        val startupInfo = WinBase.STARTUPINFO()
        val processInformation = WinBase.PROCESS_INFORMATION.ByReference()

        // create the process with debug flag to bypass IFEO
        val successful = Kernel32.INSTANCE.CreateProcess(
            executable.absolutePathString(),
            when (args.isNotEmpty()) {
                true -> " ${args.joinToString(" ")}" // note: the leading space is important
                false -> null
            },
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
            LOGGER.severe("Could not run '${executable.absolutePathString()}', error code: ${Kernel32.INSTANCE.GetLastError()}")
            return
        }

        // stop debugging the new process (it will be suspended otherwise)
        Kernel32.INSTANCE.DebugActiveProcessStop(processInformation.dwProcessId)

        Kernel32.INSTANCE.WaitForSingleObject(processInformation.hProcess, WinBase.INFINITE)
        Kernel32.INSTANCE.CloseHandle(processInformation.hProcess)
        Kernel32.INSTANCE.CloseHandle(processInformation.hThread)
    }

    fun run(executable: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf(executable.absolutePathString(), *args),
            executable.parent
        )
    }

    fun runElevated(executable: Path, args: Array<String> = emptyArray()): Process {
        return Runtime.getRuntime().exec(
            "cmd /c \"${executable.absolutePathString()}\" ${args.joinToString(" ")}"
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
