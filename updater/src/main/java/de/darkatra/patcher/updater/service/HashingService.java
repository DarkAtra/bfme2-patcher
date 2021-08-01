package de.darkatra.patcher.updater.service;

import org.bouncycastle.jcajce.provider.digest.SHA3;
import org.springframework.stereotype.Service;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
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
	public Optional<String> getSHA3Checksum(final File file) throws IOException, InterruptedException {
		if (file.isFile()) {
			return Optional.of(getSHA3Checksum(new FileInputStream(file)));
		}
		return Optional.empty();
	}

	/**
	 * Create a base64 encoded sha3-checksum for the given InputStream content.
	 *
	 * @param inputStream the InputStream
	 *
	 * @return the checksum
	 *
	 * @throws IOException if there was an error reading the InputStream
	 */
	public String getSHA3Checksum(final InputStream inputStream) throws IOException, InterruptedException {
		final SHA3.Digest256 sha3Digest = new SHA3.Digest256();
		try (final BufferedInputStream bufferedInputStream = new BufferedInputStream(inputStream)) {
			final byte[] buffer = new byte[1024];
			for (int currentChar; (currentChar = bufferedInputStream.read(buffer)) != -1; ) {
				if (Thread.currentThread().isInterrupted()) {
					throw new InterruptedException("Hashing thread was stopped.");
				}
				sha3Digest.update(buffer, 0, currentChar);
			}
		}
		final byte[] messageDigestBytes = sha3Digest.digest();
		final Base64.Encoder base64Encoder = Base64.getEncoder();
		return new String(base64Encoder.encode(messageDigestBytes), StandardCharsets.UTF_8);
	}
}
