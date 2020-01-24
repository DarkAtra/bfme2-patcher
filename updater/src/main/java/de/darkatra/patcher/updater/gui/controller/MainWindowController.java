package de.darkatra.patcher.updater.gui.controller;

import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.OptionFileService;
import de.darkatra.patcher.updater.service.PatchService;
import de.darkatra.patcher.updater.service.RegistryService;
import de.darkatra.patcher.updater.util.ProcessUtils;
import de.darkatra.patcher.updater.util.UIUtils;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ProgressBar;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.core.task.AsyncListenableTaskExecutor;
import org.springframework.core.task.SimpleAsyncTaskExecutor;
import org.springframework.stereotype.Component;

import javax.validation.ValidationException;
import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Path;
import java.util.Optional;

@Slf4j
@Component
@RequiredArgsConstructor
public class MainWindowController implements PatchEventListener {

	private final UpdaterProperties updaterProperties;
	private final PatchService patchService;
	private final OptionFileService optionFileService;
	private final RegistryService registryService;

	@FXML
	private ProgressBar patchProgressBar;
	@FXML
	private Label patchProgressLabel;
	@FXML
	private Button updateButton;
	@FXML
	private Button toggleModButton;
	@FXML
	private MenuItem versionMenuItem;
	@FXML
	private MenuItem fixBfME2MenuItem;
	@FXML
	private MenuItem fixBfME2EP1MenuItem;

	private final AsyncListenableTaskExecutor taskExecutor = new SimpleAsyncTaskExecutor();

	@FXML
	public void initialize() {

		versionMenuItem.setText(updaterProperties.getVersion());

		patchProgressBar.setProgress(0);
		patchProgressLabel.setText("Waiting for user input.");

		updateButton.setOnMouseClicked(event -> {
			if (updateButton.isDisabled()) {
				return;
			}
			updateButton.setDisable(true);

			taskExecutor.submitListenable(() -> {
				patchService.patch(this);
				return true;
			}).completable().whenComplete((patchResult, e) -> {
				if (e != null) {
					Platform.runLater(() -> {
						patchProgressBar.setProgress(0);
						patchProgressLabel.setText("Update failed. Please try again later.");
						getAlertForThrowable(e).show();
						updateButton.setDisable(false);
					});
				}
			});
		});

		toggleModButton.setDisable(true);
		toggleModButton.setOnMouseClicked(event -> {
			// TODO: toggle mod (enable/disable)
		});

		fixBfME2MenuItem.setOnAction(event -> registryService.findBfME2UserDirectory().ifPresent(rotWKUserDirectory -> writeDefaultOptions(
			rotWKUserDirectory.toFile(),
			UIUtils.alert(
				Alert.AlertType.INFORMATION,
				"Success",
				"Fixed BfME 2",
				"The options.ini file was created successfully."
			),
			UIUtils.alert(
				Alert.AlertType.ERROR,
				"Application error",
				"Could not fix BfME 2",
				"There was an error writing the default options.ini"
			)
		)));

		fixBfME2EP1MenuItem.setOnAction(event -> registryService.findBfME2RotWKUserDirectory().ifPresent(rotWKUserDirectory -> writeDefaultOptions(
			rotWKUserDirectory.toFile(),
			UIUtils.alert(
				Alert.AlertType.INFORMATION,
				"Success",
				"Fixed BfME 2 RotWK",
				"The options.ini file was created successfully."
			),
			UIUtils.alert(
				Alert.AlertType.ERROR,
				"Application error",
				"Could not fix BfME 2 RotWK",
				"There was an error writing the default options.ini"
			)
		)));
	}

	@Override
	public void preDownloadPatchlist() {
		Platform.runLater(() -> {
			patchProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
			patchProgressLabel.setText("Downloading the patchlist...");
		});
	}

