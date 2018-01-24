package de.darkatra.patcher.updater.gui;

import de.darkatra.patcher.updater.gui.controller.GameSettingsWindowController;
import de.darkatra.patcher.updater.gui.controller.MainWindowController;
import de.darkatra.patcher.updater.gui.controller.PatcherSettingsWindowController;
import de.darkatra.util.asyncapi.AsyncExecutionService;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.image.Image;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.IOException;

public class GUIApplication extends Application {
	private static ConfigurableApplicationContext applicationContext;
	private Stage primaryStage;

	public static void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		GUIApplication.applicationContext = applicationContext;
	}

	@Override
	public void start(Stage primaryStage) throws Exception {
		this.primaryStage = primaryStage;
		primaryStage.setTitle("Herr der Ringe Patcher");
		primaryStage.getIcons().add(new Image("/images/icon.jpg"));
		FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/mainWindow.fxml"));
		final Parent parent = fxmlLoader.load();
		final MainWindowController mainWindowController = fxmlLoader.getController();
		mainWindowController.setGuiApplication(this);
		mainWindowController.setApplicationContext(applicationContext);
		primaryStage.setOnCloseRequest(event->{
			AsyncExecutionService.getInstance().interruptAllAsyncTasks();
			Platform.exit();
			System.exit(0);
		});
		primaryStage.setScene(new Scene(parent, 600, 400));
		primaryStage.setResizable(false);
		primaryStage.show();
	}

	public GameSettingsWindowController showGameSettingsWindow() throws IOException {
		FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/gameSettingsWindow.fxml"));
		Parent root = fxmlLoader.load();
		GameSettingsWindowController settingsWindow = fxmlLoader.getController();
		Stage stage = new Stage();
		settingsWindow.setStage(stage);
		stage.initOwner(primaryStage);
		stage.initModality(Modality.WINDOW_MODAL);
		stage.setTitle("Game Settings");
		stage.getIcons().add(new Image("/images/icon.jpg"));
		stage.setResizable(false);
		final Scene scene = new Scene(root);
		stage.setScene(scene);
		Platform.runLater(stage::showAndWait);
		return settingsWindow;
	}

	public PatcherSettingsWindowController showPatcherSettingsWindow() throws IOException {
		FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/patcherSettingsWindow.fxml"));
		Parent root = fxmlLoader.load();
		PatcherSettingsWindowController settingsWindow = fxmlLoader.getController();
		Stage stage = new Stage();
		settingsWindow.setStage(stage);
		stage.initOwner(primaryStage);
		stage.initModality(Modality.WINDOW_MODAL);
		stage.setTitle("Patcher Settings");
		stage.getIcons().add(new Image("/images/icon.jpg"));
		stage.setResizable(false);
		final Scene scene = new Scene(root);
		stage.setScene(scene);
		Platform.runLater(stage::showAndWait);
		return settingsWindow;
	}

	public static Alert alert(Alert.AlertType type, String title, String headerText, String message) {
		Alert alert = new Alert(type);
		alert.initStyle(StageStyle.UTILITY);
		alert.setTitle(title);
		alert.setHeaderText(headerText);
		alert.setContentText(message);
		return alert;
	}
}