package de.darkatra.bfme2.persistence

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import java.io.IOException
import kotlin.io.path.readText
import kotlin.io.path.writeText

object PersistenceService {

    private const val PATCHER_STATE_FILE_NAME = "patcher-state.json"

    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
    }
    private val patcherUserDir = UpdaterContext.context.getPatcherUserDir()

    fun loadPersistentState(): PersistentState {
        return try {
            json.decodeFromString<PersistentState>(patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().readText())
        } catch (e: IOException) {
            LOGGER.info("Could not parse patcher state. Message: ${e.message}")
            PersistentState()
        } catch (e: SerializationException) {
            LOGGER.info("Could not parse patcher state. Message: ${e.message}")
            PersistentState()
        }
    }

    fun savePersistentState(persistentState: PersistentState) {
        patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().writeText(json.encodeToString(persistentState))
    }
}
