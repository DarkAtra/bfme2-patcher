package de.darkatra.patcher.updatebuilder;

import de.darkatra.patcher.BaseConfiguration;
import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import javafx.application.Application;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Import;

import java.io.File;

@Slf4j
@SpringBootApplication
@Import(BaseConfiguration.class)
public class UpdateBuilderApplication {
    @Autowired
    private void onInit(Config config, Context context) {
        log.debug("CWD: {}", new File(".").getAbsolutePath());
        log.debug("Config: {}", config);
        log.debug("Context: {}", context);
    }

    public static void main(String[] args) {
        final ConfigurableApplicationContext applicationContext = SpringApplication.run(UpdateBuilderApplication.class, args);
        GUIApplication.setApplicationContext(applicationContext);
        Application.launch(GUIApplication.class);
    }


}