package de.darkatra.patcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;

@Data
@ConfigurationProperties(prefix = "patcherConfig")
public class Config {
	private String serverUrl = "https://localhost";
	private String patchFilesFolderPath = "patch";
	private String patchListPath = "patch/version.txt";
}