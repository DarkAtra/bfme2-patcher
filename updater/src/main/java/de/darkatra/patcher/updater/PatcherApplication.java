package de.darkatra.patcher.updater;

import de.darkatra.patcher.BaseConfiguration;
import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.updater.gui.GUIApplication;
import javafx.application.Application;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Import;

import java.io.File;
import java.util.Arrays;
import java.util.stream.Collectors;

@Slf4j
@SpringBootApplication
@Import(BaseConfiguration.class)
public class PatcherApplication implements ApplicationRunner {
	@Autowired
	private void onInit(Config config, Context context) {
		log.debug("CWD: {}", new File(".").getAbsolutePath());
		log.debug("Config: {}", config);
		log.debug("Context: {}", context);
	}

	@Override
	public void run(ApplicationArguments args) throws Exception {
		log.debug("CmdArguments: {}", Arrays.toString(args.getSourceArgs()));
		log.debug("NonOptionArguments: {}", args.getNonOptionArgs());
		log.debug("OptionArguments: {}", args.getOptionNames().stream().map(arg->arg + ": " + args.getOptionValues(arg)).collect(Collectors.toList()));
	}

	public static void main(String[] args) {
		final ConfigurableApplicationContext applicationContext = SpringApplication.run(PatcherApplication.class, args);
		GUIApplication.setApplicationContext(applicationContext);
		Application.launch(GUIApplication.class);
	}
}