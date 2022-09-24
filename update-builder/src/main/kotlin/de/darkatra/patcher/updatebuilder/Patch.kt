package de.darkatra.patcher.updatebuilder

data class Patch(
    val obsoleteFiles: Set<ObsoleteFile>,
    val packets: Set<Packet>
)
