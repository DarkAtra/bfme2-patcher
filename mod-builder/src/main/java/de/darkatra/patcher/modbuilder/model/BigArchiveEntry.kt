package de.darkatra.patcher.modbuilder.model

import java.nio.file.Path

class BigArchiveEntry(
	val file: Path,
	val name: String
) {
	val size: Long
		get() = file.toFile().length()
}
