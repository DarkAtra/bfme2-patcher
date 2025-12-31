package de.darkatra.bfme2.ui

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.persistence.PersistenceService
import de.darkatra.bfme2.persistence.PersistentState
import de.darkatra.bfme2.registry.RegistryService
import de.darkatra.bfme2.ui.UpdaterModel.State.ErrorDetails
import de.darkatra.bfme2.ui.UpdaterModel.State.SelfUpdateState
import de.darkatra.bfme2.util.StringUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withContext
import org.jetbrains.skiko.OS
import org.jetbrains.skiko.hostOs
import java.util.logging.Level

class UpdaterModel : PatchProgressListener {

    private val _state: MutableStateFlow<State> = PersistenceService.loadPersistentState().let {
        configureLogLevel(it.debugModeEnabled)
        MutableStateFlow(
            State(
                hdEditionEnabled = it.hdEditionEnabled,
                timerEnabled = it.timerEnabled,
                skipIntroEnabled = it.skipIntroEnabled,
                newMusicEnabled = it.newMusicEnabled,
                patch202Enabled = it.patch202Enabled,
                modEnabled = it.modEnabled,
                trayIconEnabled = it.trayIconEnabled,
                hookEnabled = hostOs == OS.Windows && RegistryService.hasExpansionDebugger(),
                debugModeEnabled = it.debugModeEnabled,
            )
        )
    }
    val state: StateFlow<State> = _state.asStateFlow()

    override suspend fun onPatchStarted() = withContext(Dispatchers.Main.immediate) {
        setProgress(INDETERMINATE_PROGRESS, "Downloading patchlist...")
    }

    override suspend fun onRequirementsNotMet() = withContext(Dispatchers.Main.immediate) {
        setProgress(0f, "Unsupported version '${UpdaterContext.applicationVersion}'. Please update the launcher.")
        _state.update { it.copy(requirementsMet = false) }
        setPatchInProgress(false)
    }

    override suspend fun onRestoringFiles() = withContext(Dispatchers.Main.immediate) {
        setProgress(INDETERMINATE_PROGRESS, "Restoring inactive features...")
    }

    override suspend fun onDeletingObsoleteFiles() = withContext(Dispatchers.Main.immediate) {
        setProgress(INDETERMINATE_PROGRESS, "Deleting obsolete files...")
    }

    override suspend fun onCalculatingDifferences() = withContext(Dispatchers.Main.immediate) {
        setProgress(INDETERMINATE_PROGRESS, "Calculating differences...")
    }

    override suspend fun onPatchProgress(patchProgress: PatchProgress) = withContext(Dispatchers.Main.immediate) {
        setProgress(
            patchProgress.currentDisk.toFloat() / patchProgress.totalDisk.toFloat(),
            "${StringUtils.humanReadableSize(patchProgress.currentNetwork)}/${StringUtils.humanReadableSize(patchProgress.totalNetwork)}" +
                " (${StringUtils.humanReadableSize(patchProgress.currentDisk)}/${StringUtils.humanReadableSize(patchProgress.totalDisk)})"
        )
    }

    override suspend fun onApplyFeatures() {
        setProgress(INDETERMINATE_PROGRESS, "Applying active features...")
    }

    override suspend fun onPatchFinished() = withContext(Dispatchers.Main.immediate) {
        setProgress(1f, "Ready to start the game.")
        setPatchInProgress(false)
        setPatchedOnce(true)
    }

    fun setVisible(isVisible: Boolean) {
        _state.update { it.copy(isVisible = isVisible) }
    }

    fun setErrorDetails(errorDetails: ErrorDetails?) {
        if (errorDetails != null) {
            LOGGER.log(Level.SEVERE, errorDetails.message, errorDetails.cause)
        }
        _state.update { it.copy(errorDetails = errorDetails) }
    }

    fun setSelfUpdateState(selfUpdateState: SelfUpdateState) {
        _state.update { it.copy(selfUpdateState = selfUpdateState) }
    }

