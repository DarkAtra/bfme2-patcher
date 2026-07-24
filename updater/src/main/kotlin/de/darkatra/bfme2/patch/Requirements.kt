package de.darkatra.bfme2.patch

import kotlinx.serialization.Serializable

@Serializable
data class Requirements(
    val minUpdaterVersion: String? = null
)
