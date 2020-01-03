package de.darkatra.patcher.updater.gui.controller;

import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.util.UIUtils;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ProgressBar;
import lombok.extern.slf4j.Slf4j;
import org.springframework.core.task.AsyncListenableTaskExecutor;
import org.springframework.core.task.SimpleAsyncTaskExecutor;
import org.springframework.stereotype.Component;

@Slf4j
@Component
//@RequiredArgsConstructor
public class MainWindowController implements PatchEventListener {

	//	private final PatchController patchController;
	//	private final OptionFileService optionFileService;

	@FXML
	private ProgressBar patchProgressBar;
	@FXML
	private Label patchProgressLabel;
	@FXML
	private Button updateButton;
	@FXML
	private Button toggleModButton;
	@FXML
	private MenuItem gameSettingsMenuItem;
	@FXML
	private MenuItem patcherSettingsMenuItem;
	@FXML
	private MenuItem fixBfME2MenuItem;
	@FXML
	private MenuItem fixBfME2EP1MenuItem;

	//	private final Callable<Boolean> patchTask = () -> {
	//		try {
	//			patchController.patch(this);
	//		} catch (IOException e) {
	//			log.debug("IOException", e);
	//			Platform.runLater(() -> {
	//				patchProgressBar.setProgress(0);
	//				patchProgressLabel.setText("Update failed. Please try again later.");
	//				UIUtils.alert(Alert.AlertType.ERROR, "Error", "Update error",
	//					"There was an error downloading the update. Try to rerun this application with admin privileges.").show();
	//			});
	//			return false;
	//		} catch (URISyntaxException e) {
	//			log.debug("URISyntaxException", e);
	//			Platform.runLater(() -> {
	//				patchProgressBar.setProgress(0);
	//				patchProgressLabel.setText("Update failed. Please try again later.");
	//				UIUtils.alert(Alert.AlertType.ERROR, "Error", "Unexpected application error",
	//					"There was an unexpected error reading the application config. Please try again later.").show();
	//			});
	//			return false;
	//		} catch (ValidationException e) {
	//			log.debug("ValidationException", e);
	//			Platform.runLater(() -> {
	//				patchProgressBar.setProgress(0);
	//				patchProgressLabel.setText("Update failed. Please try again later.");
	//				UIUtils.alert(Alert.AlertType.ERROR, "Error", "Validation error",
	//					"Could not validate the update. Some files may have been changed by another application.").show();
	//			});
	//			return false;
	//		}
	//		return true;
	//	};
	private final AsyncListenableTaskExecutor taskExecutor = new SimpleAsyncTaskExecutor();

	@FXML
	public void initialize() {


		patchProgressBar.setProgress(0);
		patchProgressLabel.setText("Waiting for user input.");

		//		updateButton.setOnMouseClicked(new EventHandler<>() {
		//			@Override
		//			public void handle(MouseEvent event) {
		//				updateButton.setDisable(true);
		//				final EventHandler<MouseEvent> consumer = e -> {
		//				};
		//				updateButton.setOnMouseClicked(consumer);
		//				final Runnable callback = () -> {
		//					if (updateButton.getOnMouseClicked() == consumer) {
		//						updateButton.setOnMouseClicked(this);
		//					}
		//					updateButton.setDisable(false);
		//				};
		//				taskExecutor.submitListenable(patchTask)
		//					.completable()
		//					.thenRun(callback)
		//					.exceptionally(ex -> {
		//						log.debug("TaskExecutor with exception", ex);
		//						callback.run();
		//						return null;
		//					});
		//			}
		//		});

		toggleModButton.setDisable(true);
		toggleModButton.setOnMouseClicked(event -> {
			// TODO: toggle mod (enable/disable)
		});

		//		fixBfME2MenuItem.setOnAction(event -> {
		//			final Optional<String> bfme2UserDir = context.getString("bfme2UserDir");
		//			if (bfme2UserDir.isPresent()) {
		//				final int result = writeDefaultOptionsIni(bfme2UserDir.get());
		//				if (result == 1) {
		//					UIUtils.alert(Alert.AlertType.INFORMATION, "Success", "Fixed BfME 2", "The options.ini file was created successfully.").show();
		//					return;
		//				} else if (result == 0) {
		//					return;
		//				}
		//			}
		//			UIUtils.alert(Alert.AlertType.ERROR, "Application error", "Could not fix BfME 2", "There was an error writing the default options.ini")
		//				.show();
		//		});

		//		fixBfME2EP1MenuItem.setOnAction(event -> {
		//			final Optional<String> rotwkUserDir = context.getString("rotwkUserDir");
		//			if (rotwkUserDir.isPresent()) {
		//				final int result = writeDefaultOptionsIni(rotwkUserDir.get());
		//				if (result == 1) {
		//					UIUtils.alert(Alert.AlertType.INFORMATION, "Success", "Fixed BfME 2 RotWK", "The options.ini file was created successfully.")
		//						.show();
		//					return;
		//				} else if (result == 0) {
		//					return;
		//				}
		//			}
		//			UIUtils
		//				.alert(Alert.AlertType.ERROR, "Application error", "Could not fix BfME 2 RotWK", "There was an error writing the default options.ini")
		//				.show();
		//		});
	}

