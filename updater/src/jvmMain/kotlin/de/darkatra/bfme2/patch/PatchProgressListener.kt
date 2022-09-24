package de.darkatra.bfme2.patch

interface PatchProgressListener {

    fun onPatchStarted()

    fun onDeletingObsoleteFiles()

    fun onCalculatingDifferences()

    fun onPatchProgress(patchProgress: PatchProgress)

    fun onPatchFinished()
}
