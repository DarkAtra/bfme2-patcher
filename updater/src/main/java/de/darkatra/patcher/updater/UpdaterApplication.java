package de.darkatra.patcher.updater;

import de.darkatra.patcher.updater.properties.UpdaterProperties;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.stage.Stage;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

import java.util.Arrays;
import java.util.stream.Collectors;

@Slf4j
@SpringBootApplication
public class UpdaterApplication extends Application implements ApplicationRunner {

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

	@Override
	public void run(final ApplicationArguments args) {
		log.debug("CmdArguments: {}", Arrays.toString(args.getSourceArgs()));
		log.debug("NonOptionArguments: {}", args.getNonOptionArgs());
		log.debug("OptionArguments: {}", args.getOptionNames().stream().map(arg -> arg + ": " + args.getOptionValues(arg)).collect(Collectors.toList()));
	}

	public static void main(String[] args) {
		Application.launch(UpdaterApplication.class);
	}
}
