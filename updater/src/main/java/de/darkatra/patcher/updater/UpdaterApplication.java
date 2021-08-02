package de.darkatra.patcher.updater;

import static de.darkatra.patcher.updater.service.UpdateService.INSTALL_PARAMETER;
import static de.darkatra.patcher.updater.service.UpdateService.UNINSTALL_CURRENT_PARAMETER;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.UpdateService;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.util.UIUtils;
import javafx.application.Application;
import javafx.application.HostServices;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.image.Image;
import javafx.stage.Stage;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.ObjectFactory;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.IOException;

@Slf4j
@SpringBootApplication(proxyBeanMethods = false)
public class UpdaterApplication extends Application {

	private ConfigurableApplicationContext context;
	private Parent mainWindow;

	@Override
	public void init() throws Exception {

		final SpringApplicationBuilder builder = new SpringApplicationBuilder(UpdaterApplication.class);
		builder.initializers(applicationContext -> applicationContext.getBeanFactory().registerResolvableDependency(
			HostServices.class,
			(ObjectFactory<HostServices>) this::getHostServices)
		);
		context = builder.run(getParameters().getRaw().toArray(new String[0]));

		final FXMLLoader fxmlLoader = new FXMLLoader(getClass().getResource("/view/main-window.fxml"));
		fxmlLoader.setControllerFactory(context::getBean);
		mainWindow = fxmlLoader.load();
	}

	@Override
	public void start(final Stage primaryStage) throws IOException, InterruptedException {

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

		final UpdateService updateService = context.getBean(UpdateService.class);

		if (!updateService.isInCorrectLocation()) {
			updateService.moveToCorrectLocation();
			Platform.exit();
			return;
		}

		final Parameters parameters = getParameters();

		if (parameters.getRaw().contains(UNINSTALL_CURRENT_PARAMETER)) {
			updateService.performUninstallation();
			Platform.exit();
			return;
		} else if (parameters.getRaw().contains(INSTALL_PARAMETER)) {
			updateService.performInstallation();
			Platform.exit();
			return;
		}

		updateService.performCleanup();

		context.getBeanFactory().registerSingleton("primaryStage", primaryStage);

		final UpdaterProperties updaterProperties = context.getBean(UpdaterProperties.class);
		primaryStage.setScene(new Scene(mainWindow, updaterProperties.getUpdaterResolution().getWidth(), updaterProperties.getUpdaterResolution().getHeight()));
		primaryStage.setTitle("Bfme2 Mod Launcher");
		primaryStage.getIcons().add(new Image(getClass().getResourceAsStream("/images/icon.png")));
		primaryStage.centerOnScreen();
		primaryStage.setResizable(false);
		primaryStage.show();
	}

	@Override
	public void stop() {
		context.close();
	}

	public static void main(final String[] args) {
		Application.launch(UpdaterApplication.class, args);
	}
}
