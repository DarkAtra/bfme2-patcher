package de.darkatra.bfme2

import java.io.RandomAccessFile
import java.nio.channels.FileLock
import java.nio.file.Path
import java.util.logging.Level
import kotlin.io.path.deleteIfExists

object SingleInstance {

    private const val LOCK_FILE_NAME = "updater.lock"
    private val patcherUserDir: Path = UpdaterContext.context.getPatcherUserDir()
    private val lockFilePath: Path = patcherUserDir.resolve(LOCK_FILE_NAME)

    fun acquireLock(): Boolean {

        try {
            val lockFile = RandomAccessFile(lockFilePath.toFile(), "rw")
            val lock: FileLock? = lockFile.channel.tryLock()
            if (lock != null) {
                Runtime.getRuntime().addShutdownHook(Thread {
                    lock.release()
                    lockFile.close()
                    lockFilePath.deleteIfExists()
                })
                return true
            }
        } catch (e: Exception) {
            LOGGER.log(Level.INFO, "Could not acquire lock. Another instance of the application is probably already running.", e)
        }

        return false
    }
}
