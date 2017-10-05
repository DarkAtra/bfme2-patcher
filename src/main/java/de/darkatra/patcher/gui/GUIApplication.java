package de.darkatra.patcher.gui;

import de.darkatra.patcher.gui.controller.MainWindowController;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.stage.Stage;
import org.springframework.context.ConfigurableApplicationContext;

public class GUIApplication extends Application {
	private static ConfigurableApplicationContext applicationContext;

	public static void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		GUIApplication.applicationContext = applicationContext;
	}

	@Override
	public void start(Stage primaryStage) throws Exception {
		primaryStage.setTitle("Herr der Ringe Patcher");
		primaryStage.getIcons().add(new Image("/images/icon.jpg"));
		FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/mainWindow.fxml"));
		final Parent parent = fxmlLoader.load();
		final MainWindowController mainWindowController = fxmlLoader.getController();
		mainWindowController.setApplicationContext(applicationContext);
		primaryStage.setOnCloseRequest(event->mainWindowController.onExit());
		primaryStage.setScene(new Scene(parent, 600, 400));
		primaryStage.setResizable(false);
		primaryStage.show();
	}
}