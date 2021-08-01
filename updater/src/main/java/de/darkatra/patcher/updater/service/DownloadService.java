package de.darkatra.patcher.updater.service;

import lombok.extern.slf4j.Slf4j;
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
import java.util.zip.GZIPInputStream;

@Slf4j
@Service
public class DownloadService {

	private static final int BUFFER_SIZE = 1024;

	public Optional<String> getURLContent(final URL src) {
		try {
			log.debug("URL: " + src.toString());
			try (final InputStream in = src.openStream()) {
				return Optional.of(new String(in.readAllBytes(), StandardCharsets.UTF_8));
			}
		} catch (final MalformedURLException e) {
			log.error("URL malformed.", e);
		} catch (final IOException e) {
			log.error("IOException.", e);
		}
		return Optional.empty();
	}

	public boolean downloadFile(final String srcFile, final String destFile, final boolean isCompressed, @Nullable final Consumer<Integer> listener)
		throws InterruptedException {

		if (Thread.currentThread().isInterrupted()) {
			throw new InterruptedException("Downloading thread was interrupted.");
		}

		try {
			URL url = new URL(srcFile);
			{
				URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
				url = uri.toURL();
			}
			log.debug("--- Downloading ---");
			log.debug("From: " + url);
			log.debug("To: " + destFile);
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
			try (final InputStream downloadStream = new BufferedInputStream(getConnectionStream(url, isCompressed));
				 final FileOutputStream fileOut = new FileOutputStream(dest)) {

				final byte[] buffer = new byte[BUFFER_SIZE];
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

	private InputStream getConnectionStream(final URL url, final boolean isCompressed) throws IOException {
		return isCompressed ? new GZIPInputStream(url.openStream()) : url.openStream();
	}
}
