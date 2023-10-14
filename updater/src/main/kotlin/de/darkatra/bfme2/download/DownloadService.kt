package de.darkatra.bfme2.download

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule
import com.fasterxml.jackson.module.kotlin.jacksonMapperBuilder
import de.darkatra.bfme2.patch.Compression
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.apache.commons.io.input.CountingInputStream
import java.net.URI
import java.net.URL
import java.nio.file.Path
import java.util.function.Consumer
import java.util.zip.GZIPInputStream
import kotlin.io.path.exists
import kotlin.io.path.isRegularFile
import kotlin.io.path.outputStream
import kotlin.io.path.pathString
import kotlin.reflect.KClass

object DownloadService {

    private val objectMapper: ObjectMapper = jacksonMapperBuilder().addModule(JavaTimeModule()).build()

    fun <T : Any> getContent(url: URL, clazz: KClass<T>): T {

        val content = url.openStream().bufferedReader().use { it.readText() }

        return objectMapper.readValue(content, clazz.java)
    }

    suspend fun download(src: URL, dest: Path, compression: Compression, progressListener: Consumer<DownloadProgress>? = null) = withContext(Dispatchers.IO) {

        val uri = URI(src.protocol, src.userInfo, src.host, src.port, src.path, src.query, src.ref)

        ensureParentFolderExists(dest)

        if (dest.exists() && !dest.isRegularFile()) {
            error("File is not a regular file: ${dest.pathString}")
        }

        ensureActive()

        val (diskInputStream, networkInputStream) = getDownloadStream(uri.toURL(), compression)
        diskInputStream.use { downloadStream ->
            dest.outputStream().use { fileOutputStream ->

                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var count: Int

                while (downloadStream.read(buffer).also { count = it } != -1) {

                    fileOutputStream.write(buffer, 0, count)

                    ensureActive()

                    progressListener?.accept(
                        DownloadProgress(
                            countDisk = diskInputStream.byteCount,
                            countNetwork = networkInputStream.byteCount
                        )
                    )
                }

                diskInputStream.resetByteCount()
                networkInputStream.resetByteCount()
            }
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
