package de.darkatra.bfme2.persistence

import com.fasterxml.jackson.annotation.JsonIgnoreProperties
import io.goodforgod.graalvm.hint.annotation.ReflectionHint

@ReflectionHint(ReflectionHint.AccessType.ALL_DECLARED_CONSTRUCTORS)
@JsonIgnoreProperties(ignoreUnknown = true)
data class PersistentState(
    val hdEditionEnabled: Boolean = false,
    val timerEnabled: Boolean = false,
    val skipIntroEnabled: Boolean = false,
    val newMusicEnabled: Boolean = false,
    val patch202Enabled: Boolean = true,
    val modEnabled: Boolean = true,
    val trayIconEnabled: Boolean = false,
    val debugModeEnabled: Boolean = false
)
