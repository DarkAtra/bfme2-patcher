package de.darkatra.patcher.updater.gui.controller;

import de.darkatra.patcher.updater.ApplicationConstant;
import de.darkatra.patcher.updater.PatchController;
import de.darkatra.patcher.updater.exception.ContextConfigurationException;
import de.darkatra.patcher.updater.exception.ValidationException;
import de.darkatra.patcher.updater.gui.GUIApplication;
import de.darkatra.patcher.updater.listener.PatchEventListener;
import de.darkatra.patcher.updater.model.Context;
import de.darkatra.patcher.updater.service.OptionFileService;
import de.darkatra.util.asyncapi.AsyncExecutionService;
import de.darkatra.util.asyncapi.AsyncTask;
import javafx.animation.FadeTransition;
import javafx.animation.KeyFrame;
import javafx.animation.ParallelTransition;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.property.IntegerProperty;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.event.EventHandler;
import javafx.fxml.FXML;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ProgressBar;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.Pane;
import javafx.util.Duration;
import lombok.extern.slf4j.Slf4j;
import org.jetbrains.annotations.NotNull;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.util.Optional;

@Slf4j
public class MainWindowController implements AsyncTask, PatchEventListener {
	private Context context;
	private PatchController patchController;
	private OptionFileService optionFileService;

