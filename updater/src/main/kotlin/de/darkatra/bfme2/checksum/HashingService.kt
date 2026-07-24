package de.darkatra.bfme2.checksum

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.security.MessageDigest
import java.util.Base64

object HashingService {

    suspend fun calculateSha3Checksum(inputStream: InputStream): String = withContext(Dispatchers.IO) {

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
}
