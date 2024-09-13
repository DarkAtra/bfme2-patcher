package de.darkatra.bfme2.persistence

import com.fasterxml.jackson.annotation.JsonIgnoreProperties

@JsonIgnoreProperties(ignoreUnknown = true)
data class PersistentState(
    val hdEditionEnabled: Boolean = false,
    val timerEnabled: Boolean = false,
    val skipIntroEnabled: Boolean = false,
    val newMusicEnabled: Boolean = false,
    val modEnabled: Boolean = true,
    val trayIconEnabled: Boolean = false
)
