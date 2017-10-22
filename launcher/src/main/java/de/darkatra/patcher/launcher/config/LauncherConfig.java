package de.darkatra.patcher.launcher.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

@Data
@Configuration
@ConfigurationProperties(prefix = "launcherConfig")
public class LauncherConfig {
	private String serverUrl;
	private String patcherFilePath;
}