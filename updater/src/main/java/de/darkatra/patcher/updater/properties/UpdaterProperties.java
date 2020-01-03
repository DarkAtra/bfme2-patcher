package de.darkatra.patcher.updater.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

import javax.validation.Valid;
import javax.validation.constraints.NotEmpty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Positive;
import java.net.URL;
import java.util.Set;

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

	@NotNull
	private URL patcherUserFolderUrl;

	@Valid
	@NotNull
	private Resolution patcherResolution;

	@NotEmpty
	private Set<@Valid @NotNull Resolution> gameResolutions;

	@Data
	public static class Resolution {
		@Positive
		private int width;
		@Positive
		private int height;
	}
}
