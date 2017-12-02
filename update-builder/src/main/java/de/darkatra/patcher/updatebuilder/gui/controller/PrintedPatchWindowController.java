package de.darkatra.patcher.updatebuilder.gui.controller;

import com.google.gson.GsonBuilder;
import de.darkatra.patcher.model.Patch;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.TextArea;
import javafx.stage.Stage;

public class PrintedPatchWindowController {
	private Stage stage;

	@FXML
	private TextArea printedPatchWindowTextArea;
	@FXML
	private Button confirmButton;

	@FXML
	private void initialize() {
		confirmButton.setOnMouseClicked(event->this.stage.close());
	}

	public void setPrintedPatchWindowArea(Patch patch) {
		Platform.runLater(()->this.printedPatchWindowTextArea.setText(new GsonBuilder().serializeNulls().setPrettyPrinting().create().toJson(patch)));
	}

	public void setStage(Stage stage) {
		this.stage = stage;
	}
}
