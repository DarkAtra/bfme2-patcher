package de.darkatra.patcher.updater.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

import javax.validation.constraints.NotEmpty;
import javax.validation.constraints.NotNull;
import java.net.URL;

@Data
@Validated
@ConfigurationProperties(prefix = "updater-properties")
public class UpdaterProperties {

	@NotNull
	private URL serverUrl;

	@NotNull
	private URL patchFilesFolderUrl;

	@NotNull
	private URL patchListUrl;

	@NotEmpty
	private String updaterJarName;

	@NotEmpty
	private String patcherUserDir;
}
