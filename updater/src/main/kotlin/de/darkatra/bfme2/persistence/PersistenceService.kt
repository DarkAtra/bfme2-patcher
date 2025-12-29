package de.darkatra.bfme2.persistence

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import java.io.IOException
import kotlin.io.path.outputStream

object PersistenceService {

    private const val PATCHER_STATE_FILE_NAME = "patcher-state.json"

    private val objectMapper: ObjectMapper = jacksonObjectMapper()
    private val patcherUserDir = UpdaterContext.context.getPatcherUserDir()

    fun loadPersistentState(): PersistentState {
        return try {
            objectMapper.readValue<PersistentState>(
                patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().toFile()
            )
        } catch (e: IOException) {
            LOGGER.info("Could not parse patcher state. Message: ${e.message}")
            PersistentState()
        }
    }

    fun savePersistentState(persistentState: PersistentState) {
        objectMapper.writeValue(
            patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().outputStream(),
            persistentState
        )
    }
}
