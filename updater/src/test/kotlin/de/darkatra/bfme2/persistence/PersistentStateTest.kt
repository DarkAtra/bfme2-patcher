package de.darkatra.bfme2.persistence

import kotlinx.serialization.json.Json
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test

internal class PersistentStateTest {

    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
    }

    @Test
    fun `should deserialize persistent state with defaults and unknown fields`() {

        val persistentState = json.decodeFromString<PersistentState>(
            """
            {
              "hdEditionEnabled": true,
              "unknownSetting": true
            }
            """.trimIndent()
        )

        assertThat(persistentState).isEqualTo(
            PersistentState(
                hdEditionEnabled = true,
                patch202Enabled = true,
                modEnabled = true
            )
        )
    }

    @Test
    fun `should serialize persistent state`() {

        val content = json.encodeToString(
            PersistentState(
                timerEnabled = true,
                modEnabled = false
            )
        )

        assertThat(content).contains("\"hdEditionEnabled\":false")
        assertThat(content).contains("\"patch202Enabled\":true")
        assertThat(content).contains("\"timerEnabled\":true")
        assertThat(content).contains("\"modEnabled\":false")
    }
}
