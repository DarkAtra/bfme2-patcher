package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.injector.Injector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.time.withTimeoutOrNull
import kotlinx.coroutines.withContext
import java.nio.file.Path
import java.time.Duration
import kotlin.io.path.absolutePathString

object GameUtils {

    suspend fun launchGame(rotwkHomeDir: Path, patcherUserDir: Path, hdEditionEnabled: Boolean) = withContext(Dispatchers.IO) {

        LOGGER.info("Launching game without game-patcher...")

        val gameProcess = ProcessUtils.run(
            rotwkHomeDir.resolve(UpdaterContext.ROTWK_EXE_NAME).normalize(),
            when (hdEditionEnabled) {
                true -> arrayOf(
                    "-mod",
                    "\"${patcherUserDir.resolve("HDEdition.big").normalize().absolutePathString()}\""
                )

                false -> emptyArray()
            }
        )

        gameProcess.waitFor()
    }

    suspend fun launchGameBypassingDebugger(rotwkHomeDir: Path, patcherUserDir: Path, hdEditionEnabled: Boolean): Boolean = withContext(Dispatchers.IO) {

        LOGGER.info("Launching game with game-patcher...")

        val successful = ProcessUtils.runBypassingDebugger(
            rotwkHomeDir.resolve(UpdaterContext.ROTWK_EXE_NAME).normalize(),
            when (hdEditionEnabled) {
                true -> arrayOf(
                    "-mod",
                    "\"${patcherUserDir.resolve("HDEdition.big").normalize().absolutePathString()}\""
                )

                false -> emptyArray()
            }
        )

        if (successful) {
            LOGGER.info("Injecting patches via game-patcher...")

            val gameProcess = withTimeoutOrNull(Duration.ofSeconds(5)) {
                var gameProcess: ProcessHandle?
                while (ProcessUtils.findProcess(UpdaterContext.ROTWK_EXE_NAME).also { gameProcess = it } == null) {
                    delay(500)
                }
                return@withTimeoutOrNull gameProcess
            } ?: return@withContext false

            Injector.injectDll(gameProcess.pid(), rotwkHomeDir.resolve("game-patcher.dll").normalize(), JavaLogger)
            gameProcess.onExit().get()
        }

        return@withContext true
    }
}
