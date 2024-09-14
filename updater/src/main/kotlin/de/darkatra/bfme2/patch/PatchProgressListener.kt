package de.darkatra.bfme2.patch

interface PatchProgressListener {

    suspend fun onPatchStarted()

    suspend fun onRequirementsNotMet()

    suspend fun onRestoringFiles()

    suspend fun onDeletingObsoleteFiles()

    suspend fun onCalculatingDifferences()

    suspend fun onPatchProgress(patchProgress: PatchProgress)

    suspend fun onApplyFeatures()

    suspend fun onPatchFinished()
}
