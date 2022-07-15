package de.darkatra.patcher.updater.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Compression;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.service.model.ObsoleteFile;
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
import java.util.Optional;
import java.util.function.Consumer;

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
		final Optional<Patch> patchOptional = patchOf(patchListContent.get());
		if (patchOptional.isEmpty()) {
			throw new ValidationException("Could not parse the patch data.");
		}

		final Patch patch = patchOptional.get();
		patch.applyContext(context);
		patchEventListener.postReadPatchlist();

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
			return Optional.of(patch);
		} catch (final Exception e) {
			log.error("Failed to parse patch from json.", e);
			return Optional.empty();
		}
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
			Files.move(
				pathToFile,
				Paths.get(pathToFile.getParent().toString(),
					String.format("%s-%s.bak", pathToFile.getFileName().toString(), Instant.now().toString().replace(":", "-")))
			);
		}
	}

	private void deleteFiles(final Patch patch) throws InterruptedException, IOException {

		for (final ObsoleteFile file : patch.getObsoleteFiles()) {
			checkIfIsInterrupted();

			final File f = new File(file.getDest());
			if (f.exists()) {
				if (!f.delete()) {
					throw new IOException(String.format("Could not delete the file: '%s'.", f.getAbsolutePath()));
				}
			}
		}
	}
}
