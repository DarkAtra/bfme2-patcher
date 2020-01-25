package de.darkatra.patcher.updatebuilder.gui.controller;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import de.darkatra.patcher.updatebuilder.model.Patch;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.TextArea;
import javafx.stage.Stage;

public class PrintedPatchWindowController {

	private final ObjectMapper objectMapper = new ObjectMapper().enable(SerializationFeature.INDENT_OUTPUT);
	private Stage stage;

	@FXML
	private TextArea printedPatchWindowTextArea;
	@FXML
	private Button confirmButton;

	@FXML
	private void initialize() {
		confirmButton.setOnMouseClicked(event -> this.stage.close());
	}

	public void setPrintedPatchWindowArea(Patch patch) {
		Platform.runLater(() -> {
			try {
				this.printedPatchWindowTextArea.setText(objectMapper.writeValueAsString(patch));
			} catch (final JsonProcessingException e) {
				e.printStackTrace();
			}
		});
	}

	public void setStage(Stage stage) {
		this.stage = stage;
	}
}
