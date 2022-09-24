package de.darkatra.bfme2.patch

import com.fasterxml.jackson.annotation.JsonProperty
import com.fasterxml.jackson.databind.annotation.JsonDeserialize
import java.nio.file.Path
import java.time.Instant

data class Packet(
    var src: String,
    var dest: String,
    @JsonProperty("packetSize")
    val size: Long,
    val compressedSize: Long,
    val dateTime: Instant,
    val checksum: String,
    val backupExisting: Boolean,
    @JsonDeserialize(converter = CompressionDeserializer::class)
    val compression: Compression
) : ContextAware {

    /**
     * Applies the given [Context] to the [Packet], replacing placeholders with their actual value.
     * Ideally this operation should only be performed once per [Packet].
     *
     * @param context the [Context] containing values for placeholders
     */
    override fun applyContext(context: Context) {
        context.forEach { key, value ->
            src = src.replace("${Context.PREFIX}$key${Context.SUFFIX}", value)
            dest = Path.of(dest.replace("${Context.PREFIX}$key${Context.SUFFIX}", value)).normalize().toString()
        }
    }
}
