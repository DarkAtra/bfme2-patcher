package de.darkatra.patcher;

import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.service.DownloadService;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.service.PatchBuilder;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.util.Optional;

@Slf4j
@Service
public class PatchController {
	private Config config;
	private DownloadService downloadService;
	private HashingService hashingService;
	private PatchBuilder patchBuilder;

	@Autowired
	public PatchController(Config config, DownloadService downloadService, HashingService hashingService, PatchBuilder patchBuilder) {
		this.config = config;
		this.downloadService = downloadService;
		this.hashingService = hashingService;
		this.patchBuilder = patchBuilder;
	}

	public void initializePatch() {
		URL url = null;
		try {
			url = new URL(new URL(config.getServerUrl()), config.getPatchlistPath());
			{
				URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
				url = uri.toURL();
			}
		} catch(MalformedURLException e) {
			log.error("MalformedURLException", e);
		} catch(URISyntaxException e) {
			log.error("URISyntaxException", e);
		}

		if(url != null) {
			final Optional<String> urlContent = downloadService.getURLContent(url.toString());
			if(urlContent.isPresent()) {
				final Optional<Patch> patchOptional = patchBuilder.patchOf(urlContent.get());
				patchOptional.ifPresent(System.out::println);
			}
		}
	}
}