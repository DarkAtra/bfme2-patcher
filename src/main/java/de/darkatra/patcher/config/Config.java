package de.darkatra.patcher.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

@Data
@Component
@ConfigurationProperties(prefix = "patcherconfig")
public class Config {
	private String serverUrl;
	private String patchlistPath;
}
