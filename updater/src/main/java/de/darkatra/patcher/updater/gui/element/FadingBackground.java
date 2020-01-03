package de.darkatra.patcher.updater.gui.element;

import javafx.animation.Animation;
import javafx.animation.FadeTransition;
import javafx.animation.ParallelTransition;
import javafx.animation.PauseTransition;
import javafx.animation.SequentialTransition;
import javafx.application.Platform;
import javafx.scene.layout.Pane;
import javafx.scene.layout.StackPane;
import javafx.util.Duration;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.DisposableBean;
import org.springframework.stereotype.Component;

@Slf4j
@Component
public class FadingBackground extends StackPane implements DisposableBean {

	private final String[] imagePaths = new String[]{
		"/images/splash2_1920x1080.jpg",
		"/images/splash10_1920x1080.jpg",
		"/images/splash12_1920x1080.jpg",
		"/images/splash13_1920x1080.jpg"
	};
	private final SequentialTransition backgroundTransition = new SequentialTransition();
	private final Pane fadeOut = new Pane();
	private final Pane fadeIn = new Pane();
	private int currentImage = 0;

	public FadingBackground() {

		fadeOut.maxWidth(Double.MAX_VALUE);
		fadeOut.maxHeight(Double.MAX_VALUE);
		fadeOut.setOpacity(1);
		setBackgroundImage(fadeOut, currentImage);

		fadeIn.maxWidth(Double.MAX_VALUE);
		fadeIn.maxHeight(Double.MAX_VALUE);
		fadeIn.setOpacity(0);
		setBackgroundImage(fadeIn, ++currentImage);

		getChildren().addAll(fadeOut, fadeIn);

		final ParallelTransition fadeTransitions = new ParallelTransition(
			getFadeTransition(fadeIn, 0, 1),
			getFadeTransition(fadeOut, 1, 0)
		);
		fadeTransitions.setOnFinished(event -> Platform.runLater(() -> {
			setBackgroundImage(fadeOut, currentImage);
			fadeOut.setOpacity(1);
			fadeIn.setOpacity(0);
			currentImage = ++currentImage % imagePaths.length;
			setBackgroundImage(fadeIn, currentImage);
		}));

		backgroundTransition.setDelay(Duration.seconds(2));
		backgroundTransition.setCycleCount(Animation.INDEFINITE);
		backgroundTransition.getChildren().addAll(fadeTransitions, new PauseTransition(Duration.seconds(2)));
		backgroundTransition.playFromStart();
	}

	@Override
	public void destroy() {
		backgroundTransition.stop();
	}

	private void setBackgroundImage(final Pane pane, final int currentImage) {
		pane.setStyle("-fx-background-image: url('" + getClass().getResource(imagePaths[currentImage]).toExternalForm() + "');\n" +
					  "-fx-background-color: transparent;\n" +
					  "-fx-background-repeat: stretch;\n" +
					  "-fx-background-size: stretch;\n" +
					  "-fx-background-position: center center;");
	}

	private FadeTransition getFadeTransition(final Pane pane, double from, double to) {
		final FadeTransition transition = new FadeTransition(Duration.seconds(2), pane);
		transition.setFromValue(from);
		transition.setToValue(to);
		return transition;
	}
}
