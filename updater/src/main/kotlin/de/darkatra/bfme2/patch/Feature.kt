package de.darkatra.bfme2.patch

import kotlinx.serialization.Serializable

@Serializable
enum class Feature {
    PATCH_202,
    MOD,
    TIMER,
    NEW_MUSIC,
    SKIP_INTRO,
    HD_EDITION,
}
