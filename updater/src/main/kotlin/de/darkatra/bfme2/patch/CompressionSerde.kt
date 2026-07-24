package de.darkatra.bfme2.patch

import kotlinx.serialization.KSerializer
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder

object CompressionSerde : KSerializer<Compression> {

    override val descriptor: SerialDescriptor = PrimitiveSerialDescriptor("compression", PrimitiveKind.STRING)

    override fun deserialize(decoder: Decoder): Compression {

        val value = decoder.decodeString()
        if (value == "ZIP") {
            return Compression.GZIP
        }

        return Compression.valueOf(value)
    }

    override fun serialize(encoder: Encoder, value: Compression) {
        encoder.encodeString(value.name)
    }
}
