package de.darkatra.bfme2.checksum

import kotlinx.coroutines.test.runTest
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import java.io.ByteArrayInputStream

internal class HashingServiceTest {

    private val hashingService = HashingService

    @Test
    fun `should calculate expected base64 encoded sha3 checksum for input stream`() = runTest {

        val inputStream = ByteArrayInputStream(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 0))

        val checksum = hashingService.calculateSha3Checksum(inputStream)

        assertThat(checksum).isEqualTo("wBiCMhkOBCf8nMeFlyIcdseZUoZgiJvWzh81YxSP+E0=")
    }
}
