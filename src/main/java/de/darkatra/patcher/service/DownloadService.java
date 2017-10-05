package de.darkatra.patcher.service;

import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;
import org.springframework.stereotype.Service;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.util.Optional;
import java.util.function.Consumer;

@Slf4j
@Service
public class DownloadService {
	public Optional<String> getURLContent(@NotNull String src) {
		try {
			URL url = new URL(src);
			{
				URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
				url = uri.toURL();
			}
			log.debug("URL: " + url.toString());
			return Optional.of(IOUtils.toString(url, "UTF-8"));
		} catch(MalformedURLException | URISyntaxException e) {
			log.error("URL malformed.", e);
		} catch(IOException e) {
			log.error("IOException.", e);
		}
		return Optional.empty();
	}

	public boolean downloadFile(@NotNull String srcFile, @NotNull String destFile, @Nullable Consumer<Integer> listener) {
		if(Thread.currentThread().isInterrupted()) {
			return false;
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
			if(!dest.getParentFile().exists()) {
				if(!dest.getParentFile().mkdirs()) {
					return false;
				}
			}
			if(!dest.isFile()) {
				if(!dest.createNewFile()) {
					return false;
				}
			}
			try(final BufferedInputStream downloadStream = new BufferedInputStream(url.openStream()); FileOutputStream fileOut = new FileOutputStream(dest)) {
				final byte buffer[] = new byte[1024];
				int count;
				while((count = downloadStream.read(buffer, 0, buffer.length)) != -1) {
					fileOut.write(buffer, 0, count);
					if(Thread.currentThread().isInterrupted()) {
						return false;
					}
					if(listener != null) {
						listener.accept(count);
					}
				}
			} catch(Exception e) {
				log.error("Exception while downloading a file:", e);
				return false;
			}
			return true;
		} catch(MalformedURLException | URISyntaxException e) {
			log.error("URL malformed.");
			return false;
		} catch(IOException e) {
			log.error("Exception while creating a file:", e);
			return false;
		}
	}
}