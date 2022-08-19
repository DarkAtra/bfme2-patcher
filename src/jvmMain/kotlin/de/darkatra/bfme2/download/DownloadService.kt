package de.darkatra.bfme2.download

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import java.net.URI
import kotlin.reflect.KClass

class DownloadService(
    private val objectMapper: ObjectMapper = jacksonObjectMapper()
) {

    fun <T : Any> getContent(uri: URI, clazz: KClass<T>): T {

        val content = uri.toURL().openStream().bufferedReader().use { it.readText() }

        return objectMapper.readValue(content, clazz.java)
    }
}
