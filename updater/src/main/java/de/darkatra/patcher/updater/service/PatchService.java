package de.darkatra.patcher.updater.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.service.model.Packet;
import de.darkatra.patcher.updater.service.model.Patch;
import de.darkatra.patcher.updater.service.model.Version;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleLongProperty;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import javax.validation.ValidationException;
import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Paths;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class PatchService {

	private final UpdaterProperties updaterProperties;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final Context context;
	private final ObjectMapper objectMapper;

	public void patch(final PatchEventListener patchEventListener) throws IOException, ValidationException, InterruptedException, URISyntaxException {

		checkIfIsInterrupted();

		// download patchlist
		patchEventListener.preDownloadPatchlist();
		final Optional<String> patchListContent = downloadService.getURLContent(updaterProperties.getPatchListUrl());
		patchEventListener.postDownloadPatchlist();

		if (patchListContent.isEmpty()) {
			throw new IOException("Could not download the patch data.");
		}

		checkIfIsInterrupted();

		// parse patchlist
		patchEventListener.preReadPatchlist();
		final Optional<Patch> patchOptional = patchOf(patchListContent.get())
			.map(patch -> applyContextToPatch(context, patch));

		if (patchOptional.isEmpty()) {
			throw new ValidationException("Could not parse the patch data.");
		}

		final Patch patch = patchOptional.get();
		patchEventListener.postReadPatchlist();

		checkIfIsInterrupted();

		// update updater
		boolean isUpdateForUpdaterRequired = isUpdateForUpdaterRequired(patch);
		patchEventListener.onUpdaterNeedsUpdate(isUpdateForUpdaterRequired);
		if (isUpdateForUpdaterRequired) {
			// TODO: communicationService.sendMessage(new RequiresUpdateDto(true));
			return;
		}
		// TODO: communicationService.sendMessage(new RequiresUpdateDto(false));

		checkIfIsInterrupted();

		patchEventListener.preDeleteFiles();
		deleteFiles(patch);
		patchEventListener.postDeleteFiles();

		checkIfIsInterrupted();

		// calculate differences
		patchEventListener.preCalculateDifferences();
		final long totalPatchSize = calculateDifferences(patch);
		patchEventListener.postCalculateDifferences();

		patchEventListener.prePacketsDownload();
		final LongProperty curProgress = new SimpleLongProperty(0);
		for (final Packet packet : patch.getPackets()) {
			checkIfIsInterrupted();

			boolean succeededToDownloadFile = downloadService.downloadFile(
				packet.getSrc(),
				packet.getDest(),
				progress -> {
					curProgress.setValue(curProgress.getValue() + progress);
					patchEventListener.onPatchProgressChange(curProgress.getValue(), totalPatchSize);
				}
			);

			if (!succeededToDownloadFile) {
				throw new IOException(String.format("Could not download %s", packet.getSrc()));
			}

			patchEventListener.onValidatingPacket();

			if (!hashingService.getSHA3Checksum(new File(packet.getDest())).map(checksum -> checksum.equals(packet.getChecksum())).orElse(false)) {
				throw new ValidationException("The checksum specified by the server is not equal to the local checksum.");
			}
		}
		patchEventListener.postPacketsDownload();

		patchEventListener.onPatchDone();
	}

	private Optional<Patch> patchOf(final String json) {

		try {
			final Patch patch = objectMapper.readValue(json, Patch.class);
			patch.getPackets().stream().map(Packet::getDest).forEach(dest -> patch.getFileIndex().add(dest));
			return Optional.of(patch);
		} catch (final Exception e) {
			log.error("Failed to parse patch from json.", e);
			return Optional.empty();
		}
	}

	private Patch applyContextToPatch(final Context context, final Patch patch) {

		final Patch returnPatch = new Patch().setVersion(new Version(patch.getVersion()));
		final String prefix = "${";
		final String suffix = "}";

		for (final Packet packet : patch.getPackets()) {

			String src = packet.getSrc();
			String dest = packet.getDest();

			for (final Map.Entry<String, String> entry : context.entrySet()) {
				final String key = entry.getKey();
				final String value = entry.getValue();
				src = src.replace(prefix + key + suffix, value);
				dest = dest.replace(prefix + key + suffix, value);
			}

			dest = Paths.get(dest).normalize().toString();

			returnPatch.getPackets().add(
				new Packet()
					.setSrc(src)
					.setDest(dest)
					.setPacketSize(packet.getPacketSize())
					.setDateTime(packet.getDateTime())
					.setChecksum(packet.getChecksum())
					.setBackupExisting(packet.isBackupExisting())
			);
		}

		for (String destToRemove : patch.getFileIndex()) {

			for (final Map.Entry<String, String> entry : context.entrySet()) {
				final String key = entry.getKey();
				final String value = entry.getValue();
				destToRemove = destToRemove.replace(prefix + key + suffix, value);
			}

			returnPatch.getFileIndex().add(Paths.get(destToRemove).normalize().toString());
		}

		return returnPatch;
	}

	private long calculateDifferences(final Patch patch) throws InterruptedException, IOException {

		long tempPatchSize = 0L;
		final Iterator<Packet> packets = patch.getPackets().iterator();

		while (packets.hasNext()) {
			checkIfIsInterrupted();

			final Packet packet = packets.next();
			final File localFile = new File(packet.getDest());

			final Optional<String> fileChecksum = hashingService.getSHA3Checksum(localFile);
			if (fileChecksum.isPresent() && fileChecksum.get().equals(packet.getChecksum())) {
				packets.remove();
			} else {
				tempPatchSize += packet.getPacketSize();
			}
		}

		return tempPatchSize;
	}

	private void checkIfIsInterrupted() throws InterruptedException {
		if (Thread.currentThread().isInterrupted()) {
			throw new InterruptedException("Patching thread was interrupted.");
		}
	}

	private boolean isUpdateForUpdaterRequired(final Patch patch) throws URISyntaxException, IOException, InterruptedException {

		final File updaterJar = Paths.get(updaterProperties.getUpdaterJarUrl().toURI()).toFile();
		final Optional<Packet> latestUpdater = patch.getPackets().stream()
			.filter(packet -> packet.getDest().equalsIgnoreCase(updaterJar.getAbsolutePath()))
			.findFirst();

		if (latestUpdater.isPresent()) {
			final Optional<String> fileChecksum = hashingService.getSHA3Checksum(updaterJar);
			return fileChecksum.isPresent() && fileChecksum.get().equals(latestUpdater.get().getChecksum());
		}

		return false;
	}

	private void deleteFiles(final Patch patch) throws InterruptedException, IOException {

		final List<String> filesToDelete = patch.getFileIndex().stream()
			.filter(p -> patch.getPackets().stream().noneMatch(p2 -> p.equals(p2.getDest())))
			.collect(Collectors.toList());

		for (final String file : filesToDelete) {
			checkIfIsInterrupted();

			final File f = new File(file);
			if (f.exists()) {
				if (!f.delete()) {
					throw new IOException(String.format("Could not delete the file: %s", f.getAbsolutePath()));
				}
			}
		}
	}
}
