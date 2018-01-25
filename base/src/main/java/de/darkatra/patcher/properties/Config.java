package de.darkatra.patcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;

import java.nio.file.Paths;

@Data
@ConfigurationProperties(prefix = "patcherConfig")
public class Config {
	private String serverUrl = "https://localhost";
	private String patchFilesFolderPath = "patch";
	private String patchListPath = "patch/version.txt";
	private String updaterJarName = "Patcher.jar";
	private String patcherUserDir = Paths.get("patcher").toAbsolutePath().toString();
}