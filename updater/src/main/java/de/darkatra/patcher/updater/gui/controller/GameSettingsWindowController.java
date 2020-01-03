package de.darkatra.patcher.updater.gui.controller;

import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.ComboBox;
import org.springframework.stereotype.Component;

@Component
public class GameSettingsWindowController {

	@FXML
	private ComboBox<String> resolutionComboBox;

	@FXML
	private Button saveButton, discardButton;
}
