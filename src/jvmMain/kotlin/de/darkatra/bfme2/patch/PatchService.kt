package de.darkatra.bfme2.patch

import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.net.URI
import java.nio.file.Path
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists
import kotlin.io.path.inputStream

class PatchService(
    private val context: Context,
    private val downloadService: DownloadService = DownloadService(),
    private val hashingService: HashingService = HashingService()
) {

    private var isPatching: Boolean = false

    suspend fun patch() = withContext(Dispatchers.IO) {

        if (isPatching) return@withContext

        ensureActive()

        val patch = downloadService.getContent(URI.create(PatchConstants.PATCH_LIST_URL), Patch::class)
        patch.applyContext(context)

        ensureActive()

        patch.obsoleteFiles.forEach { obsoleteFile ->

            Path.of(obsoleteFile.dest).deleteIfExists()

            ensureActive()
        }

        val differences = calculateDifferences(patch)
        val totalPatchSize = differences.size

        differences.packets.forEach { packet ->

            // TODO download the packet and track the download progress (current and total)

            // TODO validate the packet checksum

            ensureActive()
        }
    }

    private suspend fun calculateDifferences(patch: Patch): Patch = withContext(Dispatchers.IO) {

        val packets = mutableSetOf<Packet>()

        patch.packets.forEach { packet ->

            val destPath = Path.of(packet.dest)

            if (!destPath.exists() || hashingService.calculateSha3Checksum(destPath.inputStream()) != packet.checksum) {
                packets.add(packet)
            }

            ensureActive()
        }

        return@withContext Patch(
            packets = packets,
            obsoleteFiles = patch.obsoleteFiles.toSet()
        )
    }
}
