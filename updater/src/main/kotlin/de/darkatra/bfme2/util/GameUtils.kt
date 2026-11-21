package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.injector.Injector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object GameUtils {

    // game-patcher's PATCH_FAILURE_EXIT_CODE (dllmain.cpp)
    private const val PATCH_FAILURE_EXIT_CODE = 0x47500001

    suspend fun launchGame(rotwkHomeDir: Path): Unit = withContext(Dispatchers.IO) {

        LOGGER.info("Launching game without game-patcher...")

        PatchService.configureCompatModeIfNecessary(hookEnabled = false)

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

        PatchService.configureCompatModeIfNecessary(hookEnabled = true)

        val gameArgs = buildList {
            if (debugModeEnabled) {
                add("-debugmode")
            }
            if (fpsCounterEnabled) {
                add("-showfps")
            }
        }

        val exitCode = ProcessUtils.runSuspendedBypassingDebugger(
            rotwkHomeDir.resolve(UpdaterContext.ROTWK_EXE_NAME).normalize(),
            gameArgs
        ) { gameProcessId ->
            LOGGER.info("Injecting patches via game-patcher...")

            Injector.injectDllIntoSuspendedProcess(gameProcessId, rotwkHomeDir.resolve("game-patcher.dll").normalize(), JavaLogger)
        }

        if (exitCode == PATCH_FAILURE_EXIT_CODE) {
            val logPath = rotwkHomeDir.resolve(".patcher").resolve("game-patcher-log.txt").normalize()
            throw IllegalStateException("game-patcher could not apply all of its patches and closed the game. Details in: ${logPath.absolutePathString()}")
        }
    }
}
