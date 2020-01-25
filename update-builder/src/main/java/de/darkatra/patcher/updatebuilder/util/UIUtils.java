package de.darkatra.patcher.updatebuilder.util;

import javafx.scene.control.Alert;
import javafx.stage.StageStyle;

public class UIUtils {

	public static Alert alert(final Alert.AlertType type, final String title, final String headerText, final String message) {
		final Alert alert = new Alert(type);
		alert.initStyle(StageStyle.UTILITY);
		alert.setTitle(title);
		alert.setHeaderText(headerText);
		alert.setContentText(message);
		return alert;
	}
}
