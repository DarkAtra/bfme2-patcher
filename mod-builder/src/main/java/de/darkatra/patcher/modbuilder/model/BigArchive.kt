package de.darkatra.patcher.modbuilder.model

import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.file.Path

const val HEADER_SIZE = 16

class BigArchive(
	val output: OutputStream,
	val version: BigArchiveVersion = BigArchiveVersion.BIG_F
) {
	private val entries = arrayListOf<BigArchiveEntry>()

	fun addFile(file: Path, name: String) {
		entries.add(BigArchiveEntry(file, name))
	}

	fun writeToDisk() {
		entries.sortWith(Comparator.comparing(BigArchiveEntry::name))

		val tableSize = calculateTableSize()
		val contentSize = calculateContentSize()
		val archiveSize: Long = HEADER_SIZE + tableSize + contentSize
		val dataStart: Int = HEADER_SIZE + tableSize

		output.use {
			writeHeader(output, archiveSize, dataStart)
			writeFileTable(output, dataStart)
			writeFileContent(output)

			output.flush()
		}
	}

	private fun writeHeader(output: OutputStream, archiveSize: Long, dataStart: Int) {
		output.write(
			when (version) {
				BigArchiveVersion.BIG_F -> "BIGF".toByteArray()
				BigArchiveVersion.BIG_4 -> "BIG4".toByteArray()
			}
		)

		output.write(archiveSize.toInt().toUnsignedInt32())
		output.write(entries.size.toUnsignedInt32())
		output.write(dataStart.toUnsignedInt32())
	}

	private fun writeFileTable(output: OutputStream, dataStart: Int) {
		var entryOffset: Long = dataStart.toLong()

		entries.forEach { entry ->
			output.write(entryOffset.toInt().toUnsignedInt32())
			output.write(entry.size.toInt().toUnsignedInt32())
			output.write(entry.name.toByteArray())
			output.write(byteArrayOf(0.toByte()))

			entryOffset += entry.size
		}
	}

	private fun writeFileContent(output: OutputStream) {
		entries.forEach { entry ->
			entry.file.toFile().inputStream().transferTo(output)
		}
	}

	private fun calculateTableSize(): Int {
		// Each entry has 4 bytes for the offset + 4 for size and a null-terminated string
		return entries.fold(0) { acc, entry -> acc + 8 + entry.name.length + 1 }
	}

	private fun calculateContentSize(): Long {
		return entries.fold(0) { acc, entry -> acc + entry.size }
	}
}

fun Int.toUnsignedInt32(): ByteArray = ByteBuffer.allocate(4).putInt(this).array()
