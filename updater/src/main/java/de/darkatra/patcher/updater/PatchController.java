package de.darkatra.patcher.updater;

import de.darkatra.patcher.exception.ValidationException;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.communication.RequiresUpdateDto;
import de.darkatra.patcher.properties.Config;
import de.darkatra.patcher.service.CommunicationService;
import de.darkatra.patcher.service.DownloadService;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.service.PatchService;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleLongProperty;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Paths;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

@Slf4j
@Service
public class PatchController {
	private final Context context;
	private final Config config;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final PatchService patchService;
	private final CommunicationService communicationService;

	public PatchController(Context context, Config config, DownloadService downloadService, HashingService hashingService, PatchService patchService, CommunicationService communicationService) {
		this.context = context;
		this.config = config;
		this.downloadService = downloadService;
		this.hashingService = hashingService;
		this.patchService = patchService;
		this.communicationService = communicationService;
	}

	public void patch(PatchEventListener patchEventListener) throws IOException, URISyntaxException, ValidationException, InterruptedException {
		URL url = new URL(new URL(config.getServerUrl()), config.getPatchListPath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}

		if(Thread.currentThread().isInterrupted()) {
			throw new InterruptedException("Patching thread was interrupted.");
		}

		patchEventListener.preDownloadServerPatchlist();
		final Optional<String> urlContent = downloadService.getURLContent(url.toString());
		patchEventListener.postDownloadServerPatchlist();
		if(urlContent.isPresent()) {
			patchEventListener.preReadServerPatchlist();
			final Optional<Patch> patchOptional = patchService.patchOf(urlContent.get());
			if(patchOptional.isPresent()) {
				final Patch patch = patchService.applyContextToPatch(context, patchOptional.get());
				patchEventListener.postReadServerPatchlist();

				// updater needs update
				final String patcherUserDir = config.getPatcherUserDir();
				final File patcherJar = Paths.get(patcherUserDir, config.getUpdaterJarName()).toFile();
				final Optional<Packet> firstUpdater = patch.getPackets().stream().filter(packet->packet.getDest().equalsIgnoreCase(patcherJar.getAbsolutePath())).findFirst();
				if(firstUpdater.isPresent()) {
					final Optional<String> fileChecksum = hashingService.getSHA3Checksum(patcherJar);
					if(fileChecksum.isPresent() && fileChecksum.get().equals(firstUpdater.get().getChecksum())) {
						communicationService.sendMessage(new RequiresUpdateDto(true));
						patchEventListener.onPatcherNeedsUpdate(true);
						return;
					}
				}
				communicationService.sendMessage(new RequiresUpdateDto(false));
				patchEventListener.onPatcherNeedsUpdate(false);

				// delete files
				patchEventListener.preDeleteFiles();
				final List<String> filesToDelete = patch.getFileIndex().stream().filter(p->patch.getPackets().stream().noneMatch(p2->p.equals(p2.getDest()))).collect(Collectors.toList());
				for(String file : filesToDelete) {
					if(Thread.currentThread().isInterrupted()) {
						throw new InterruptedException("Patching thread was interrupted.");
					}
					File f = new File(file);
					if(f.exists()) {
						if(!f.delete()) {
							throw new IOException("Could not delete the file: " + f.getAbsolutePath());
						}
					}
				}
				patchEventListener.postDeleteFiles();

				// calculate differences, download, validate
				patchEventListener.preCalculateDifferences();
				long tempPatchSize = 0L;
				final Iterator<Packet> packets = patch.getPackets().iterator();
				for(Packet packet; packets.hasNext(); ) {
					packet = packets.next();
					if(Thread.currentThread().isInterrupted()) {
						throw new InterruptedException("Patching thread was interrupted.");
					}
					File localFile = new File(packet.getDest());
					final Optional<String> fileChecksum = hashingService.getSHA3Checksum(localFile);
					if(fileChecksum.isPresent() && fileChecksum.get().equals(packet.getChecksum())) {
						packets.remove();
					} else {
						tempPatchSize += packet.getPacketSize();
					}
				}
				patchEventListener.postCalculateDifferences();

				patchEventListener.prePacketsDownload();
				final long totalPatchSize = tempPatchSize;
				final LongProperty curProgress = new SimpleLongProperty(0);
				for(Packet packet : patch.getPackets()) {
					if(Thread.currentThread().isInterrupted()) {
						throw new InterruptedException("Patching thread was interrupted.");
					}
					final File localFile = new File(packet.getDest());
					if(downloadService.downloadFile(packet.getSrc(), packet.getDest(), progress->{
						curProgress.setValue(curProgress.getValue() + progress);
						patchEventListener.onPatchProgressChange(curProgress.getValue(), totalPatchSize);
					})) {
						patchEventListener.onValidatingPacket();
						if(!hashingService.getSHA3Checksum(localFile).map(checksum->checksum.equals(packet.getChecksum())).orElse(false)) {
							throw new ValidationException("The checksum specified by the server is not equal to the the downloaded files checksum.");
						}
					} else {
						throw new IOException("Could not download " + url.toString());
					}
				}
				patchEventListener.postPacketsDownload();

				patchEventListener.onPatchDone();
			} else {
				throw new ValidationException("Could not parse the patch data.");
			}
		} else {
			throw new IOException("Could not download " + url.toString());
		}
	}
}