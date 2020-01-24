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

import java.io.IOException;

// TODO: rewrite with javafx as dependency
public class GUIApplication extends Application {
	private Stage stage;

	@Override
	public void start(final Stage primaryStage) throws Exception {
		this.stage = primaryStage;
		stage.setTitle("Patchlistcreator");
		stage.setResizable(false);
		final FXMLLoader loader = new FXMLLoader();
		loader.setLocation(GUIApplication.class.getResource("/view/PatchListCreatorView.fxml"));
		final Parent root = (Parent) loader.load();
		final MainWindowController mainWindowController = loader.getController();
		mainWindowController.setGuiApplication(this);
		mainWindowController.initialize();
		final Scene scene = new Scene(root);
		primaryStage.setScene(scene);
		primaryStage.show();
	}

	public PrintedPatchWindowController showPrintedPatch() throws IOException {
		final FXMLLoader fxmlLoader = new FXMLLoader();
		fxmlLoader.setLocation(getClass().getResource("/view/PrintPatchWindow.fxml"));
		final Parent root = fxmlLoader.load();
		final PrintedPatchWindowController settingsWindow = fxmlLoader.getController();
		final Stage stage = new Stage();
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

	public static Alert alert(final Alert.AlertType alertType, final String title, final String header, final String msg) {
		final Alert alert = new Alert(alertType);
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
