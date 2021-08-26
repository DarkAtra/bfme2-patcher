package de.darkatra.patcher.updater.gui.element;

import javafx.geometry.Insets;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.layout.StackPane;

public class UpdateProgressBar extends StackPane {

	private final ProgressBar progressBar;
	private final Label label;

	public UpdateProgressBar() {
		progressBar = new ProgressBar();
		progressBar.setMaxWidth(Double.MAX_VALUE);
		progressBar.setMaxHeight(Double.MAX_VALUE);

		label = new Label("0/0");
		label.setPadding(new Insets(5, 0, 5, 0));

		getChildren().addAll(progressBar, label);
	}

	public void setText(final String value) {
		label.setText(value);
	}

	public void setProgress(final double value) {
		progressBar.setProgress(value);
	}
}
