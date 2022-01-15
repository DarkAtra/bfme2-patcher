package de.darkatra.patcher.updater.gui.controller;

import de.darkatra.patcher.updater.gui.element.UpdateProgressBar;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.OptionFileService;
import de.darkatra.patcher.updater.service.PatchService;
import de.darkatra.patcher.updater.service.PatcherStateService;
import de.darkatra.patcher.updater.service.UpdateService;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.service.model.PatcherState;
import de.darkatra.patcher.updater.service.model.UpdateProgress;
import de.darkatra.patcher.updater.util.ProcessUtils;
import de.darkatra.patcher.updater.util.UIUtils;
import io.reactivex.disposables.Disposable;
import io.reactivex.subjects.BehaviorSubject;
import io.reactivex.subjects.Subject;
import javafx.application.HostServices;
import javafx.application.Platform;
import javafx.concurrent.Task;
import javafx.fxml.FXML;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckMenuItem;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ProgressBar;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.DisposableBean;
import org.springframework.beans.factory.InitializingBean;
import org.springframework.core.task.SimpleAsyncTaskExecutor;
import org.springframework.stereotype.Component;

import javax.validation.ValidationException;
import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Path;
import java.text.CharacterIterator;
import java.text.StringCharacterIterator;
import java.util.Optional;
import java.util.concurrent.TimeUnit;

@Slf4j
@Component
@RequiredArgsConstructor
public class MainWindowController implements PatchEventListener, InitializingBean, DisposableBean {

	private final Context context;
	private final UpdaterProperties updaterProperties;
	private final PatchService patchService;
	private final OptionFileService optionFileService;
	private final PatcherStateService patcherStateService;
	private final UpdateService updateService;
	private final HostServices hostServices;

	private PatcherState patcherState;
	private Path patcherUserDir;
	private Path bfme2UserDir;
	private Path rotwkUserDir;
	private Path rotwkHomeDir;

	private boolean patchComplete = false;

	@FXML
	private UpdateProgressBar updateProgressBar;
	@FXML
	private Button updateButton;
	@FXML
	private Button startGameButton;
	@FXML
	private MenuItem versionMenuItem;
	@FXML
	private MenuItem checkUpdates;
	@FXML
	private MenuItem fixBfME2MenuItem;
	@FXML
	private MenuItem fixBfME2EP1MenuItem;
	@FXML
	private MenuItem bfmeReforgedCredits;
	@FXML
	private CheckMenuItem toggleHdEdition;
	@FXML
	private CheckMenuItem patchOnStartup;
	@FXML
	private CheckMenuItem launchAfterPatch;

	private final SimpleAsyncTaskExecutor taskExecutor = new SimpleAsyncTaskExecutor();
	private final Subject<UpdateProgress> patchProgressPublisher = BehaviorSubject.create();
	private Disposable subscription;

	@Override
	public void afterPropertiesSet() {
		taskExecutor.setDaemon(true);

		patcherState = patcherStateService.loadPatcherState();

		patcherUserDir = Path.of(context.get("patcherUserDir"));
		bfme2UserDir = Path.of(context.get("bfme2UserDir"));
		rotwkUserDir = Path.of(context.get("rotwkUserDir"));
		rotwkHomeDir = Path.of(context.get("rotwkHomeDir"));
	}

	@Override
	public void destroy() {
		subscription.dispose();
	}

