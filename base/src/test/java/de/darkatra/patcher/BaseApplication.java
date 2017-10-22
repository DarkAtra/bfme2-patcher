package de.darkatra.patcher;

import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.model.Context;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.autoconfigure.SpringBootApplication;

import java.io.File;

@Slf4j
@SpringBootApplication
public class BaseApplication {
	@Autowired
	private void onInit(Config config, Context context) {
		log.debug("CWD: {}", new File(".").getAbsolutePath());
		log.debug("Config: {}", config);
		log.debug("Context: {}", context);
	}
}