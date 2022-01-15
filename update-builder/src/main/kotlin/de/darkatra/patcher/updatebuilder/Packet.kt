package de.darkatra.patcher.updatebuilder

import java.time.Instant

data class Packet(
	val src: String,
	val dest: String,
	val packetSize: Long,
	val dateTime: Instant,
	val checksum: String,
	val backupExisting: Boolean,
	val compression: Compression
)
