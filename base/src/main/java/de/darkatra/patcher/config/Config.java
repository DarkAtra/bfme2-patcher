package de.darkatra.patcher.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

@Data
@Configuration
@ConfigurationProperties(prefix = "patcherConfig")
public class Config {
	private String serverUrl = "https://localhost";
	private String patchFilesFolderPath = "patch";
	private String patchListPath = "patch/version.txt";
}