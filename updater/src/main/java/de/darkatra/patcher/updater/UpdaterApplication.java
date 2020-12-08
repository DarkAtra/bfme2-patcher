package de.darkatra.patcher.updater;

import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.util.UIUtils;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.stage.Stage;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

@Slf4j
@SpringBootApplication(proxyBeanMethods = false)
public class UpdaterApplication extends Application {

	private ConfigurableApplicationContext context;
	private Parent mainWindow;

	@Override
	public void init() throws Exception {

		final SpringApplicationBuilder builder = new SpringApplicationBuilder(UpdaterApplication.class);
		context = builder.run(getParameters().getRaw().toArray(new String[0]));

		final FXMLLoader fxmlLoader = new FXMLLoader(getClass().getResource("/view/main-window.fxml"));
		fxmlLoader.setControllerFactory(context::getBean);
		mainWindow = fxmlLoader.load();
	}

	@Override
	public void start(final Stage primaryStage) {

		// make sure that all folders are available
		final Context patcherContext = context.getBean(Context.class);
		if (!(patcherContext.containsKey("serverUrl")
			  && patcherContext.containsKey("patcherUserDir")
			  && patcherContext.containsKey("bfme2UserDir")
			  && patcherContext.containsKey("bfme2HomeDir")
			  && patcherContext.containsKey("rotwkUserDir")
			  && patcherContext.containsKey("rotwkHomeDir"))) {

			UIUtils.alert(
				Alert.AlertType.ERROR,
				"Error",
				"Game Error",
				"Could not find the game. Is it installed?"
			).showAndWait();
			Platform.exit();
			return;
		}

		final UpdaterProperties updaterProperties = context.getBean(UpdaterProperties.class);
		primaryStage.setScene(new Scene(mainWindow, updaterProperties.getUpdaterResolution().getWidth(), updaterProperties.getUpdaterResolution().getHeight()));
		primaryStage.centerOnScreen();
		primaryStage.setResizable(false);
		primaryStage.show();
	}

	@Override
	public void stop() {
		context.close();
	}

	public static void main(final String[] args) {
		Application.launch(UpdaterApplication.class);
	}
}
