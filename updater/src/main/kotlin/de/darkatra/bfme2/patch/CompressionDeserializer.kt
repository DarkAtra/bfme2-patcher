package de.darkatra.bfme2.patch

import com.fasterxml.jackson.databind.util.StdConverter

class CompressionDeserializer : StdConverter<String, Compression>() {

    override fun convert(value: String?): Compression? {

        if (value == null) {
            return null
        }

        if (value == "ZIP") {
            return Compression.GZIP
        }

        return Compression.valueOf(value)
    }
}
