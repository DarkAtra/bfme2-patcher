package de.darkatra.patcher.updatebuilder.gui.controller;

import de.darkatra.patcher.config.ContextConfig;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TextField;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.GridPane;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.util.Collection;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

@Slf4j
public class MainWindowController {
	private HashingService hashingService;
	private ContextConfig contextConfig;

	public void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		this.hashingService = applicationContext.getBean(HashingService.class);
		this.contextConfig = applicationContext.getBean(ContextConfig.class);
	}

	@FXML
	private Button addFilesButton, addDirectoryButton, removeFilesButton, createPatchlistButton, printPatchlistButton, confirmVersionButton, cancelVersionButton;
	@FXML
	private ListView<File> listView;
	@FXML
	private TextField versionTextField;
	@FXML
	private GridPane versionTextFieldButtons;
	@FXML
	private Label invalidInputLabel;

	private final Pattern versionRegexPattern = Pattern.compile("(\\d+)\\.(\\d+)\\.(\\d+)");
	private GUIApplication guiApplication;
	private Version version;

	@FXML
	public void initialize() {
		addFilesButton.setOnMouseClicked(event->{
			final FileChooser fc = new FileChooser();
			fc.setInitialDirectory(new File("."));
			fc.setTitle("Select a File");
			final List<File> files = fc.showOpenMultipleDialog(guiApplication.getStage());
			if(files != null) {
				listView.getItems().addAll(files);
			}
		});

		addDirectoryButton.setOnMouseClicked(event->{
			final DirectoryChooser dc = new DirectoryChooser();
			dc.setInitialDirectory(new File((".")));
			dc.setTitle("Select a directory");
			final File directory = dc.showDialog(guiApplication.getStage());
			if(directory != null) {
				final Collection<File> files = FileUtils.listFiles(directory, null, true);
				listView.getItems().addAll(files);
			}
		});

		removeFilesButton.setOnMouseClicked(event->{
			final List<File> toRemove = listView.getSelectionModel().getSelectedItems();
			if(toRemove != null) {
				listView.getItems().removeAll(toRemove);
				listView.getSelectionModel().clearSelection();
			}
		});

		createPatchlistButton.setOnMouseClicked(event->{

		});

		printPatchlistButton.setOnMouseClicked(event->{
			try {
				final PrintedPatchWindowController printedPatchWindowController = guiApplication.showPrintedPatch();
				printedPatchWindowController.setPrintedPatchWindowArea(buildPatchFromList());
			} catch(IOException e) {
				GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application Error", "Could not create Patchwindow");
			}
		});

		listView.getSelectionModel().setSelectionMode(SelectionMode.MULTIPLE);
		listView.setItems(FXCollections.observableArrayList());
		listView.setOnDragOver(event->{
			final Dragboard dragboard = event.getDragboard();
			if(dragboard.hasFiles()) {
				event.acceptTransferModes(TransferMode.LINK);
			}
		});
		listView.setOnDragDropped(event->{
			final Dragboard db = event.getDragboard();
			if(db.hasFiles()) {
				listView.getItems().addAll(db.getFiles());
			}
			event.setDropCompleted(true);
			event.consume();
		});

		confirmVersionButton.setOnMouseClicked(event->{
			final String versionText = versionTextField.getText();
			final Optional<Version> validated = validateVersion(versionText);
			if(validated.isPresent()) {
				this.version = validated.get();
				versionTextField.setText(this.version.toString());
				hideTextFieldButtons();
				hideInvalidLabel();
			} else {
				invalidInputLabel.setVisible(true);
			}
		});

		cancelVersionButton.setOnMouseClicked(event->{
			hideTextFieldButtons();
			hideInvalidLabel();
			versionTextField.clear();
		});

		versionTextField.setOnMouseClicked(event->{
			hideInvalidLabel();
			versionTextFieldButtons.setManaged(true);
		});

		versionTextFieldButtons.visibleProperty().bind(versionTextFieldButtons.managedProperty());
		hideTextFieldButtons();
	}

	private Optional<Version> validateVersion(String toValidate) {
		final Matcher matcher = versionRegexPattern.matcher(toValidate);
		if(matcher.matches()) {
			final int major = Integer.parseInt(matcher.group(1));
			final int minor = Integer.parseInt(matcher.group(2));
			final int build = Integer.parseInt(matcher.group(3));
			return Optional.of(new Version(major, minor, build));
		} else {
			return Optional.empty();
		}
	}

	private void hideTextFieldButtons() {
		versionTextFieldButtons.setManaged(false);
	}

	private void hideInvalidLabel() {
		invalidInputLabel.setVisible(false);
	}

	private Patch buildPatchFromList() {
		final Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
		final Patch patch = new Patch(new Version(0, 0, 1));
		patch.getPackets().addAll(listView.getItems().stream()
				.map(file->{
					try {
						final Optional<String> sha3Checksum = hashingService.getSHA3Checksum(file);
						if(sha3Checksum.isPresent()) {
							final Path relativePathToCWD = currentDir.relativize(file.toPath()).normalize();
							// nur dateien im arbeitsverzeichnis
							// Pfad im applicationcontext nachschauen des dst
							// configurationconfig
							// ersetze c:user/../desktop mit %(desktop) aus app config
							return new Packet("${serverUrl}/" + relativePathToCWD.toString(), "${flyffDir}/" + relativePathToCWD.toFile().getPath(), file.length(), LocalDateTime.now(), sha3Checksum.get(), false);
						}
					} catch(IOException | InterruptedException e) {
						if(e instanceof IOException) {
							GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application Error", "Could not build Patchlist.").showAndWait();
						}
					}
					return null;
				})
				.filter(Objects::nonNull)
				.collect(Collectors.toList()));
		return patch;
	}

	public void setGuiApplication(GUIApplication guiApplication) {
		this.guiApplication = guiApplication;
	}
}
