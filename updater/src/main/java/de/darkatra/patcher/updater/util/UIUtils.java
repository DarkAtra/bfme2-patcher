package de.darkatra.patcher.updater.util;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.beans.property.IntegerProperty;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.scene.control.Alert;
import javafx.stage.StageStyle;
import javafx.util.Duration;

import java.util.function.Consumer;

public class UIUtils {
	public static Timeline getCountdownTimeline(final int duration, final Consumer<Integer> action) {
		final IntegerProperty secondsLeft = new SimpleIntegerProperty(duration);
		final Timeline timeline = new Timeline();
		timeline.setCycleCount(Timeline.INDEFINITE);
		timeline.getKeyFrames().add(new KeyFrame(Duration.seconds(1), event -> {
			secondsLeft.setValue(secondsLeft.getValue() - 1);
			action.accept(secondsLeft.get());
			if (secondsLeft.get() <= 0) {
				timeline.stop();
			}
		}));
		return timeline;
	}

	public static Alert alert(final Alert.AlertType type, final String title, final String headerText, final String message) {
		final Alert alert = new Alert(type);
		alert.initStyle(StageStyle.UTILITY);
		alert.setTitle(title);
		alert.setHeaderText(headerText);
		alert.setContentText(message);
		return alert;
	}
}
