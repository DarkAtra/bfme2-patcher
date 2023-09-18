package de.darkatra.patcher.ifeo

import com.sun.jna.platform.win32.WinBase
import com.sun.jna.platform.win32.WinBase.PROCESS_INFORMATION
import com.sun.jna.platform.win32.WinBase.STARTUPINFO
import com.sun.jna.platform.win32.WinDef.DWORD
import java.nio.file.Path
import java.util.Base64
import java.util.logging.FileHandler
import java.util.logging.Logger
import java.util.logging.SimpleFormatter
import kotlin.io.path.absolutePathString
import kotlin.io.path.notExists

val LOGGER: Logger = Logger.getLogger("updater")

fun main(args: Array<String>) {

    if ("filelog" in args) {
        LOGGER.addHandler(
            FileHandler(ApplicationHome.parent.toAbsolutePath().resolve("ifeo-log-%g.txt").absolutePathString()).apply {
                formatter = SimpleFormatter()
            }
        )
    }

    LOGGER.info("Args: ${args.joinToString(", ")}")

    when {
        "set" in args -> {
            val executableIndex = args.indexOf("set") + 1
            if (executableIndex >= args.size) {
                LOGGER.info("The set operation expects one argument.")
                return
            }

            val executable = Path.of(String(Base64.getDecoder().decode(args[executableIndex])))
            if (executable.notExists()) {
                LOGGER.info("Executable does not exist: ${executable.absolutePathString()}")
                return
            }

            LOGGER.info("Setting Debugger to '${executable.absolutePathString()}'")
            RegistryService.setExpansionDebugger(executable)
        }

        "reset" in args -> {
            LOGGER.info("Resetting Debugger")
            RegistryService.resetExpansionDebugger()
        }

        // if a debugger is set, we need to launch the executable with specific flags to prevent spawning another instance of the debugger
        // see the following link for a detailed explanation: https://stackoverflow.com/a/54844956/6357653
        "run" in args -> {

            val executableIndex = args.indexOf("run") + 1
            if (executableIndex >= args.size) {
                LOGGER.info("The run operation expects one argument.")
                return
            }

            val executable = Path.of(String(Base64.getDecoder().decode(args[executableIndex])))
            if (executable.notExists()) {
                LOGGER.info("Executable does not exist: ${executable.absolutePathString()}")
                return
            }

            var additionalArguments: String? = null
            val additionalArgumentsIndex = executableIndex + 1
            if (additionalArgumentsIndex < args.size) {
                additionalArguments = String(Base64.getDecoder().decode(args[additionalArgumentsIndex]))
            }

            LOGGER.info("Running '${executable.absolutePathString()}' without a debugger and args '${additionalArguments}'.")

            // we need to use jna here to set the DEBUG_ONLY_THIS_PROCESS flag when we create the process
            val startupInfo = STARTUPINFO()
            val processInformation = PROCESS_INFORMATION.ByReference()

            // create the process with debug flag to bypass IFEO
            val successful = Kernel32.INSTANCE.CreateProcess(
                executable.absolutePathString(),
                when (additionalArguments != null) {
                    true -> " $additionalArguments" // note: the leading space is important
                    false -> null
                },
                null,
                null,
                false,
                DWORD(WinBase.DEBUG_ONLY_THIS_PROCESS.toLong()),
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
    }
}
