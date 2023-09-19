package de.darkatra.patcher.ifeo

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
    }
}