	@Override
	public void postDownloadPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Downloaded the patchlist."));
	}

	@Override
	public void preReadPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Reading patchlist..."));
	}

	@Override
	public void postReadPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Read patchlist."));
	}

	@Override
	public void onUpdaterNeedsUpdate(final boolean requiresUpdate) {
		if (requiresUpdate) {
			Platform.runLater(() -> {
				patchProgressBar.setProgress(0);
				UIUtils.getCountdownTimeline(5, secondsLeft -> {
					patchProgressLabel.setText(String.format("Patcher requires an update. Updating application in %d seconds.", secondsLeft));
					if (secondsLeft <= 0) {
						Platform.exit();
					}
				}).playFromStart();
			});
		}
	}

	@Override
	public void preCalculateDifferences() {
		Platform.runLater(() -> patchProgressLabel.setText("Calculating differences..."));
	}

	@Override
	public void postCalculateDifferences() {
		Platform.runLater(() -> patchProgressLabel.setText("Calculated differences."));
	}

	@Override
	public void preDeleteFiles() {
		Platform.runLater(() -> patchProgressLabel.setText("Deleting obsolete files..."));
	}

	@Override
	public void postDeleteFiles() {
		Platform.runLater(() -> patchProgressLabel.setText("Deleted obsolete files."));
	}

	@Override
	public void prePacketsDownload() {
		Platform.runLater(() -> {
			patchProgressBar.setProgress(0);
			patchProgressLabel.setText("Downloading the patch...");
		});
	}

	@Override
	public void postPacketsDownload() {
		Platform.runLater(() -> {
			patchProgressBar.setProgress(1);
			patchProgressLabel.setText("Downloaded the patch.");
		});
	}

	@Override
	public void onPatchDone() {
		Platform.runLater(() -> {
			updateButton.setDisable(false);
			patchProgressLabel.setText("Ready to start the game.");
			updateButton.setText("Start Game");
			updateButton.setOnMouseClicked(event -> {
				updateButton.setDisable(true);
				final Optional<Path> rotWKHomeDirectory = registryService.findBfME2RotWKHomeDirectory();
				if (rotWKHomeDirectory.isPresent()) {
					try {
						ProcessUtils.run(rotWKHomeDirectory.get().resolve("/lotrbfme2ep1.exe")).waitFor();
					} catch (final IOException e) {
						Platform.runLater(() -> UIUtils.alert(
							Alert.AlertType.ERROR,
							"Error",
							"Game Error",
							"Could not start the Game. Please try again."
						).showAndWait());
					} catch (InterruptedException e) {
						log.debug("InterruptedException:", e);
					}
				} else {
					Platform.runLater(() -> UIUtils.alert(
						Alert.AlertType.ERROR,
						"Error",
						"Game Error",
						"Could not find the game. Is it installed?"
					).showAndWait());
				}
			});
		});
	}

	@Override
	public void onValidatingPacket() {
		Platform.runLater(() -> patchProgressLabel.setText("Validating the packet..."));
	}

	@Override
	public void onPatchProgressChange(final long current, final long target) {
		Platform.runLater(() -> {
			patchProgressBar.setProgress((double) current / target);
			patchProgressLabel.setText(current + "/" + target);
		});
	}

	private Alert getAlertForThrowable(final Throwable throwable) {
		if (throwable instanceof IOException) {
			return UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Update error",
				"There was an error downloading the update. Please try again later."
			);
		} else if (throwable instanceof URISyntaxException) {
			return UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Unexpected application error",
				"There was an unexpected error reading the application config. Please try again later."
			);
		} else if (throwable instanceof ValidationException) {
			return UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Validation error",
				"Could not validate the update. Some files may have been changed by another application."
			);
		}
		log.error("An unexpected error occurred.", throwable);
		return UIUtils.alert(
			Alert.AlertType.ERROR,
			"Error",
			"Unexpected error",
			"An unexpected error occurred."
		);
	}

	private void writeDefaultOptions(final File userDirectory, final Alert successAlert, final Alert errorAlert) {
		if (!userDirectory.exists()) {
			if (!userDirectory.mkdirs()) {
				errorAlert.show();
				return;
			}
		}
		if (userDirectory.exists()) {
			final File optionsIni = new File(userDirectory.getAbsolutePath() + "/options.ini");
			if (optionsIni.exists()) {
				final Optional<ButtonType> confirmation = UIUtils.alert(
					Alert.AlertType.CONFIRMATION,
					"Confirmation",
					"The game seems to be working just fine.",
					"Trying the fix again could result in loss of progress in game. Are you sure that u want to continue?"
				).showAndWait();
				if (confirmation.isEmpty() || confirmation.get() != ButtonType.OK) {
					return;
				}
			}
			try {
				optionFileService.writeOptionsFile(optionsIni, optionFileService.buildDefaultOptions());
				successAlert.show();
			} catch (IOException e) {
				log.error("Could not generate default options.ini", e);
				errorAlert.show();
			}
		}
	}
}
