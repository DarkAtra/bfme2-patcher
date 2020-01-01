package de.darkatra.patcher.service;

import lombok.extern.slf4j.Slf4j;
import org.springframework.lang.NonNull;
import org.springframework.lang.Nullable;
import org.springframework.stereotype.Service;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Optional;
import java.util.function.Consumer;

@Slf4j
@Service
public class DownloadService {
	public Optional<String> getURLContent(@NonNull final String src) {
		try {
			URL url = new URL(src);
			{
				URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
				url = uri.toURL();
			}
			log.debug("URL: " + url.toString());
			try (final InputStream in = url.openStream()) {
				return Optional.of(new String(in.readAllBytes(), StandardCharsets.UTF_8));
			}
		} catch (MalformedURLException | URISyntaxException e) {
			log.error("URL malformed.", e);
		} catch (IOException e) {
			log.error("IOException.", e);
		}
		return Optional.empty();
	}

	public boolean downloadFile(@NonNull final String srcFile, @NonNull final String destFile) throws InterruptedException {
		return downloadFile(srcFile, destFile, null);
	}

	public boolean downloadFile(@NonNull final String srcFile, @NonNull final String destFile, @Nullable final Consumer<Integer> listener)
		throws InterruptedException {
		if (Thread.currentThread().isInterrupted()) {
			throw new InterruptedException("Downloading thread was interrupted.");
		}
		try {
			log.debug("Downloading: " + srcFile);
			log.debug("To: " + destFile);
			URL url = new URL(srcFile);
			{
				URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
				url = uri.toURL();
			}
			log.debug("URL: " + url.toString());
			final File dest = new File(destFile);
			if (!dest.getParentFile().exists()) {
				if (!dest.getParentFile().mkdirs()) {
					return false;
				}
			}
			if (!dest.isFile()) {
				if (!dest.createNewFile()) {
					return false;
				}
			}
			try (final BufferedInputStream downloadStream = new BufferedInputStream(url.openStream());
				 final FileOutputStream fileOut = new FileOutputStream(dest)) {

				final byte[] buffer = new byte[1024];
				int count;
				while ((count = downloadStream.read(buffer, 0, buffer.length)) != -1) {
					fileOut.write(buffer, 0, count);
					if (Thread.currentThread().isInterrupted()) {
						throw new InterruptedException("Downloading thread was interrupted.");
					}
					if (listener != null) {
						listener.accept(count);
					}
				}
			} catch (final InterruptedException e) {
				throw e;
			} catch (final Exception e) {
				log.error("Exception while downloading a file:", e);
				return false;
			}
			return true;
		} catch (final MalformedURLException | URISyntaxException e) {
			log.error("URL malformed.");
			return false;
		} catch (final IOException e) {
			log.error("Exception while creating a file:", e);
			return false;
		}
	}
}
