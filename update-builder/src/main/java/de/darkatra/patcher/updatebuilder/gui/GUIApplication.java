package de.darkatra.patcher.updatebuilder.gui;

import de.darkatra.patcher.updatebuilder.gui.controller.MainWindowController;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.BorderPane;
import javafx.stage.Stage;
import org.springframework.context.ConfigurableApplicationContext;

public class GUIApplication extends Application {
	private Stage stage;
	private static ConfigurableApplicationContext applicationContext;

	public static void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		GUIApplication.applicationContext = applicationContext;
	}
	@Override
	public void start(Stage primaryStage) throws Exception {
		this.stage= primaryStage;

		stage.setTitle("Patchlistcreator");
		stage.setResizable(false);
		FXMLLoader loader = new FXMLLoader();
		loader.setLocation(GUIApplication.class.getResource("/view/PatchListCreatorView.fxml"));
		BorderPane root = (BorderPane) loader.load();
		MainWindowController mainWindowController = loader.getController();
		mainWindowController.setGuiApplication(this);
		mainWindowController.initialize();
		mainWindowController.setApplicationContext(applicationContext);
		Scene scene = new Scene(root);
		primaryStage.setScene(scene);
		primaryStage.show();
	}

	public Stage getStage() {
		return stage;
	}

}