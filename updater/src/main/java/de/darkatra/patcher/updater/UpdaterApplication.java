package de.darkatra.patcher.updater;

import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_NAME;
import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_OLD_NAME;
import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_TEMP_NAME;
import static de.darkatra.patcher.updater.service.UpdateService.INSTALL_PARAMETER;
import static de.darkatra.patcher.updater.service.UpdateService.UNINSTALL_CURRENT_PARAMETER;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.util.ProcessUtils;
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

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;

@Slf4j
@SpringBootApplication(proxyBeanMethods = false)
public class UpdaterApplication extends Application {

	public static final int MAX_RENAME_ATTEMPTS = 7;

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
	public void start(final Stage primaryStage) throws IOException {

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

		final Parameters parameters = getParameters();
		final Path patcherUserDir = Path.of(patcherContext.get("patcherUserDir"));
		final Path currentUpdaterLocation = patcherUserDir.resolve(UPDATER_NAME);
		final Path updaterTempLocation = patcherUserDir.resolve(UPDATER_TEMP_NAME);
		final Path oldUpdaterLocation = patcherUserDir.resolve(UPDATER_OLD_NAME);

		if (parameters.getRaw().contains(UNINSTALL_CURRENT_PARAMETER)) {

			deleteFile(oldUpdaterLocation.toFile());
			if (attemptRename(currentUpdaterLocation, oldUpdaterLocation)) {
				ProcessUtils.runJar(oldUpdaterLocation, INSTALL_PARAMETER);
			}

			Platform.exit();
			return;
		} else if (parameters.getRaw().contains(INSTALL_PARAMETER)) {

			if (attemptRename(updaterTempLocation, currentUpdaterLocation)) {
				ProcessUtils.runJar(currentUpdaterLocation);
			}

			Platform.exit();
			return;
		}

		deleteFile(oldUpdaterLocation.toFile());

		context.getBeanFactory().registerSingleton("primaryStage", primaryStage);

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

	private boolean attemptRename(final Path from, final Path to) {
		for (int i = 0; i < MAX_RENAME_ATTEMPTS; i++) {
			if (from.toFile().renameTo(to.toFile())) {
				return true;
			}
			try {
				Thread.sleep(100L * (i + 1));
			} catch (final InterruptedException e) {
				return false;
			}
		}
		return false;
	}

	private void deleteFile(final File fileToDelete) {
		if (fileToDelete.exists()) {
			if (!fileToDelete.delete()) {
				log.error("Could not delete file: " + fileToDelete.getAbsolutePath());
			}
		}
	}

	public static void main(final String[] args) {
		Application.launch(UpdaterApplication.class, args);
	}
}
