package de.darkatra.bfme2.patch

interface PatchProgressListener {

    suspend fun onPatchStarted()

    suspend fun onDeletingObsoleteFiles()

    suspend fun onCalculatingDifferences()

    suspend fun onPatchProgress(patchProgress: PatchProgress)

    suspend fun onPatchFinished()
}
