package de.darkatra.patcher.updatebuilder.gui.controller;

import de.darkatra.patcher.updatebuilder.HashingService;
import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import de.darkatra.patcher.updatebuilder.model.Context;
import de.darkatra.patcher.updatebuilder.model.Packet;
import de.darkatra.patcher.updatebuilder.model.Patch;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.springframework.lang.NonNull;
import org.springframework.util.FileCopyUtils;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.Collection;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;
import java.util.zip.GZIPOutputStream;

@Slf4j
public class MainWindowController {

	private final HashingService hashingService = new HashingService();

	@FXML
	private Button addFilesButton, addDirectoryButton, removeFilesButton, createPatchlistButton, printPatchlistButton, gzipButton;
	@FXML
	private ListView<File> listView;

	private GUIApplication guiApplication;

	@FXML
	public void initialize() {

		addFilesButton.setOnMouseClicked(event -> {
			final FileChooser fc = new FileChooser();
			fc.setInitialDirectory(new File("."));
			fc.setTitle("Select a File");
			final List<File> files = fc.showOpenMultipleDialog(guiApplication.getStage());
			if (files != null) {
				listView.getItems().addAll(files);
			}
		});

		addDirectoryButton.setOnMouseClicked(event -> {
			final DirectoryChooser dc = new DirectoryChooser();
			dc.setInitialDirectory(new File((".")));
			dc.setTitle("Select a directory");
			final File directory = dc.showDialog(guiApplication.getStage());
			if (directory != null) {
				final Collection<File> files = FileUtils.listFiles(directory, null, true);
				listView.getItems().addAll(files);
			}
		});

		removeFilesButton.setOnMouseClicked(event -> {
			final List<File> toRemove = listView.getSelectionModel().getSelectedItems();
			if (toRemove != null) {
				listView.getItems().removeAll(toRemove);
				listView.getSelectionModel().clearSelection();
			}
		});

		createPatchlistButton.setOnMouseClicked(event -> {
			// TODO: generate patchlist and upload file to ftp
		});

		printPatchlistButton.setOnMouseClicked(event -> {
			try {
				final PrintedPatchWindowController printedPatchWindowController = guiApplication.showPrintedPatch();
				printedPatchWindowController.setPrintedPatchWindowArea(buildPatchFromList());
			} catch (IOException e) {
				log.error("Could not create Patchwindow", e);
				GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application Error", "Could not create Patchwindow");
			}
		});

		gzipButton.setOnMouseClicked(event -> listView.getItems()
			.forEach(file -> {
				try (final GZIPOutputStream out = new GZIPOutputStream(new FileOutputStream(file.getAbsolutePath() + ".gz"));
					 final FileInputStream in = new FileInputStream(file)) {
					FileCopyUtils.copy(in, out);
				} catch (IOException e) {
					e.printStackTrace();
				}
			}));

		listView.getSelectionModel().setSelectionMode(SelectionMode.MULTIPLE);
		listView.setItems(FXCollections.observableArrayList());
		listView.setOnDragOver(event -> {
			final Dragboard dragboard = event.getDragboard();
			if (dragboard.hasFiles()) {
				event.acceptTransferModes(TransferMode.LINK);
			}
		});
		listView.setOnDragDropped(event -> {
			final Dragboard db = event.getDragboard();
			if (db.hasFiles()) {
				listView.getItems().addAll(db.getFiles());
			}
			event.setDropCompleted(true);
			event.consume();
		});
	}

	private Patch buildPatchFromList() {
		final Patch patch = new Patch();
		patch.getPackets().addAll(listView.getItems().stream()
			.map(file -> {
				try {
					final Optional<String> sha3Checksum = hashingService.getSHA3Checksum(file);
					if (sha3Checksum.isPresent()) {
						return new Packet()
							.setSrc(file.getAbsolutePath())
							.setDest(file.getAbsolutePath())
							.setPacketSize(file.length())
							.setDateTime(Instant.now())
							.setChecksum(sha3Checksum.get())
							.setBackupExisting(false);
					}
				} catch (IOException e) {
					GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application Error", "Could not build Patchlist.").showAndWait();
				} catch (InterruptedException ignored) {
				}
				return null;
			})
			.filter(Objects::nonNull)
			.collect(Collectors.toList()));
		return generateContextForPatch(getDevContext(), patch);
	}

	/*
	 * TODO: Lets say we have a context containing:
	 * <key>		<value>
	 * bfme2		/test/bfme2
	 * bfme2ep1		/test/bfme2something-else
	 *
	 * Currently has a bug that causes the generated context to contain '${bfme2}something-else/ instead of '${bfme2ep1}'.
	 * Should be rewritten so that the directories are resolved against the file system and only the most specific directory alias is used.
	 *
	 * Same problem could occur for the following setup:
	 * <key>		<value>
	 * bfme2ep1		/test/rotwk
	 * patcherDir	/test/rotwk/.patcher
	 *
	 * All files in /test/rotwk/.patcher should be resolved via '${patcherDir}/...' instead of '${bfme2ep1}/.patcher/...'.
	 */
	public Patch generateContextForPatch(@NonNull final Context context, @NonNull final Patch patch) {
		final String prefix = "${";
		final String suffix = "}";
		final Patch returnPatch = new Patch();
		final Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
		for (final Packet packet : patch.getPackets()) {
			final Path relativePathToCWD = currentDir.relativize(Paths.get(packet.getSrc())).normalize();
			final String src = prefix + "serverUrl" + suffix + "/" + relativePathToCWD.toString();
			String dest = packet.getDest();
			for (final String key : context.keySet()) {
				try {
					final String value = context.getOrDefault(key, null);
					if (value != null) {
						String temp = dest.replace(value, prefix + key + suffix);
						if (!temp.equalsIgnoreCase(dest)) {
							dest = temp;
							break;
						}
					}
				} catch (final ClassCastException e) {
					log.error("Unexpected Error while generating the context {} for patch {}", context, patch);
					log.debug("ClassCastException", e);
				}
			}

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
		return returnPatch;
	}

	private Context getDevContext() {

		final Context applicationContext = new Context();
		applicationContext.putIfAbsent("serverUrl", "https://darkatra.de");
		applicationContext.putIfAbsent("bfme2HomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2/").normalize().toString());
		applicationContext.putIfAbsent("bfme2UserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2/").normalize().toString());
		applicationContext.putIfAbsent("rotwkHomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/rotwk/").normalize().toString());
		applicationContext.putIfAbsent("patcherUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/rotwk/.patcher").normalize().toString());
		applicationContext.putIfAbsent("rotwkUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirRotwk/").normalize().toString());
		return applicationContext;
	}

	public void setGuiApplication(GUIApplication guiApplication) {
		this.guiApplication = guiApplication;
	}
}
