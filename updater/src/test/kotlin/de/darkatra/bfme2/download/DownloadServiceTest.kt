package de.darkatra.bfme2.download

import com.fasterxml.jackson.module.kotlin.jacksonTypeRef
import com.github.tomakehurst.wiremock.client.WireMock.get
import com.github.tomakehurst.wiremock.client.WireMock.ok
import com.github.tomakehurst.wiremock.junit5.WireMockRuntimeInfo
import com.github.tomakehurst.wiremock.junit5.WireMockTest
import de.darkatra.bfme2.patch.Compression
import de.darkatra.bfme2.randomString
import kotlinx.coroutines.test.runTest
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.io.ByteArrayOutputStream
import java.net.URI
import java.nio.charset.StandardCharsets
import java.nio.file.Path
import java.util.zip.GZIPOutputStream
import kotlin.io.path.readText

@WireMockTest
internal class DownloadServiceTest {

    private val downloadService = DownloadService

    @Test
    fun `should download content from uri`(wireMockRuntimeInfo: WireMockRuntimeInfo) {

        wireMockRuntimeInfo.wireMock.register(
            get("/test.json").willReturn(
                ok().withBody("{\"name\": \"Testi\"}")
            )
        )

        val port = wireMockRuntimeInfo.httpPort

        val content: Map<String, String> = downloadService.getContent(
            url = URI.create("http://localhost:$port/test.json").toURL(),
            typeOfT = jacksonTypeRef<Map<String, String>>()
        )

        assertThat(content).containsEntry("name", "Testi")
    }

    @Test
    fun `should download content from uri with reified type parameter`(wireMockRuntimeInfo: WireMockRuntimeInfo) {

        wireMockRuntimeInfo.wireMock.register(
            get("/test.json").willReturn(
                ok().withBody("{\"name\": \"Testi\"}")
            )
        )

        val port = wireMockRuntimeInfo.httpPort

        val content: Map<String, String> = downloadService.getContent<Map<String, String>>(
            url = URI.create("http://localhost:$port/test.json").toURL()
        )

        assertThat(content).containsEntry("name", "Testi")
    }

    @Test
    fun `should download file`(wireMockRuntimeInfo: WireMockRuntimeInfo, @TempDir tempDir: Path) = runTest {

        wireMockRuntimeInfo.wireMock.register(
            get("/test.json").willReturn(
                ok().withBody("{\"name\": \"Testi\"}")
            )
        )

        val port = wireMockRuntimeInfo.httpPort
        val tempFile = tempDir.resolve("test.json")

        downloadService.download(
            src = URI.create("http://localhost:$port/test.json").toURL(),
            dest = tempFile,
            compression = Compression.NONE
        )

        assertThat(tempFile.readText()).isEqualTo("{\"name\": \"Testi\"}")
    }

    @Test
    fun `should download file with compression`(wireMockRuntimeInfo: WireMockRuntimeInfo, @TempDir tempDir: Path) = runTest {

        val content = randomString(1024)

        val byteArrayOutputStream = ByteArrayOutputStream()
        GZIPOutputStream(byteArrayOutputStream).use { output ->
            output.write(content.toByteArray(StandardCharsets.UTF_8))
        }
        val gzippedContent = byteArrayOutputStream.toByteArray()

        wireMockRuntimeInfo.wireMock.register(
            get("/test.json").willReturn(
                ok().withBody(gzippedContent)
            )
        )

        val port = wireMockRuntimeInfo.httpPort
        val tempFile = tempDir.resolve("test.json")

        var diskByteCount = 0L
        var networkByteCount = 0L
        downloadService.download(
            src = URI.create("http://localhost:$port/test.json").toURL(),
            dest = tempFile,
            compression = Compression.GZIP,
            progressListener = { progress ->
                diskByteCount += progress.countDisk
                networkByteCount += progress.countNetwork
            }
        )

        assertThat(tempFile.readText()).isEqualTo(content)
        assertThat(diskByteCount).isEqualTo(content.length.toLong())
        assertThat(networkByteCount).isEqualTo(gzippedContent.count().toLong())
    }
}
