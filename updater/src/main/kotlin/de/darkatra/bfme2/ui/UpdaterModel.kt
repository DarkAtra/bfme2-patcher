package de.darkatra.bfme2.ui

import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.persistence.PersistenceService
import de.darkatra.bfme2.persistence.PersistentState
import de.darkatra.bfme2.ui.UpdaterModel.State.SelfUpdateState
import de.darkatra.bfme2.util.StringUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withContext
import kotlin.io.path.exists

class UpdaterModel : PatchProgressListener {

    private val _state = PersistenceService.loadPersistentState().let {
        MutableStateFlow(
            State(
                hdEditionEnabled = it.hdEditionEnabled,
                modEnabled = it.modEnabled,
                trayIconEnabled = it.trayIconEnabled,
                hookEnabled = UpdaterContext.hasExpansionDebugger,
                hookingSupported = UpdaterContext.ifeoHome.exists()
            )
        )
    }
    val state = _state.asStateFlow()

    override suspend fun onPatchStarted() = withContext(Dispatchers.Main.immediate) {
        setProgress(INDETERMINATE_PROGRESS, "Downloading patchlist...")
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

    override suspend fun onPatchFinished() = withContext(Dispatchers.Main.immediate) {
        setProgress(1f, "Ready to start the game.")
        setPatchInProgress(false)
        setPatchedOnce(true)
    }

    fun setVisible(isVisible: Boolean) {
        _state.update { it.copy(isVisible = isVisible) }
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
        _state.update { it.copy(hdEditionEnabled = hdEditionEnabled) }
        updatePersistentState()
    }

    fun setModEnabled(modEnabled: Boolean) {
        _state.update { it.copy(modEnabled = modEnabled) }
        updatePersistentState()
    }

    fun setTrayIconEnabled(trayIconEnabled: Boolean) {
        _state.update { it.copy(trayIconEnabled = trayIconEnabled, isVisible = true) }
        updatePersistentState()
    }

    fun setHookEnabled(hookEnabled: Boolean) {
        _state.update { it.copy(hookEnabled = hookEnabled) }
    }

    private fun updatePersistentState() {
        PersistenceService.savePersistentState(
            PersistentState(
                hdEditionEnabled = _state.value.hdEditionEnabled,
                modEnabled = _state.value.modEnabled,
                trayIconEnabled = _state.value.trayIconEnabled,
            )
        )
    }

    data class State(
        val isVisible: Boolean = true,

        val selfUpdateState: SelfUpdateState = SelfUpdateState.UNKNOWN,
        val selfUpdateInProgress: Boolean = false,

        val patchInProgress: Boolean = false,
        val patchedOnce: Boolean = false,
        val gameRunning: Boolean = false,

        val progress: Float = 0f,
        val progressText: String = "Waiting for user input.",

        val hdEditionEnabled: Boolean = false,
        val modEnabled: Boolean = true,
        val trayIconEnabled: Boolean = false,
        val hookEnabled: Boolean = false,
        val hookingSupported: Boolean = false
    ) {

        enum class SelfUpdateState {
            UNKNOWN,
            UP_TO_DATE,
            OUTDATED
        }
    }
}