	@FXML
	public void initialize() {

		versionMenuItem.setText(updaterProperties.getVersion());

		updateProgressBar.setProgress(0);
		updateProgressBar.setText("Waiting for user input.");

		subscription = patchProgressPublisher
			.throttleLast(100, TimeUnit.MILLISECONDS)
			.subscribe(updateProgress -> Platform.runLater(() -> {
				updateProgressBar.setProgress((double) updateProgress.getCurrent() / updateProgress.getTotal());
				updateProgressBar.setText(humanReadableByteCountBin(updateProgress.getCurrent()) + "/" + humanReadableByteCountBin(updateProgress.getTotal()));
			}));

		updateButton.setOnAction(event -> {
			if (updateButton.isDisabled()) {
				return;
			}
			updateButton.setDisable(true);
			checkUpdates.setDisable(true);
			startGameButton.setDisable(true);
			patchComplete = false;

			taskExecutor.submitListenable(() -> {
				patchService.patch(this);
				return true;
			}).completable().whenComplete((patchResult, e) -> {
				if (e != null) {
					Platform.runLater(() -> {
						updateProgressBar.setProgress(0);
						updateProgressBar.setText("Update failed. Please try again later.");
						getAlertForThrowable(e).show();
						updateButton.setDisable(false);
						checkUpdates.setDisable(false);
					});
				}
			});
		});

		startGameButton.setDisable(true);
		startGameButton.setOnAction(event -> launchGame(patcherState.isHdEditionEnabled()));

		checkUpdates.setOnAction(event -> checkForUpdates());

		fixBfME2MenuItem.setOnAction(event -> Platform.runLater(() -> writeDefaultOptions(
			bfme2UserDir.toFile(),
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

		fixBfME2EP1MenuItem.setOnAction(event -> Platform.runLater(() -> writeDefaultOptions(
			rotwkUserDir.toFile(),
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

		bfmeReforgedCredits.setOnAction(event -> hostServices.showDocument("https://bfmereforged.org/"));

		toggleHdEdition.setSelected(patcherState.isHdEditionEnabled());
		toggleHdEdition.setOnAction(event -> {
			patcherState.setHdEditionEnabled(toggleHdEdition.isSelected());
			persistsPatcherState(patcherState);
		});

		patchOnStartup.setSelected(patcherState.isPatchOnStartup());
		patchOnStartup.setOnAction(event -> {
			patcherState.setPatchOnStartup(patchOnStartup.isSelected());
			persistsPatcherState(patcherState);
		});

		launchAfterPatch.setSelected(patcherState.isLaunchAfterPatch());
		launchAfterPatch.setOnAction(event -> {
			patcherState.setLaunchAfterPatch(launchAfterPatch.isSelected());
			persistsPatcherState(patcherState);
		});

		if (patcherState.isPatchOnStartup()) {
			updateButton.fire();
		}
	}

	@Override
	public void preDownloadPatchlist() {
		Platform.runLater(() -> {
			updateProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
			updateProgressBar.setText("Downloading the patchlist...");
		});
	}

	@Override
	public void postDownloadPatchlist() {
		Platform.runLater(() -> updateProgressBar.setText("Downloaded the patchlist."));
	}

	@Override
	public void preReadPatchlist() {
		Platform.runLater(() -> updateProgressBar.setText("Reading patchlist..."));
	}

	@Override
	public void postReadPatchlist() {
		Platform.runLater(() -> updateProgressBar.setText("Read patchlist."));
	}

	@Override
	public void preCalculateDifferences() {
		Platform.runLater(() -> updateProgressBar.setText("Calculating differences..."));
	}

	@Override
	public void postCalculateDifferences() {
		Platform.runLater(() -> updateProgressBar.setText("Calculated differences."));
	}

	@Override
	public void preDeleteFiles() {
		Platform.runLater(() -> updateProgressBar.setText("Deleting obsolete files..."));
	}

	@Override
	public void postDeleteFiles() {
		Platform.runLater(() -> updateProgressBar.setText("Deleted obsolete files."));
	}

	@Override
	public void prePacketsDownload() {
		Platform.runLater(() -> {
			updateProgressBar.setProgress(0);
			updateProgressBar.setText("Downloading the patch...");
		});
	}

	@Override
	public void postPacketsDownload() {
		Platform.runLater(() -> {
			updateProgressBar.setProgress(1);
			updateProgressBar.setText("Downloaded the patch.");
		});
	}

	@Override
	public void onPatchDone() {
		patchComplete = true;
		Platform.runLater(() -> {
			checkUpdates.setDisable(false);
			updateButton.setDisable(false);
			updateProgressBar.setText("Ready to start the game.");
			startGameButton.setDisable(false);
			if (patcherState.isLaunchAfterPatch()) {
				startGameButton.fire();
			}
		});
	}

	@Override
	public void onValidatingPacket() {
		Platform.runLater(() -> updateProgressBar.setText("Validating the packet..."));
	}

	@Override
	public void onPatchProgressChange(final long current, final long total) {
		patchProgressPublisher.onNext(new UpdateProgress(current, total));
	}

	public static String humanReadableByteCountBin(long bytes) {
		final long absB = bytes == Long.MIN_VALUE ? Long.MAX_VALUE : Math.abs(bytes);
		if (absB < 1024) {
			return bytes + " B";
		}
		long value = absB;
		final CharacterIterator ci = new StringCharacterIterator("KMGTPE");
		for (int i = 40; i >= 0 && absB > 0xfffccccccccccccL >> i; i -= 10) {
			value >>= 10;
			ci.next();
		}
		value *= Long.signum(bytes);
		return String.format("%.1f %ciB", value / 1024.0, ci.current());
	}

	private void persistsPatcherState(final PatcherState patcherState) {
		try {
			patcherStateService.persistPatcherState(patcherState);
		} catch (final IOException e) {
			log.error("Could not persist the Patcher Settings.", e);
			Platform.runLater(() -> UIUtils.alert(
				Alert.AlertType.ERROR,
				"Application error",
				"Could not persist the Patcher Settings.",
				"Unable to write Patcher Settings - you could try to restart the patcher with admin privileges."
			).showAndWait());
		}
	}

	private void launchGame(final boolean withHdEdition) {

		updateButton.setDisable(true);

		final Task<Integer> launchGameTask = new Task<>() {
			@Override
			protected Integer call() throws Exception {
				return ProcessUtils.run(
					rotwkHomeDir.resolve("./lotrbfme2ep1.exe"),
					withHdEdition ? new String[]{"-mod", patcherUserDir.resolve("HDEdition.big").normalize().toString()} : new String[0]
				).waitFor();
			}
		};
		launchGameTask.setOnSucceeded((e) -> updateButton.setDisable(false));
		launchGameTask.setOnFailed((e) -> {
			if (e.getSource().getException() instanceof IOException) {
				Platform.runLater(() -> UIUtils.alert(
					Alert.AlertType.ERROR,
					"Error",
					"Game Error",
					"Could not start the Game. Please try again."
				).showAndWait());
			}
			updateButton.setDisable(false);
		});
		launchGameTask.setOnCancelled((e) -> updateButton.setDisable(false));

		taskExecutor.submitListenable(launchGameTask);
	}

	private Alert getAlertForThrowable(final Throwable throwable) {
		if (throwable instanceof IOException) {
			log.warn("IOException during update.", throwable);
			return UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Update error",
				"There was an error downloading the update. Please try again later."
			);
		} else if (throwable instanceof URISyntaxException) {
			log.error("URISyntaxException during update.", throwable);
			return UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Unexpected application error",
				"There was an unexpected error reading the application config. Please try again later."
			);
		} else if (throwable instanceof ValidationException) {
			log.debug("ValidationException during update.", throwable);
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

	private void checkForUpdates() {
		checkUpdates.setDisable(true);
		updateButton.setDisable(true);
		updateProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
		updateProgressBar.setText("Checking for updates...");

		taskExecutor.submitListenable(updateService::isNewVersionAvailable)
			.completable().whenComplete((newVersionAvailable, e) -> {
				if (newVersionAvailable) {
					Platform.runLater(() -> UIUtils.alert(
							Alert.AlertType.CONFIRMATION,
							"Update available",
							"A new update is available.",
							"Do you want to proceed and update to the latest version?"
						).showAndWait()
						.ifPresent(response -> {
							if (response == ButtonType.OK) {
								performUpdate();
							} else {
								resetProgressUI();
							}
						}));
				} else {
					Platform.runLater(() -> {
						resetProgressUI();
						UIUtils.alert(
							Alert.AlertType.INFORMATION,
							"Up to Date",
							"No new update is available.",
							"You're already using the latest version of the updater. Good Job!"
						).showAndWait();
					});
				}
			});
	}

	private void performUpdate() {

		Platform.runLater(() -> updateProgressBar.setText("Updating..."));

		taskExecutor.submitListenable(updateService::downloadLatestUpdaterVersion)
			.completable().whenComplete((downloadSucceeded, e) -> {

				if (!downloadSucceeded || e != null) {
					resetProgressUI();
					Platform.runLater(() -> UIUtils.alert(
						Alert.AlertType.ERROR,
						"Error",
						"Update error",
						"Could not download the latest version of the updater. Please try again later."
					).showAndWait());
					return;
				}

				Platform.runLater(() -> UIUtils.alert(
						Alert.AlertType.CONFIRMATION,
						"Application restart required",
						"The application requires a restart to complete the update to the latest version.",
						"Do you want to proceed and update to the latest version?"
					).showAndWait()
					.ifPresent(response -> {
						if (response == ButtonType.OK) {
							try {
								updateService.installUpdate();
								Platform.exit();
							} catch (final IOException ex) {
								log.error("Error installing the update.", ex);
								resetProgressUI();
								Platform.runLater(() -> UIUtils.alert(
									Alert.AlertType.ERROR,
									"Error",
									"Update error",
									"Could not install the latest version of the updater. Please try again later."
								).showAndWait());
							}
						} else {
							resetProgressUI();
						}
					}));
			});
	}

	private void resetProgressUI() {
		Platform.runLater(() -> {
			if (!patchComplete) {
				updateProgressBar.setProgress(0);
				updateProgressBar.setText("Waiting for user input.");
				startGameButton.setDisable(true);
			} else {
				updateProgressBar.setProgress(1);
				updateProgressBar.setText("Ready to start the game.");
				startGameButton.setDisable(false);
			}
			checkUpdates.setDisable(false);
			updateButton.setDisable(false);
		});
	}
}
