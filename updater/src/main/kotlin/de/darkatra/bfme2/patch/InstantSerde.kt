package de.darkatra.bfme2.patch

import kotlinx.serialization.KSerializer
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import kotlinx.serialization.json.JsonDecoder
import kotlinx.serialization.json.JsonEncoder
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonPrimitive
import java.math.BigDecimal
import java.math.RoundingMode
import java.time.Instant

object InstantSerde : KSerializer<Instant> {

    override val descriptor: SerialDescriptor = PrimitiveSerialDescriptor("Instant", PrimitiveKind.DOUBLE)

    override fun deserialize(decoder: Decoder): Instant {

        if (decoder !is JsonDecoder) {
            throw IllegalStateException(
                "This serializer can be used only with Json format." +
                    "Expected Decoder to be JsonDecoder, got ${decoder::class}"
            )
        }

        val jsonPrimitive = decoder.decodeJsonElement().jsonPrimitive
        return when {
            jsonPrimitive.isString -> Instant.parse(jsonPrimitive.content)
            else -> parseTimestamp(jsonPrimitive.content)
        }
    }

    override fun serialize(encoder: Encoder, value: Instant) {

        val timestamp = BigDecimal.valueOf(value.epochSecond) + BigDecimal.valueOf(value.nano.toLong(), 9)

        if (encoder is JsonEncoder) {
            encoder.encodeJsonElement(JsonPrimitive(timestamp))
        } else {
            encoder.encodeDouble(timestamp.toDouble())
        }
    }

    private fun parseTimestamp(value: String): Instant {

        val timestamp = value.toBigDecimal()
        val seconds = timestamp.setScale(0, RoundingMode.DOWN).longValueExact()
        val nanos = timestamp.subtract(BigDecimal.valueOf(seconds)).movePointRight(9).setScale(0, RoundingMode.DOWN).longValueExact()

        return Instant.ofEpochSecond(seconds, nanos)
    }
}
