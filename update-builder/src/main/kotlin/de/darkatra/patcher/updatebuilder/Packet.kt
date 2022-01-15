package de.darkatra.patcher.updatebuilder

import com.fasterxml.jackson.annotation.JsonIgnore
import java.nio.file.Path
import java.time.Instant

data class Packet(
	val src: String,
	val dest: String,
	val packetSize: Long,
	val dateTime: Instant,
	val checksum: String,
	val backupExisting: Boolean,
	val compression: Compression,
	@JsonIgnore
	val gzipPath: Path? = null
)
