package de.darkatra.bfme2.persistence

import kotlinx.serialization.Serializable

@Serializable
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
