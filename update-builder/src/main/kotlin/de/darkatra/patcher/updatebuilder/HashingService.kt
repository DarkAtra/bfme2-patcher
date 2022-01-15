package de.darkatra.patcher.updatebuilder

import org.bouncycastle.jcajce.provider.digest.SHA3
import java.io.File
import java.io.FileInputStream
import java.nio.charset.StandardCharsets
import java.util.Base64
import java.util.Optional

object HashingService {

	fun getSHA3Checksum(file: File): Optional<String> {
		if (file.isFile) {
			val sha3Digest = SHA3.Digest256()
			FileInputStream(file).use { fileInputStream ->
				val buffer = ByteArray(1024 * 1024 * 50) // 50 MB
				var currentChar: Int
				while (fileInputStream.read(buffer).also { currentChar = it } != -1) {
					if (Thread.currentThread().isInterrupted) {
						throw InterruptedException("Hashing thread was stopped.")
					}
					sha3Digest.update(buffer, 0, currentChar)
				}
			}
			return Optional.of(String(Base64.getEncoder().encode(sha3Digest.digest()), StandardCharsets.UTF_8))
		}
		return Optional.empty()
	}
}
