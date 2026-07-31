package de.darkatra.bfme2.checksum

import de.darkatra.bfme2.patch.ETag
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.net.URL
import java.nio.file.Path
import java.security.MessageDigest
import java.time.Instant
import java.util.Base64
import kotlin.io.path.fileSize
import kotlin.io.path.getLastModifiedTime
import kotlin.io.path.inputStream
import kotlin.io.path.isRegularFile
import kotlin.io.path.pathString

object HashingService {

    suspend fun calculateSha3Checksum(path: Path): String = withContext(Dispatchers.IO) {
        if (!path.isRegularFile()) {
            error("File is not a regular file: ${path.pathString}")
        }
        return@withContext calculateSha3Checksum(path.inputStream())
    }

    suspend fun calculateSha3Checksum(url: URL): String = withContext(Dispatchers.IO) {
        return@withContext calculateSha3Checksum(url.openStream())
    }

    private suspend fun calculateSha3Checksum(inputStream: InputStream): String = withContext(Dispatchers.IO) {

        val sha3Digest = MessageDigest.getInstance("SHA3-256")

        inputStream.buffered().use { bufferedInputStream ->

            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var count: Int

            while (bufferedInputStream.read(buffer).also { count = it } != -1) {
                sha3Digest.update(buffer, 0, count)
                ensureActive()
            }
        }

        return@withContext Base64.getEncoder().encodeToString(sha3Digest.digest())
    }

    fun calculateEtag(path: Path): ETag {

        if (!path.isRegularFile()) {
            error("File is not a regular file: ${path.pathString}")
        }

        val size = path.fileSize()
        val modified = path.getLastModifiedTime().toMillis()

        return ETag(
            fileSize = size,
            lastModified = Instant.ofEpochMilli(modified)
        )
    }
}
