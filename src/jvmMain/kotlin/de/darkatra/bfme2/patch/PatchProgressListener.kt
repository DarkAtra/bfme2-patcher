package de.darkatra.bfme2.patch

interface PatchProgressListener {

    fun onPatchStarted()

    fun deletingObsoleteFiles()

    fun calculatingDifferences()

    fun onPatchProgress(patchProgress: PatchProgress)

    fun validatingPacket()

    fun onPatchFinished()
}
