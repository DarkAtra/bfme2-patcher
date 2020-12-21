package de.darkatra.patcher.updater.util;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.beans.property.IntegerProperty;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.stage.Modality;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import javafx.util.Duration;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.ApplicationContext;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.util.function.Consumer;

@Slf4j
@Component
@RequiredArgsConstructor
public class UIUtils {

	private final ApplicationContext context;

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

	public Stage dialog(final String view) {
		final Stage dialog = new Stage();
		dialog.initModality(Modality.APPLICATION_MODAL);
		dialog.initOwner(context.getBean(Stage.class));

		final FXMLLoader fxmlLoader = new FXMLLoader(getClass().getResource(view));
		fxmlLoader.setControllerFactory(context::getBean);

		try {
			dialog.setScene(new Scene(fxmlLoader.load()));
		} catch (final IOException e) {
			log.error("IOException while reading the fxml file for a modal.", e);
		}

		return dialog;
	}
}
