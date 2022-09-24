package de.darkatra.bfme2.ui

import com.arkivanov.decompose.value.MutableValue
import com.arkivanov.decompose.value.reduce
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.persistence.PersistenceService
import de.darkatra.bfme2.persistence.PersistentState
import de.darkatra.bfme2.util.StringUtils

class UpdaterModel : PatchProgressListener {

    val state = PersistenceService.loadPersistentState().let {
        MutableValue(
            State(
                hdEditionEnabled = it.hdEditionEnabled,
                trayIconEnabled = it.trayIconEnabled
            )
        )
    }

    override fun onPatchStarted() {
        setProgress(INDETERMINATE_PROGRESS, "Downloading patchlist...")
    }

    override fun onDeletingObsoleteFiles() {
        setProgress(INDETERMINATE_PROGRESS, "Deleting obsolete files...")
    }

    override fun onCalculatingDifferences() {
        setProgress(INDETERMINATE_PROGRESS, "Calculating differences...")
    }

    override fun onPatchProgress(patchProgress: PatchProgress) {
        setProgress(
            patchProgress.currentDisk.toFloat() / patchProgress.totalDisk.toFloat(),
            "${StringUtils.humanReadableSize(patchProgress.currentNetwork)}/${StringUtils.humanReadableSize(patchProgress.totalNetwork)}"
        )
    }

    override fun onPatchFinished() {
        setProgress(1f, "Ready to start the game.")
        setPatchInProgress(false)
        setPatchedOnce(true)
    }

    fun setVisible(isVisible: Boolean) {
        state.reduce { it.copy(isVisible = isVisible) }
    }

    fun setNewVersionAvailable(newVersionAvailable: Boolean) {
        state.reduce { it.copy(newVersionAvailable = newVersionAvailable) }
    }

    fun setSelfUpdateInProgress(selfUpdateInProgress: Boolean) {
        state.reduce { it.copy(selfUpdateInProgress = selfUpdateInProgress) }
    }

    fun setPatchInProgress(patchInProgress: Boolean) {
        state.reduce { it.copy(patchInProgress = patchInProgress) }
    }

    fun setPatchedOnce(patchedOnce: Boolean) {
        state.reduce { it.copy(patchedOnce = patchedOnce) }
    }

    fun setGameRunning(gameRunning: Boolean) {
        state.reduce { it.copy(gameRunning = gameRunning) }
    }

    fun setProgress(progress: Float, progressText: String) {
        state.reduce { it.copy(progress = progress, progressText = progressText) }
    }

    fun setHdEditionEnabled(hdEditionEnabled: Boolean) {
        state.reduce { it.copy(hdEditionEnabled = hdEditionEnabled) }
        updatePersistentState()
    }

    fun setTrayIconEnabled(trayIconEnabled: Boolean) {
        state.reduce { it.copy(trayIconEnabled = trayIconEnabled, isVisible = true) }
        updatePersistentState()
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

        val newVersionAvailable: Boolean = false,
        val selfUpdateInProgress: Boolean = false,

        val patchInProgress: Boolean = false,
        val patchedOnce: Boolean = false,
        val gameRunning: Boolean = false,

        val progress: Float = 0f,
        val progressText: String = "Waiting for user input.",

        val hdEditionEnabled: Boolean = false,
        val trayIconEnabled: Boolean = false
    )
}
