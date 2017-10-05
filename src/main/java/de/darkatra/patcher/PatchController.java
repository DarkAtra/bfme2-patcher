package de.darkatra.patcher;

import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.exception.ValidationException;
import de.darkatra.patcher.listener.PatchEventListener;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.service.DownloadService;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.service.PatchService;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleLongProperty;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.util.List;
import java.util.Optional;

@Slf4j
@Service
public class PatchController {
	private Context context;
	private final Config config;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final PatchService patchService;

	@Autowired
	public PatchController(Context context, Config config, DownloadService downloadService, HashingService hashingService, PatchService patchService) {
		this.context = context;
		this.config = config;
		this.downloadService = downloadService;
		this.hashingService = hashingService;
		this.patchService = patchService;
	}

	public void patch(PatchEventListener patchEventListener) throws IOException, URISyntaxException, ValidationException {
		URL url = new URL(new URL(config.getServerUrl()), config.getPatchListPath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}

		final Optional<String> urlContent = downloadService.getURLContent(url.toString());
		patchEventListener.onServerPatchlistDownloaded();
		if(urlContent.isPresent()) {
			final Optional<Patch> patchOptional = patchService.patchOf(urlContent.get());
			if(patchOptional.isPresent()) {
				final Patch patch = patchService.applyContextToPatch(context, patchOptional.get());
				patchEventListener.onServerPatchlistRead();

				// TODO: patcher requires update?
				// patchEventListener.onPatcherNeedsUpdate();

				// TODO: delete files
				// patchEventListener.onFilesDeleted();

				// calculate differences, download, validate
				long tempPatchSize = 0L;
				final List<Packet> packets = patch.getPackets();
				for(int i = 0; i < packets.size(); i++) {
					Packet packet = packets.get(i);
					if(Thread.currentThread().isInterrupted()) {
						return;
					}
					File localFile = new File(packet.getDest());
					final Optional<String> fileChecksum = hashingService.getSHA3Checksum(localFile);
					if(fileChecksum.isPresent() && fileChecksum.get().equals(packet.getChecksum())) {
						packets.remove(packet);
						i--;
					} else {
						tempPatchSize += packet.getPacketSize();
					}
				}
				patchEventListener.onDifferencesCalculated();

				final long totalPatchSize = tempPatchSize;
				for(Packet packet : packets) {
					File localFile = new File(packet.getDest());
					LongProperty curProgress = new SimpleLongProperty(0);
					downloadService.downloadFile(packet.getSrc(), packet.getDest(), progress->{
						curProgress.setValue(curProgress.getValue() + progress);
						patchEventListener.onPatchProgressChanged(curProgress.getValue(), totalPatchSize);
					});
					if(!hashingService.getSHA3Checksum(localFile).map(checksum->checksum.equals(packet.getChecksum())).orElse(false)) {
						throw new ValidationException("The checksum specified by the server is not equal to the the downloaded files checksum.");
					}
				}
				patchEventListener.onPacketsDownloaded();

				patchEventListener.onPatchDone();
			} else {
				throw new ValidationException("Could not parse the patch data.");
			}
		} else {
			throw new IOException("Could not download " + url.toString());
		}
	}
}