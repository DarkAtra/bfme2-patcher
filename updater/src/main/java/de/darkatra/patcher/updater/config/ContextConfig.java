package de.darkatra.patcher.updater.config;

import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.updater.properties.UpdaterConfig;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Configuration;

@Configuration
public class ContextConfig {
	@Autowired
	public void configureContext(Context context, UpdaterConfig updaterConfig) {
		context.putIfAbsent("launcherLocation", updaterConfig.getLauncherLocation());
	}
}
