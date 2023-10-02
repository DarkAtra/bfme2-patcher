package de.darkatra.bfme2

import java.io.RandomAccessFile
import java.nio.file.Path
import java.util.logging.Logger
import kotlin.io.path.deleteIfExists

private val logger = Logger.getLogger("updater-single-instance")

object SingleInstance {

    private const val LOCK_FILE_NAME = "updater.lock"
    private val patcherUserDir: Path = UpdaterContext.context.getPatcherUserDir()
    private val lockFilePath: Path = patcherUserDir.resolve(LOCK_FILE_NAME)

    fun acquireLock(): Boolean {

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
}
