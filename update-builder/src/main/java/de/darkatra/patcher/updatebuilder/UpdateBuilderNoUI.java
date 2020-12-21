package de.darkatra.patcher.updatebuilder;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import de.darkatra.patcher.updatebuilder.service.model.Compression;
import de.darkatra.patcher.updatebuilder.service.model.ObsoleteFile;
import de.darkatra.patcher.updatebuilder.service.model.Packet;
import de.darkatra.patcher.updatebuilder.service.model.Patch;
import lombok.RequiredArgsConstructor;
import org.apache.commons.io.IOUtils;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.util.zip.GZIPOutputStream;

public class UpdateBuilderNoUI {

	@RequiredArgsConstructor
	enum Directory {
		/**
		 * For example: C:\Users\<User>\AppData\Roaming\Meine Der Herr der Ringe™, Aufstieg des Hexenkönigs™-Dateien\
		 */
		APPDATA_DIR_NAME("appdata", "${rotwkUserDir}"),
		/**
		 * B:\Electronic Arts\Die Schlacht um Mittelerde II\
		 */
		BMFE2_DIR_NAME("bfme2", "${bfme2HomeDir}"),
		/**
		 * B:\Electronic Arts\Aufstieg des Hexenkönigs\
		 */
		ROTWK_DIR_NAME("rotwk", "${rotwkHomeDir}"),
		/**
		 * C:\Users\<User>\AppData\Roaming\.patcher\
		 */
		PATCH_DIR_NAME("patcher", "${patcherUserDir}");

		private final String name;
		private final String contextVariable;
	}

	private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper()
		.findAndRegisterModules()
		.configure(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS, false);
	private static final Path OBSOLETE_FILES_PATH = Path.of("./obsolete-files.json");
	private static final Set<String> FILES_REQUIRING_BACKUP = Set.of("asset.dat", "game.dat");
	private static final HashingService HASHING_SERVICE = new HashingService();

	public static void main(final String[] args) throws IOException, InterruptedException {

		final Patch patch = new Patch();

		final ObsoleteFile[] obsoleteFiles = OBJECT_MAPPER.readValue(OBSOLETE_FILES_PATH.toFile(), ObsoleteFile[].class);
		patch.setObsoleteFiles(Arrays.stream(obsoleteFiles).collect(Collectors.toSet()));

		for (final Directory directory : Directory.values()) {
			final Path basePath = Path.of("./" + directory.name);
			for (final Path filePath : readFilesInDirectory(basePath)) {
				addFilesToPatch(patch, directory, filePath);
			}
		}

		Files.writeString(
			Path.of("./output/version.json"),
			OBJECT_MAPPER.writerWithDefaultPrettyPrinter().writeValueAsString(patch),
			StandardOpenOption.WRITE,
			StandardOpenOption.TRUNCATE_EXISTING,
			StandardOpenOption.CREATE
		);
	}

	private static void addFilesToPatch(final Patch patch, final Directory directory, final Path filePath) throws IOException, InterruptedException {

		final File gzipFile = createGzipArchive(directory, filePath);

		patch.getPackets().add(
			new Packet()
				.setSrc(Path.of("${serverUrl}/bfmemod2/").resolve(
					Path.of(directory.name).relativize(Path.of(filePath.toString() + ".gz"))
				).normalize().toString().replace("\\", "/"))
				.setDest(Path.of(directory.contextVariable).resolve(
					Path.of(directory.name).relativize(filePath)
				).normalize().toString().replace("\\", "/"))
				.setPacketSize(filePath.toFile().length())
				.setDateTime(Instant.now())
				.setChecksum(HASHING_SERVICE.getSHA3Checksum(filePath.toFile())
					.orElseThrow(() -> new IllegalStateException("Could not calculate the hash for: " + filePath.toFile())))
				.setBackupExisting(FILES_REQUIRING_BACKUP.contains(filePath.toFile().getName()))
				.setCompression(Compression.ZIP)
		);
	}

	private static File createGzipArchive(final Directory directory, final Path input) throws IOException {

		final File outputFile = new File(Path.of("./output/", Path.of(directory.name).relativize(input).toString()).normalize().toString() + ".gz");
		outputFile.getParentFile().mkdirs();
		try (final FileInputStream inputStream = new FileInputStream(input.toFile());
			 final GZIPOutputStream outputStream = new GZIPOutputStream(new FileOutputStream(outputFile))) {
			IOUtils.copy(inputStream, outputStream);
		}
		return outputFile;
	}

	private static Set<Path> readFilesInDirectory(final Path directory) throws IOException {
		try (Stream<Path> stream = Files.walk(directory)) {
			return stream
				.filter(Files::isRegularFile)
				.collect(Collectors.toSet());
		}
	}
}
