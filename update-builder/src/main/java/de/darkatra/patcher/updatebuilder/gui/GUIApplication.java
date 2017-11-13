package de.darkatra.patcher.updatebuilder.gui;

import de.darkatra.patcher.updatebuilder.gui.controller.MainWindowController;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.BorderPane;
import javafx.stage.Stage;

import java.awt.*;

public class GUIApplication extends Application {

	private Stage stage;

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
		Scene scene = new Scene(root);
		primaryStage.setScene(scene);
		primaryStage.show();
	}

	public static void main(String[] args) {
		Application.launch(GUIApplication.class);
	}
}