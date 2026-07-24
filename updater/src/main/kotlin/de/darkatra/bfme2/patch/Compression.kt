package de.darkatra.bfme2.patch

import kotlinx.serialization.Serializable

@Serializable(with = CompressionSerde::class)
enum class Compression {
    NONE,
    GZIP
}
