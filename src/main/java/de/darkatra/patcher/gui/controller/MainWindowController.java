package de.darkatra.patcher.gui.controller;

import de.darkatra.patcher.PatchController;
import de.darkatra.patcher.exception.ValidationException;
import de.darkatra.patcher.listener.PatchEventListener;
import de.darkatra.util.asyncapi.AsyncExecutionService;
import de.darkatra.util.asyncapi.AsyncTask;
import javafx.animation.FadeTransition;
import javafx.animation.ParallelTransition;
import javafx.application.Platform;
import javafx.event.EventHandler;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.Pane;
import javafx.util.Duration;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.IOException;
import java.net.URISyntaxException;

@Slf4j
public class MainWindowController implements AsyncTask, PatchEventListener {
	private PatchController patchController;

	public void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		this.patchController = applicationContext.getBean(PatchController.class);
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

	private ParallelTransition fadeBackgroundTransition;
	private int currentImage = 0;
	private String[] imagePaths;
	private AsyncTask patchTask;

	public MainWindowController() {
		imagePaths = new String[] {
				"/images/splash2_1920x1080.jpg",
				"/images/splash10_1920x1080.jpg",
				"/images/splash12_1920x1080.jpg",
				"/images/splash13_1920x1080.jpg"
		};
		fadeBackgroundTransition = new ParallelTransition();
		patchTask = ()->{
			try {
				patchController.patch(this);
			} catch(IOException e) {
				// TODO: Fehlermeldung
			} catch(URISyntaxException e) {
				// TODO: Fehlermeldung
			} catch(ValidationException e) {
				// TODO: Fehlermeldung
			}
			return true;
		};
	}

	@FXML
	public void initialize() {
		log.debug("Test");
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
	}

	@Override
	public void onServerPatchlistDownloaded() {
		Platform.runLater(()->{
			patchProgressBar.setProgress(ProgressBar.INDETERMINATE_PROGRESS);
			patchProgressLabel.setText("Downloaded the patchlist.");
		});
	}

	@Override
	public void onServerPatchlistRead() {
		Platform.runLater(()->patchProgressLabel.setText("Read patchlist."));
	}

	@Override
	public void onPatcherNeedsUpdate() {
		Platform.runLater(()->patchProgressLabel.setText("Patcher requires an update."));
	}

	@Override
	public void onDifferencesCalculated() {
		Platform.runLater(()->patchProgressLabel.setText("Calculated differences."));
	}

	@Override
	public void onFilesDeleted() {
		// Nothing yet
	}

	@Override
	public void onPacketsDownloaded() {
		Platform.runLater(()->{
			patchProgressBar.setProgress(1);
			patchProgressLabel.setText("Downloaded the patch.");
		});
	}

	@Override
	public void onPatchDone() {
		Platform.runLater(()->{
			patchProgressLabel.setText("Downloaded the patch.");
			updateButton.setText("Start Game");
			updateButton.setOnMouseClicked(e->{
				// TODO: start the game
			});
		});
	}

	@Override
	public void onPatchProgressChanged(long current, long target) {
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

	public void onExit() {
		AsyncExecutionService.getInstance().interruptAsyncTask(patchTask);
		AsyncExecutionService.getInstance().interruptAsyncTask(this);
	}
}