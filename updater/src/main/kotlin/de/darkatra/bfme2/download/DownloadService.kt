package de.darkatra.bfme2.download

import de.darkatra.bfme2.patch.Compression
import de.darkatra.bfme2.patch.ETag
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Path
import java.time.Instant
import java.util.function.Consumer
import java.util.zip.GZIPInputStream
import kotlin.io.path.exists
import kotlin.io.path.isRegularFile
import kotlin.io.path.outputStream
import kotlin.io.path.pathString

object DownloadService {

    @PublishedApi
    internal val json = Json {
        ignoreUnknownKeys = true
    }

    inline fun <reified T : Any> getContent(url: URL): T {

        val content = url.openStream().bufferedReader().use { it.readText() }

        return json.decodeFromString<T>(content)
    }

    suspend fun download(src: URL, dest: Path, compression: Compression, progressListener: Consumer<DownloadProgress>? = null) = withContext(Dispatchers.IO) {

        ensureParentFolderExists(dest)

        if (dest.exists() && !dest.isRegularFile()) {
            error("File is not a regular file: ${dest.pathString}")
        }

        ensureActive()

        val (diskInputStream, networkInputStream) = getDownloadStream(src, compression)
        diskInputStream.use { downloadStream ->
            dest.outputStream().use { fileOutputStream ->

                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var count: Int

                while (downloadStream.read(buffer).also { count = it } != -1) {

                    fileOutputStream.write(buffer, 0, count)

                    ensureActive()

                    progressListener?.accept(
                        DownloadProgress(
                            countDisk = diskInputStream.count,
                            countNetwork = networkInputStream.count
                        )
                    )

                    diskInputStream.resetCount()
                    networkInputStream.resetCount()
                }
            }
        }
    }

    suspend fun getETag(url: URL): ETag = withContext(Dispatchers.IO) {

        val connection = url.openConnection()
        if (connection !is HttpURLConnection) {
            error("Could not get etag for '$url'.")
        }

        try {
            connection.requestMethod = "HEAD"
            connection.connect()

            val size = connection.contentLengthLong
            val modified = connection.lastModified

            check(size >= 0) { "Missing Content-Length while getting E-Tag for '$url'" }
            check(modified > 0) { "Missing Last-Modified while getting E-Tag for '$url'" }

            return@withContext ETag(
                fileSize = size,
                lastModified = Instant.ofEpochMilli(modified)
            )
        } finally {
            connection.disconnect()
        }
    }

    private fun ensureParentFolderExists(src: Path) {
        val parentFile = src.toFile().parentFile
        if (!parentFile.exists()) {
            check(parentFile.mkdirs()) {
                "Could not create: '${parentFile.absolutePath}'."
            }
        }
    }

    private fun getDownloadStream(url: URL, compression: Compression): Pair<CountingInputStream, CountingInputStream> {

        val networkInputStream = CountingInputStream(url.openStream())

        val contentInputStream = when (compression) {
            Compression.NONE -> networkInputStream
            Compression.GZIP -> GZIPInputStream(networkInputStream)
        }.buffered()

        return CountingInputStream(contentInputStream) to networkInputStream
    }
}