	public void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		this.context = applicationContext.getBean(Context.class);
		this.patchController = applicationContext.getBean(PatchController.class);
		this.optionFileService = applicationContext.getBean(OptionFileService.class);
	}

	@FXML
	private Pane fadeOut;
	@FXML
	private Pane fadeIn;
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

	private final ParallelTransition fadeBackgroundTransition;
	private final Timeline updateCountdownTimeline;
	private final String[] imagePaths;
	private final AsyncTask patchTask;
	private GUIApplication guiApplication;
	private int currentImage = 0;

	public MainWindowController() {
		imagePaths = new String[] {
				"/images/splash2_1920x1080.jpg",
				"/images/splash10_1920x1080.jpg",
				"/images/splash12_1920x1080.jpg",
				"/images/splash13_1920x1080.jpg"
		};
		fadeBackgroundTransition = new ParallelTransition();
		IntegerProperty secondsLeft = new SimpleIntegerProperty(5);
		updateCountdownTimeline = new Timeline();
		updateCountdownTimeline.setCycleCount(Timeline.INDEFINITE);
		updateCountdownTimeline.getKeyFrames().add(new KeyFrame(Duration.seconds(1), event->{
			secondsLeft.setValue(secondsLeft.getValue() - 1);
			patchProgressLabel.setText("Patcher requires an update. Updating application in " + secondsLeft.getValue() + " seconds.");
			if(secondsLeft.getValue() <= 0) {
				updateCountdownTimeline.stop();
				Platform.exit();
				System.exit(ApplicationConstant.REQUIRES_UPDATE_EXIT_CODE);
			}
		}));
		patchTask = ()->{
			try {
				patchController.patch(this);
			} catch(IOException e) {
				log.debug("IOException", e);
				Platform.runLater(()->{
					patchProgressBar.setProgress(0);
					patchProgressLabel.setText("Update failed. Please try again later.");
					GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Update error", "There was an error downloading the update. Try to rerun this application with admin privileges.").show();
				});
			} catch(URISyntaxException e) {
				log.debug("URISyntaxException", e);
				Platform.runLater(()->{
					patchProgressBar.setProgress(0);
					patchProgressLabel.setText("Update failed. Please try again later.");
					GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Unexpected application error", "There was an unexpected error reading the application config. Please try again later.").show();
				});
			} catch(ValidationException e) {
				log.debug("ValidationException", e);
				Platform.runLater(()->{
					patchProgressBar.setProgress(0);
					patchProgressLabel.setText("Update failed. Please try again later.");
					GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Validation error", "Could not validate the update. Some files may have been changed by another application.").show();
				});
			} catch(ContextConfigurationException e) {
				log.debug("ContextConfigurationException", e);
				Platform.runLater(()->{
					patchProgressBar.setProgress(0);
					patchProgressLabel.setText("Update failed. Please try again later.");
					GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Configuration error", "Update failed due to an unexpected configuration error.").show();
				});
			} catch(InterruptedException e) {
				log.debug("InterruptedException", e);
			}
			return true;
		};
	}

	@FXML
	public void initialize() {
		fadeOut.setStyle("-fx-background-image: url('" + imagePaths[currentImage] + "');");
		fadeIn.setStyle("-fx-background-image: url('" + imagePaths[++currentImage] + "');");
		FadeTransition fadeInTransition = new FadeTransition(Duration.millis(2000), fadeIn);
		fadeInTransition.setFromValue(0.0);
		fadeInTransition.setToValue(1.0);
		FadeTransition fadeOutTransition = new FadeTransition(Duration.millis(2000), fadeOut);
		fadeOutTransition.setFromValue(1.0);
		fadeOutTransition.setToValue(0.0);
		fadeBackgroundTransition.getChildren().addAll(fadeInTransition, fadeOutTransition);
		fadeBackgroundTransition.setOnFinished((event)->{
			fadeOut.setStyle("-fx-background-image: url('" + imagePaths[currentImage] + "');");
			fadeOut.setOpacity(1);
			fadeIn.setOpacity(0);
			currentImage = ++currentImage % imagePaths.length;
			fadeIn.setStyle("-fx-background-image: url('" + imagePaths[currentImage] + "');");
		});
		AsyncExecutionService.getInstance().executeAsyncTask(this, "GUIFadeInFadeOutThread");

		patchProgressBar.setProgress(0);
		patchProgressLabel.setText("Waiting for user input.");

		updateButton.setOnMouseClicked(new EventHandler<MouseEvent>() {
			@Override
			public void handle(MouseEvent event) {
				final EventHandler<MouseEvent> consumer = e->{};
				updateButton.setOnMouseClicked(consumer);
				AsyncExecutionService.getInstance().executeAsyncTask(patchTask).onSuccessAndFailure(()->{
					if(updateButton.getOnMouseClicked() == consumer) {
						updateButton.setOnMouseClicked(this);
					}
				});
			}
		});

		toggleModButton.setOnMouseClicked(event->{
			// TODO: toggle mod (enable/disable)
		});

		fixBfME2MenuItem.setOnAction(event->{
			final Optional<String> bfme2UserDir = context.getString("bfme2UserDir");
			if(bfme2UserDir.isPresent()) {
				final int result = writeDefaultOptionsIni(bfme2UserDir.get());
				if(result == 1) {
					GUIApplication.alert(Alert.AlertType.INFORMATION, "Success", "Fixed BfME 2", "The options.ini file was created successfully.").show();
					return;
				} else if(result == 0) {
					return;
				}
			}
			GUIApplication.alert(Alert.AlertType.ERROR, "Application error", "Could not fix BfME 2", "There was an error writing the default options.ini").show();
		});

		fixBfME2EP1MenuItem.setOnAction(event->{
			final Optional<String> rotwkUserDir = context.getString("rotwkUserDir");
			if(rotwkUserDir.isPresent()) {
				final int result = writeDefaultOptionsIni(rotwkUserDir.get());
				if(result == 1) {
					GUIApplication.alert(Alert.AlertType.INFORMATION, "Success", "Fixed BfME 2 RotWK", "The options.ini file was created successfully.").show();
					return;
				} else if(result == 0) {
					return;
				}
			}
			GUIApplication.alert(Alert.AlertType.ERROR, "Application error", "Could not fix BfME 2 RotWK", "There was an error writing the default options.ini").show();
		});

		gameSettingsMenuItem.setOnAction(event->{
			try {
				guiApplication.showGameSettingsWindow();
			} catch(IOException e) {
				GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application error", "Could not open the game settings.").show();
			}
		});
		patcherSettingsMenuItem.setOnAction(event->{
			try {
				guiApplication.showPatcherSettingsWindow();
			} catch(IOException e) {
				GUIApplication.alert(Alert.AlertType.ERROR, "Error", "Application error", "Could not open the patcher settings.").show();
			}
		});
	}

	@Override
	public void preDownloadServerPatchlist() {
		Platform.runLater(()->{
			patchProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
			patchProgressLabel.setText("Downloading the patchlist...");
		});
	}

	@Override
	public void postDownloadServerPatchlist() {
		Platform.runLater(()->{
			patchProgressLabel.setText("Downloaded the patchlist.");
		});
	}

	@Override
	public void preReadServerPatchlist() {
		Platform.runLater(()->patchProgressLabel.setText("Reading patchlist..."));
	}

	@Override
	public void postReadServerPatchlist() {
		Platform.runLater(()->patchProgressLabel.setText("Read patchlist."));
	}

	@Override
	public void onPatcherNeedsUpdate(boolean requiresUpdate) {
		if(requiresUpdate) {
			Platform.runLater(()->{
				patchProgressBar.setProgress(0);
				updateCountdownTimeline.playFromStart();
			});
		} else {
			// TODO: close the launcher application via msg
		}
	}

	@Override
	public void preCalculateDifferences() {
		Platform.runLater(()->patchProgressLabel.setText("Calculating differences..."));
	}

	@Override
	public void postCalculateDifferences() {
		Platform.runLater(()->patchProgressLabel.setText("Calculated differences."));
	}

	@Override
	public void preDeleteFiles() {
		Platform.runLater(()->patchProgressLabel.setText("Deleting obsolete files..."));
	}

	@Override
	public void postDeleteFiles() {
		Platform.runLater(()->patchProgressLabel.setText("Deleted obsolete files."));
	}

	@Override
	public void prePacketsDownload() {
		Platform.runLater(()->{
			patchProgressBar.setProgress(0);
			patchProgressLabel.setText("Downloading the patch...");
		});
	}

	@Override
	public void postPacketsDownload() {
		Platform.runLater(()->{
			patchProgressBar.setProgress(1);
			patchProgressLabel.setText("Downloaded the patch.");
		});
	}

	@Override
	public void onPatchDone() {
		Platform.runLater(()->{
			patchProgressLabel.setText("Ready to start the game.");
			updateButton.setText("Start Game");
			updateButton.setOnMouseClicked(e->{
				// TODO: start the game
			});
		});
	}

	@Override
	public void onValidatingPacket() {
		Platform.runLater(()->patchProgressLabel.setText("Validating the packet..."));
	}

	@Override
	public void onPatchProgressChange(long current, long target) {
		Platform.runLater(()->{
			patchProgressBar.setProgress((double) current / target);
			patchProgressLabel.setText(current + "/" + target);
		});
	}

	@Override
	public boolean run() {
		while(!Thread.currentThread().isInterrupted()) {
			try {
				Thread.sleep(3000);
				Platform.runLater(fadeBackgroundTransition::playFromStart);
			} catch(InterruptedException | IllegalArgumentException e) {
				return false;
			}
		}
		return true;
	}

	private int writeDefaultOptionsIni(@NotNull String userDit) {
		File userDirFile = new File(userDit);
		if(!userDirFile.exists()) {
			if(!userDirFile.mkdirs()) {
				return -1;
			}
		}
		if(userDirFile.exists()) {
			File optionsIni = new File(userDirFile.getAbsolutePath() + "/options.ini");
			if(optionsIni.exists()) {
				final Optional<ButtonType> confirmation = GUIApplication.alert(Alert.AlertType.CONFIRMATION, "Confirmation", "The game seems to be working just fine.", "The game seems to be working just fine. Trying the fix again could result in loss of progress in game. Are you sure that u want to continue?").showAndWait();
				if(!confirmation.isPresent() || confirmation.get() != ButtonType.OK) {
					return 0;
				}
			}
			try {
				optionFileService.writeOptionsFile(optionsIni, optionFileService.buildDefaultOptionsIni());
				return 1;
			} catch(IOException e) {
				log.error("Could not generate default options.ini", e);
			}
		}
		return -1;
	}

	public void setGuiApplication(GUIApplication guiApplication) {
		this.guiApplication = guiApplication;
	}

	public void onExit() {
		AsyncExecutionService.getInstance().interruptAsyncTask(patchTask);
		AsyncExecutionService.getInstance().interruptAsyncTask(this);
	}
}