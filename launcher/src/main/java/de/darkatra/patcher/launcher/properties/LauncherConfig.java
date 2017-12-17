package de.darkatra.patcher.launcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

@Data
@Component
@ConfigurationProperties(prefix = "launcherConfig")
public class LauncherConfig {
	private String serverUrl;
	private String patcherFilePath;
}