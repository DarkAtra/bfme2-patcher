package de.darkatra.patcher.updatebuilder;

import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.stage.Stage;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

@Slf4j
@SpringBootApplication
public class UpdateBuilderApplication extends Application {

	private ConfigurableApplicationContext context;
	@Getter
	private Stage mainStage;
	private Parent mainWindow;

	@Override
	public void init() throws Exception {

		final SpringApplicationBuilder builder = new SpringApplicationBuilder(UpdateBuilderApplication.class);
		context = builder.run(getParameters().getRaw().toArray(new String[0]));

		final FXMLLoader fxmlLoader = new FXMLLoader(getClass().getResource("/view/main-window.fxml"));
		fxmlLoader.setControllerFactory(context::getBean);
		mainWindow = fxmlLoader.load();
	}

	@Override
	public void start(final Stage primaryStage) {

		mainStage = primaryStage;
		primaryStage.setScene(new Scene(mainWindow, 600, 400));
		primaryStage.centerOnScreen();
		primaryStage.setResizable(false);
		primaryStage.show();
	}

	@Override
	public void stop() {
		context.close();
	}

	public static void main(final String[] args) {
		Application.launch(UpdateBuilderApplication.class);
	}
}
