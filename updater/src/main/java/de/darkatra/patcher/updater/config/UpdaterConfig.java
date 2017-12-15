package de.darkatra.patcher.updater.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Configuration;

@Configuration
public class UpdaterConfig {
	private int launcherPort;

	public int getLauncherPort() {
		return launcherPort;
	}

	@Value("${updater.launcherPort}")
	public void setLauncherPort(int launcherPort) {
		this.launcherPort = launcherPort;
	}
}
