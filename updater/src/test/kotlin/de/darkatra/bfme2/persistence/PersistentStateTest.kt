package de.darkatra.bfme2.persistence

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test

internal class PersistentStateTest {

    private val objectMapper = jacksonObjectMapper()

    @Test
    fun `should deserialize persistent state with defaults and unknown fields`() {

        val persistentState = objectMapper.readValue<PersistentState>(
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

        val json = objectMapper.writeValueAsString(
            PersistentState(
                timerEnabled = true,
                modEnabled = false
            )
        )

        assertThat(json).contains("\"timerEnabled\":true")
        assertThat(json).contains("\"modEnabled\":false")
    }
}
