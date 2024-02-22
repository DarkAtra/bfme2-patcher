package de.darkatra.bfme2.ui

import com.arkivanov.decompose.value.MutableValue
import com.arkivanov.decompose.value.update
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.persistence.PersistenceService
import de.darkatra.bfme2.persistence.PersistentState
import de.darkatra.bfme2.ui.UpdaterModel.State.SelfUpdateState
import de.darkatra.bfme2.util.StringUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.io.path.exists

class UpdaterModel : PatchProgressListener {

    val state = PersistenceService.loadPersistentState().let {
        MutableValue(
            State(
                hdEditionEnabled = it.hdEditionEnabled,
                trayIconEnabled = it.trayIconEnabled,
                hookEnabled = UpdaterContext.hasExpansionDebugger,
                hookingSupported = UpdaterContext.ifeoHome.exists()
            )
        )
    }

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
        state.update { it.copy(isVisible = isVisible) }
    }

    fun setSelfUpdateState(selfUpdateState: SelfUpdateState) {
        state.update { it.copy(selfUpdateState = selfUpdateState) }
    }

    fun setSelfUpdateInProgress(selfUpdateInProgress: Boolean) {
        state.update { it.copy(selfUpdateInProgress = selfUpdateInProgress) }
    }

    fun setPatchInProgress(patchInProgress: Boolean) {
        state.update { it.copy(patchInProgress = patchInProgress) }
    }

    fun setPatchedOnce(patchedOnce: Boolean) {
        state.update { it.copy(patchedOnce = patchedOnce) }
    }

    fun setGameRunning(gameRunning: Boolean) {
        state.update { it.copy(gameRunning = gameRunning) }
    }

    fun setProgress(progress: Float, progressText: String) {
        state.update { it.copy(progress = progress, progressText = progressText) }
    }

    fun setHdEditionEnabled(hdEditionEnabled: Boolean) {
        state.update { it.copy(hdEditionEnabled = hdEditionEnabled) }
        updatePersistentState()
    }

    fun setTrayIconEnabled(trayIconEnabled: Boolean) {
        state.update { it.copy(trayIconEnabled = trayIconEnabled, isVisible = true) }
        updatePersistentState()
    }

    fun setHookEnabled(hookEnabled: Boolean) {
        state.update { it.copy(hookEnabled = hookEnabled) }
    }

    private fun updatePersistentState() {
        PersistenceService.savePersistentState(
            PersistentState(
                hdEditionEnabled = state.value.hdEditionEnabled,
                trayIconEnabled = state.value.trayIconEnabled,
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
