package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.injector.Injector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Path

object GameUtils {

    suspend fun launchGame(rotwkHomeDir: Path): Unit = withContext(Dispatchers.IO) {

        LOGGER.info("Launching game without game-patcher...")

        val gameProcess = ProcessUtils.run(
            rotwkHomeDir.resolve(UpdaterContext.ROTWK_EXE_NAME).normalize(),
        )

        gameProcess.waitFor()
    }

    suspend fun launchGameBypassingDebugger(
        rotwkHomeDir: Path,
        debugModeEnabled: Boolean,
        fpsCounterEnabled: Boolean,
    ): Unit = withContext(Dispatchers.IO) {

        LOGGER.info("Launching game with game-patcher...")

        val gameArgs = buildList {
            if (debugModeEnabled) {
                add("-debugmode")
            }
            if (fpsCounterEnabled) {
                add("-showfps")
            }
        }

        val gameProcess = ProcessUtils.runSuspendedBypassingDebugger(
            rotwkHomeDir.resolve(UpdaterContext.ROTWK_EXE_NAME).normalize(),
            gameArgs
        ) { gameProcessId ->
            LOGGER.info("Injecting patches via game-patcher...")

            Injector.injectDllIntoSuspendedProcess(gameProcessId, rotwkHomeDir.resolve("game-patcher.dll").normalize(), JavaLogger)
        }

        gameProcess.onExit().get()
    }
}
