package de.darkatra.bfme2.persistence

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import de.darkatra.bfme2.UpdaterContext
import java.io.IOException
import java.util.logging.Logger
import kotlin.io.path.exists
import kotlin.io.path.outputStream

object PersistenceService {

    private const val PATCHER_STATE_FILE_NAME = "patcher-state.json"

    private val logger = Logger.getLogger("updater-persistence")
    private val objectMapper: ObjectMapper = jacksonObjectMapper()
    private val patcherUserDir = UpdaterContext.context.getPatcherUserDir()

    fun loadPersistentState(): PersistentState {
        return try {
            objectMapper.readValue(
                patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().toFile(),
                PersistentState::class.java
            )
        } catch (e: IOException) {
            logger.info("Could not parse patcher state. Message: ${e.message}")
            PersistentState()
        }
    }

    fun savePersistentState(persistentState: PersistentState) {
        if (!patcherUserDir.exists()) {
            patcherUserDir.toFile().mkdirs()
        }
        objectMapper.writeValue(
            patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().outputStream(),
            persistentState
        )
    }
}
