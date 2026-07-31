package de.darkatra.bfme2.checksum

import kotlinx.coroutines.test.runTest
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Path
import java.nio.file.attribute.FileTime
import kotlin.io.path.setLastModifiedTime
import kotlin.io.path.writeBytes

internal class HashingServiceTest {

    private val hashingService = HashingService

    @Test
    fun `should calculate expected base64 encoded sha3 checksum`(@TempDir tempDir: Path) = runTest {

        val tempFile = tempDir.resolve("checksum.txt")
        tempFile.writeBytes(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 0))

        val checksum = hashingService.calculateSha3Checksum(tempFile)

        assertThat(checksum).isEqualTo("wBiCMhkOBCf8nMeFlyIcdseZUoZgiJvWzh81YxSP+E0=")
    }

    @Test
    fun `should calculate expected nginx etag`(@TempDir tempDir: Path) = runTest {

        val tempFile = tempDir.resolve("etag.txt")
        tempFile.writeBytes(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 0))
        tempFile.setLastModifiedTime(FileTime.fromMillis(1785529399723))

        val etag = hashingService.calculateEtag(tempFile)

        assertThat(etag.toString()).isEqualTo("a-6a6d0437")
    }
}
