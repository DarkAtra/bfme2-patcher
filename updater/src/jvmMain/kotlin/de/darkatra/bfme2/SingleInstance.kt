package de.darkatra.bfme2

import de.darkatra.bfme2.patch.Context
import java.io.RandomAccessFile
import java.nio.file.Path
import java.util.logging.Logger
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists

private val logger = Logger.getLogger("updater-single-instance")

object SingleInstance {

    private const val LOCK_FILE_NAME = "updater.lock"
    private val patcherUserDir: Path = UpdaterContext.context.getPatcherUserDir()
    private val lockFilePath: Path = patcherUserDir.resolve(LOCK_FILE_NAME)

    fun acquireLock(): Boolean {
        ensurePatcherUserDirExists()

        runCatching {
            val lockFile = RandomAccessFile(lockFilePath.toFile(), "rw")
            val lock = lockFile.channel.tryLock()
            if (lock != null) {
                Runtime.getRuntime().addShutdownHook(Thread {
                    lock.release()
                    lockFile.close()
                    lockFilePath.deleteIfExists()
                })
                return true
            }
        }.onFailure {
            logger.info("Could not acquire lock. Another instance of the application is probably already running.")
        }

        return false
    }

    private fun ensurePatcherUserDirExists() {
        if (!patcherUserDir.exists()) {
            check(patcherUserDir.toFile().mkdirs()) {
                "Could not create ${Context.PATCHER_USER_DIR_IDENTIFIER}."
            }
        }
    }
}
