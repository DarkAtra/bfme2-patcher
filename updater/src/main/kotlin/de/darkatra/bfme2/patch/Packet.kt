package de.darkatra.bfme2.patch

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import java.nio.file.Path
import java.time.Instant

@Serializable
data class Packet(
    var src: String,
    var dest: String,
    @SerialName("packetSize")
    val size: Long,
    val compressedSize: Long,
    @Serializable(with = InstantSerde::class)
    val dateTime: Instant,
    val checksum: String,
    val backupExisting: Boolean,
    val compression: Compression,
    val feature: Feature? = null
) : ContextAware {

    /**
     * Applies the given [Context] to the [Packet], replacing placeholders with their actual value.
     * Ideally, this operation should only be performed once per [Packet].
     *
     * @param context the [Context] containing values for placeholders
     */
    override fun applyContext(context: Context) {

        val origSrc: String = src
        val origDest: String = dest

        context.forEach { (key, value) ->
            src = src.replace("${Context.PREFIX}$key${Context.SUFFIX}", value)
            dest = Path.of(dest.replace("${Context.PREFIX}$key${Context.SUFFIX}", value)).normalize().toString()
        }

        if (src == origSrc || dest == origDest) {
            error("Invalid Packet without placeholder was found. (Src: '$src', Dest: '$dest')")
        }
    }
}