    fun setSelfUpdateInProgress(selfUpdateInProgress: Boolean) {
        _state.update { it.copy(selfUpdateInProgress = selfUpdateInProgress) }
    }

    fun setPatchInProgress(patchInProgress: Boolean) {
        _state.update { it.copy(patchInProgress = patchInProgress) }
    }

    fun setPatchedOnce(patchedOnce: Boolean) {
        _state.update { it.copy(patchedOnce = patchedOnce) }
    }

    fun setGameRunning(gameRunning: Boolean) {
        _state.update { it.copy(gameRunning = gameRunning) }
    }

    fun setProgress(progress: Float, progressText: String) {
        _state.update { it.copy(progress = progress, progressText = progressText) }
    }

    fun setHdEditionEnabled(hdEditionEnabled: Boolean) {
        _state.update { it.copy(hdEditionEnabled = hdEditionEnabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setTimerEnabled(timerEnabled: Boolean) {
        _state.update { it.copy(timerEnabled = timerEnabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setSkipIntroEnabled(skipIntroEnabled: Boolean) {
        _state.update { it.copy(skipIntroEnabled = skipIntroEnabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setNewMusicEnabled(newMusicEnabled: Boolean) {
        _state.update { it.copy(newMusicEnabled = newMusicEnabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setPatch202Enabled(patch202Enabled: Boolean) {
        _state.update { it.copy(patch202Enabled = patch202Enabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setModEnabled(modEnabled: Boolean) {
        _state.update { it.copy(modEnabled = modEnabled, patchedOnce = false) }
        updatePersistentState()
    }

    fun setTrayIconEnabled(trayIconEnabled: Boolean) {
        _state.update { it.copy(trayIconEnabled = trayIconEnabled, isVisible = true) }
        updatePersistentState()
    }

    fun setHookEnabled(hookEnabled: Boolean) {
        _state.update { it.copy(hookEnabled = hookEnabled) }
    }

    fun setDebugModeEnabled(debugModeEnabled: Boolean) {
        configureLogLevel(debugModeEnabled)
        _state.update { it.copy(debugModeEnabled = debugModeEnabled) }
        updatePersistentState()
    }

    private fun configureLogLevel(debugModeEnabled: Boolean) {
        LOGGER.level = when {
            debugModeEnabled -> Level.FINE
            else -> Level.INFO
        }
    }

    private fun updatePersistentState() {
        PersistenceService.savePersistentState(
            PersistentState(
                hdEditionEnabled = _state.value.hdEditionEnabled,
                timerEnabled = _state.value.timerEnabled,
                skipIntroEnabled = _state.value.skipIntroEnabled,
                newMusicEnabled = _state.value.newMusicEnabled,
                patch202Enabled = _state.value.patch202Enabled,
                modEnabled = _state.value.modEnabled,
                trayIconEnabled = _state.value.trayIconEnabled,
                debugModeEnabled = _state.value.debugModeEnabled
            )
        )
    }

    data class State(
        val isVisible: Boolean = true,
        val errorDetails: ErrorDetails? = null,

        val selfUpdateState: SelfUpdateState = SelfUpdateState.UNKNOWN,
        val selfUpdateInProgress: Boolean = false,

        val requirementsMet: Boolean = true,
        val patchInProgress: Boolean = false,
        val patchedOnce: Boolean = false,
        val gameRunning: Boolean = false,

        val progress: Float = 0f,
        val progressText: String = "Waiting for user input.",

        val hdEditionEnabled: Boolean = false,
        val timerEnabled: Boolean = false,
        val skipIntroEnabled: Boolean = false,
        val newMusicEnabled: Boolean = false,
        val patch202Enabled: Boolean = true,
        val modEnabled: Boolean = true,

        val trayIconEnabled: Boolean = false,
        val hookEnabled: Boolean = false,
        val debugModeEnabled: Boolean = false,
    ) {

        enum class SelfUpdateState {
            UNKNOWN,
            UP_TO_DATE,
            OUTDATED
        }

        data class ErrorDetails(
            val title: String = "Unexpected error",
            val message: String,
            val cause: Throwable? = null
        )
    }
}
