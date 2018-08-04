package de.darkatra.patcher.updater.properties;

import lombok.Data;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Configuration;

import java.nio.charset.StandardCharsets;
import java.util.Base64;

@Data
@Configuration
public class UpdaterConfig {
	private int launcherPort;
	private String launcherLocation;
	private String configFilePath;

	@Value("${updater.launcherPort}")
	public void setLauncherPort(int launcherPort) {
		this.launcherPort = launcherPort;
	}

	@Value("${updater.launcherLocation}")
	public void setLauncherLocation(String launcherLocation) {
		this.launcherLocation = new String(Base64.getDecoder().decode(launcherLocation), StandardCharsets.UTF_8);
	}

	@Value("${updater.configFilePath}")
	public void setConfigFilePath(String configFilePath) {
		this.configFilePath = configFilePath;
	}
}
