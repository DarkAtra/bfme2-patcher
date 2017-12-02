package de.darkatra.patcher.updatebuilder.gui;

import de.darkatra.patcher.updatebuilder.gui.controller.MainWindowController;
import de.darkatra.patcher.updatebuilder.gui.controller.PrintedPatchWindowController;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.IOException;

public class GUIApplication extends Application {
	private static ConfigurableApplicationContext applicationContext;
	private Stage stage;

	public static void setApplicationContext(ConfigurableApplicationContext applicationContext) {
		GUIApplication.applicationContext = applicationContext;
	}

	@Override
	public void start(Stage primaryStage) throws Exception {
		this.stage = primaryStage;
		stage.setTitle("Patchlistcreator");
		stage.setResizable(false);
		FXMLLoader loader = new FXMLLoader();
		loader.setLocation(GUIApplication.class.getResource("/view/PatchListCreatorView.fxml"));
		Parent root = (Parent) loader.load();
		MainWindowController mainWindowController = loader.getController();
		mainWindowController.setGuiApplication(this);
		mainWindowController.initialize();
		mainWindowController.setApplicationContext(applicationContext);
		Scene scene = new Scene(root);
		primaryStage.setScene(scene);
		primaryStage.show();
	}

	public PrintedPatchWindowController showPrintedPatch() throws IOException {
		FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/PrintPatchWindow.fxml"));
		Parent root = fxmlLoader.load();
		PrintedPatchWindowController settingsWindow = fxmlLoader.getController();
		Stage stage = new Stage();
		settingsWindow.setStage(stage);
		stage.initOwner(this.stage);
		stage.initModality(Modality.WINDOW_MODAL);
		stage.setTitle("Patchlistcreator");
		//		stage.getIcons().add(new Image("/images/icon.jpg"));
		stage.setResizable(false);
		final Scene scene = new Scene(root);
		stage.setScene(scene);
		Platform.runLater(stage::showAndWait);
		return settingsWindow;
	}

	public static Alert alert(Alert.AlertType alertType, String title, String header, String msg) {
		Alert alert = new Alert(alertType);
		alert.initStyle(StageStyle.UTILITY);
		alert.setTitle(title);
		alert.setHeaderText(header);
		alert.setContentText(msg);
		return alert;
	}

	public Stage getStage() {
		return stage;
	}
}