	@Override
	public void preDownloadServerPatchlist() {
		Platform.runLater(() -> {
			patchProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
			patchProgressLabel.setText("Downloading the patchlist...");
		});
	}

	@Override
	public void postDownloadServerPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Downloaded the patchlist."));
	}

	@Override
	public void preReadServerPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Reading patchlist..."));
	}

	@Override
	public void postReadServerPatchlist() {
		Platform.runLater(() -> patchProgressLabel.setText("Read patchlist."));
	}

	@Override
	public void onPatcherNeedsUpdate(final boolean requiresUpdate) {
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
		//		Platform.runLater(() -> {
		//			updateButton.setDisable(false);
		//			patchProgressLabel.setText("Ready to start the game.");
		//			updateButton.setText("Start Game");
		//			updateButton.setOnMouseClicked(event -> {
		//				updateButton.setDisable(true);
		//				final Optional<String> rotwkHomeDirPath = context.getString("rotwkHomeDir");
		//				if (rotwkHomeDirPath.isPresent()) {
		//					final ProcessBuilder pb = new ProcessBuilder(rotwkHomeDirPath.get() + "/lotrbfme2ep1.exe");
		//					try {
		//						final Process p = pb.start();
		//						p.waitFor();
		//					} catch (IOException e) {
		//						Platform.runLater(
		//							() -> UIUtils.alert(Alert.AlertType.ERROR, "Error", "Game Error", "Could not start the Game. Please try again.")
		//								.showAndWait());
		//					} catch (InterruptedException e) {
		//						log.debug("InterruptedException:", e);
		//					}
		//				} else {
		//					Platform.runLater(
		//						() -> UIUtils.alert(Alert.AlertType.ERROR, "Error", "Game Error", "Could not find the game. Is it installed?")
		//							.showAndWait());
		//				}
		//			});
		//		});
	}

	@Override
	public void onValidatingPacket() {
		Platform.runLater(() -> patchProgressLabel.setText("Validating the packet..."));
	}

	@Override
	public void onPatchProgressChange(long current, long target) {
		Platform.runLater(() -> {
			patchProgressBar.setProgress((double) current / target);
			patchProgressLabel.setText(current + "/" + target);
		});
	}

	//	private int writeDefaultOptionsIni(@NonNull String userDit) {
	//		File userDirFile = new File(userDit);
	//		if (!userDirFile.exists()) {
	//			if (!userDirFile.mkdirs()) {
	//				return -1;
	//			}
	//		}
	//		if (userDirFile.exists()) {
	//			final File optionsIni = new File(userDirFile.getAbsolutePath() + "/options.ini");
	//			if (optionsIni.exists()) {
	//				final Optional<ButtonType> confirmation = UIUtils
	//					.alert(Alert.AlertType.CONFIRMATION, "Confirmation", "The game seems to be working just fine.",
	//						"The game seems to be working just fine. Trying the fix again could result in loss of progress in game. Are you sure that u want to continue?")
	//					.showAndWait();
	//				if (confirmation.isEmpty() || confirmation.get() != ButtonType.OK) {
	//					return 0;
	//				}
	//			}
	//			try {
	//				optionFileService.writeOptionsFile(optionsIni, optionFileService.buildDefaultOptions());
	//				return 1;
	//			} catch (IOException e) {
	//				log.error("Could not generate default options.ini", e);
	//			}
	//		}
	//		return -1;
	//	}
}
