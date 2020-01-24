package de.darkatra.patcher.updater.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Compression;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.service.model.LatestUpdater;
import de.darkatra.patcher.updater.service.model.Packet;
import de.darkatra.patcher.updater.service.model.Patch;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleLongProperty;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import javax.validation.ValidationException;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.function.Consumer;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class PatchService {

	private final Context context;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final ObjectMapper objectMapper;
	private final UpdaterProperties updaterProperties;

	public void patch(final PatchEventListener patchEventListener) throws IOException, ValidationException, InterruptedException {

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
		//		boolean isUpdateForUpdaterRequired = isUpdateForUpdaterRequired(patch);
		//		patchEventListener.onUpdaterNeedsUpdate(isUpdateForUpdaterRequired);
		//		if (isUpdateForUpdaterRequired) {
		//			final LongProperty curProgress = new SimpleLongProperty(0);
		//
		//			downloadPacket(
		//				patch.getLatestUpdater().getLocation(),
		//				getCurrentPatcherJarPath().toString(),
		//				Compression.NONE,
		//				progress -> {
		//					curProgress.setValue(curProgress.getValue() + progress);
		//					patchEventListener.onPatchProgressChange(curProgress.getValue(), patch.getLatestUpdater().getPacketSize());
		//				}
		//			);
		//
		//			patchEventListener.onValidatingPacket();
		//
		//			validateDownloadedPacket(patch.getLatestUpdater().getLocation(), getCurrentPatcherJarPath().toString());
		//
		//			// exit
		//			ProcessUtils.run("move", patch.getLatestUpdater().getLocation(), getCurrentPatcherJarPath().toString());
		//			Platform.exit();
		//			return;
		//		}

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

			downloadPacket(packet, progress -> {
				curProgress.setValue(curProgress.getValue() + progress);
				patchEventListener.onPatchProgressChange(curProgress.getValue(), totalPatchSize);
			});

			patchEventListener.onValidatingPacket();

			validateDownloadedPacket(packet);
		}
		patchEventListener.postPacketsDownload();

		patchEventListener.onPatchDone();
	}

	private void downloadPacket(final Packet packet, final Consumer<Integer> listener) throws InterruptedException, IOException {

		backupExistingFileIfRequired(packet);

		downloadPacket(packet.getSrc(), packet.getDest(), packet.getCompression(), listener);
	}

	private void downloadPacket(final String src, final String dest, final Compression compression, final Consumer<Integer> listener)
		throws InterruptedException, IOException {

		final boolean succeededToDownloadFile = downloadService.downloadFile(src, dest, compression == Compression.ZIP, listener);
		if (!succeededToDownloadFile) {
			throw new IOException(String.format("Could not download '%s'.", src));
		}
	}

	private void validateDownloadedPacket(final Packet packet) throws IOException, InterruptedException {

		validateDownloadedPacket(packet.getDest(), packet.getChecksum());
	}

	private void validateDownloadedPacket(final String dest, final String checksum) throws IOException, InterruptedException {

		if (!hashingService.getSHA3Checksum(new File(dest))
			.map(localChecksum -> localChecksum.equals(checksum))
			.orElse(false)) {

			throw new ValidationException(String.format("The checksum of local file '%s' does not match the servers checksum.", dest));
		}
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

		final Patch returnPatch = new Patch();
		final String prefix = "${";
		final String suffix = "}";

		String latestUpdaterLocation = patch.getLatestUpdater().getLocation();
		for (final Map.Entry<String, String> entry : context.entrySet()) {
			final String key = entry.getKey();
			final String value = entry.getValue();
			latestUpdaterLocation = latestUpdaterLocation.replace(prefix + key + suffix, value);
		}
		returnPatch.setLatestUpdater(new LatestUpdater(patch.getLatestUpdater()).setLocation(latestUpdaterLocation));

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
					.setCompression(packet.getCompression())
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

	private void checkIfIsInterrupted() throws InterruptedException {
		if (Thread.currentThread().isInterrupted()) {
			throw new InterruptedException("Patching thread was interrupted.");
		}
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

	private void backupExistingFileIfRequired(final Packet packet) throws IOException {

		final Path pathToFile = Paths.get(packet.getDest());
		if (Paths.get(packet.getDest()).toFile().exists() && packet.isBackupExisting()) {
			Files.move(pathToFile, Paths.get((String.format("%s%s.bak", pathToFile.getFileName().toString(), Instant.now().toString()))));
		}
	}

	private boolean isUpdateForUpdaterRequired(final Patch patch) throws IOException, InterruptedException {

		final Optional<String> fileChecksum = hashingService.getSHA3Checksum(getCurrentPatcherJarPath().toFile());
		return fileChecksum.isEmpty() || !fileChecksum.get().equals(patch.getLatestUpdater().getChecksum());
	}

	private Path getCurrentPatcherJarPath() {
		return Paths.get(context.get("patcherUserDir"), updaterProperties.getUpdaterJarName()).normalize();
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
					throw new IOException(String.format("Could not delete the file: '%s'.", f.getAbsolutePath()));
				}
			}
		}
	}
}
