package de.darkatra.bfme2.patch

data class PatchProgress(
    val currentNetwork: Long,
    val currentDisk: Long,
    val totalNetwork: Long,
    val totalDisk: Long,
)
