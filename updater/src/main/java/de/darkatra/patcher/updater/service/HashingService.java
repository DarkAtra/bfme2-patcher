package de.darkatra.patcher.updater.service;

import org.bouncycastle.jcajce.provider.digest.SHA3;
import org.jetbrains.annotations.NotNull;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.Optional;

@Service
public class HashingService {
	/**
	 * Create a base64 encoded sha3-checksum for the given file.
	 *
	 * @param file the file
	 *
	 * @return the checksum
	 *
	 * @throws IOException if there was an error reading the file
	 */
	public Optional<String> getSHA3Checksum(@NotNull File file) throws IOException, InterruptedException {
		if(file.isFile()) {
			final SHA3.Digest256 sha3Digest = new SHA3.Digest256();
			try(final FileInputStream fileInputStream = new FileInputStream(file)) {
				byte[] buffer = new byte[1024];
				for(int currentChar; (currentChar = fileInputStream.read(buffer)) != -1; ) {
					if(Thread.currentThread().isInterrupted()) {
						throw new InterruptedException("Hashing thread was stopped.");
					}
					sha3Digest.update(buffer, 0, currentChar);
				}
			}
			byte[] messageDigestBytes = sha3Digest.digest();
			final Base64.Encoder base64Encoder = Base64.getEncoder();
			return Optional.of(new String(base64Encoder.encode(messageDigestBytes), StandardCharsets.UTF_8));
		}
		return Optional.empty();
	}
}