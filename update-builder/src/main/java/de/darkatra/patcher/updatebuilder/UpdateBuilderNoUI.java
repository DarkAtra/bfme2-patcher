package de.darkatra.patcher.updatebuilder;

import static de.darkatra.patcher.updatebuilder.UpdateBuilderNoUI.Directory.APPDATA_DIR_NAME;
import static de.darkatra.patcher.updatebuilder.UpdateBuilderNoUI.Directory.BMFE2_DIR_NAME;
import static de.darkatra.patcher.updatebuilder.UpdateBuilderNoUI.Directory.ROTWK_DIR_NAME;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import de.darkatra.patcher.updatebuilder.service.model.Compression;
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
import java.nio.file.Paths;
import java.time.Instant;
import java.util.List;
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
		ROTWK_DIR_NAME("rotwk", "${rotwkHomeDir}");

		private final String name;
		private final String contextVariable;
	}

	private static final HashingService hashingService = new HashingService();

	public static void main(final String[] args) throws IOException, InterruptedException {

		final Patch patch = new Patch();

		for (final Directory directory : List.of(APPDATA_DIR_NAME, BMFE2_DIR_NAME, ROTWK_DIR_NAME)) {
			final Path basePath = Paths.get("./" + directory.name);
			for (final Path filePath : readFilesInDirectory(basePath)) {
				addFilesToPatch(patch, directory, filePath);
			}
		}

		System.out.println(new ObjectMapper().registerModule(new JavaTimeModule()).writeValueAsString(patch));
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
				.setPacketSize(gzipFile.length())
				.setDateTime(Instant.now())
				.setChecksum(hashingService.getSHA3Checksum(filePath.toFile()).get())
				.setBackupExisting(false)
				.setCompression(Compression.ZIP)
		);
	}

	private static File createGzipArchive(final Directory directory, final Path input) throws IOException {

		final File outputFile =
			new File(Paths.get("./output/", Path.of(directory.name).relativize(input).normalize().toString()).normalize().toString() + ".gz");
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
