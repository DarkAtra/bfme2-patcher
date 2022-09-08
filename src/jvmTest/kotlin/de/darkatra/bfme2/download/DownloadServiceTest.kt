package de.darkatra.bfme2.download

import com.github.tomakehurst.wiremock.client.WireMock.get
import com.github.tomakehurst.wiremock.client.WireMock.ok
import com.github.tomakehurst.wiremock.junit5.WireMockRuntimeInfo
import com.github.tomakehurst.wiremock.junit5.WireMockTest
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import java.net.URI

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

        @Suppress("UNCHECKED_CAST")
        val content: Map<String, String> = downloadService.getContent(URI.create("http://localhost:$port/test.json").toURL(), Map::class) as Map<String, String>

        assertThat(content).containsEntry("name", "Testi")
    }
}
