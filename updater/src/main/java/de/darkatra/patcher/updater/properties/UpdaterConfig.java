package de.darkatra.patcher.updater.properties;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Configuration;

import java.nio.charset.StandardCharsets;
import java.util.Base64;

@Configuration
public class UpdaterConfig {
	private int launcherPort;
	private String launcherLocation;

	public int getLauncherPort() {
		return launcherPort;
	}

	public String getLauncherLocation() {
		return launcherLocation;
	}

	@Value("${updater.launcherPort}")
	public void setLauncherPort(int launcherPort) {
		this.launcherPort = launcherPort;
	}

	@Value("${updater.launcherLocation}")
	public void setLauncherLocation(String launcherLocation) {
		this.launcherLocation = new String(Base64.getDecoder().decode(launcherLocation), StandardCharsets.UTF_8);
	}
}
