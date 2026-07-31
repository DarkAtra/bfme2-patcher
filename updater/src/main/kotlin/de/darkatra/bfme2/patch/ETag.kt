package de.darkatra.bfme2.patch

import java.time.Instant

data class ETag(
    val fileSize: Long,
    val lastModified: Instant,
) {

    override fun toString(): String {
        return "${fileSize.toString(16)}-${(lastModified.toEpochMilli() / 1000).toString(16)}"
    }
}
