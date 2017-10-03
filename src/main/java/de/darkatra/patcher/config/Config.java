package de.darkatra.patcher.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

@Data
@Configuration
@ConfigurationProperties(prefix = "patcherConfig")
public class Config {
	private String serverUrl;
	private String patchListPath;
}
