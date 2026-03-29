package de.darkatra.bfme2.patch

import io.goodforgod.graalvm.hint.annotation.ReflectionHint

@ReflectionHint(ReflectionHint.AccessType.ALL_DECLARED_CONSTRUCTORS)
data class Requirements(
    val minUpdaterVersion: String? = null
)
