package de.darkatra.bfme2.checksum

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.bouncycastle.jcajce.provider.digest.SHA3
import java.io.InputStream
import java.util.Base64

class HashingService {

    companion object {
        private const val BUFFER_SIZE = 8192
    }

    suspend fun calculateSha3Checksum(inputStream: InputStream): String = withContext(Dispatchers.IO) {

        val sha3Digest: SHA3.Digest256 = SHA3.Digest256()

        inputStream.buffered().use {

            sha3Digest.update(it.readNBytes(BUFFER_SIZE))

            ensureActive()
        }

        return@withContext Base64.getEncoder().encodeToString(sha3Digest.digest())
    }
}
