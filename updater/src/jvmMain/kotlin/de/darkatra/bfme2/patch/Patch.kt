package de.darkatra.bfme2.patch

data class Patch(
    val packets: Set<Packet>,
    val obsoleteFiles: Set<ObsoleteFile>
) : ContextAware {

    val size: Long = packets.sumOf { packet -> packet.size }
    val compressedSize: Long = packets.sumOf { packet -> packet.compressedSize }

    /**
     * Applies the given [Context] to the [Patch], replacing placeholders of both [Packets][Packet] and [ObsoleteFiles][ObsoleteFile] with their actual value.
     * Ideally this operation should only be performed once per [Patch].
     *
     * @param context the [Context] containing values for placeholders
     */
    override fun applyContext(context: Context) {
        packets.forEach { packet ->
            packet.applyContext(context)
        }
        obsoleteFiles.forEach { obsoleteFile ->
            obsoleteFile.applyContext(context)
        }
    }
}
