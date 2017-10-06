package de.darkatra.patcher.gui.controller;

import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.ComboBox;
import javafx.stage.Stage;

public class PatcherSettingsWindowController {
	@FXML
	private ComboBox<String> resolutionComboBox;
	@FXML
	private Button saveButton, discardButton;
	private Stage stage;

	@FXML
	private void initialize() {
		discardButton.setOnMouseClicked(event->stage.close());
	}

	public void setStage(Stage stage) {
		this.stage = stage;
	}
}