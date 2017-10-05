package de.darkatra.patcher;

import de.darkatra.patcher.gui.GUIApplication;
import javafx.application.Application;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.ConfigurableApplicationContext;

@Slf4j
@SpringBootApplication
public class PatcherApplication {
	public static void main(String[] args) {
		final ConfigurableApplicationContext applicationContext = SpringApplication.run(PatcherApplication.class, args);
		GUIApplication.setApplicationContext(applicationContext);
		Application.launch(GUIApplication.class);
	}
}