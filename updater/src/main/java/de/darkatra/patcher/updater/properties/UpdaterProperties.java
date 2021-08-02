package de.darkatra.patcher.updater.properties;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.ConstructorBinding;

import javax.validation.Valid;
import javax.validation.constraints.NotBlank;
import javax.validation.constraints.NotEmpty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Positive;
import java.net.URL;
import java.util.Set;

@Getter
//@Validated // FIXME: currently does not work with the latest spring boot version and jigsaw modules
@ConstructorBinding
@RequiredArgsConstructor
@ConfigurationProperties(prefix = "updater-properties")
public class UpdaterProperties {

	public static final String UPDATER_NAME = "updater.jar";
	public static final String UPDATER_TEMP_NAME = "_updater.jar";
	public static final String UPDATER_OLD_NAME = "updater.old.jar";

	@NotNull
	private final URL baseUrl;

	@NotNull
	private final URL updaterUrl;

	@NotNull
	private final URL updaterIconUrl;

	@NotNull
	private final URL patchListUrl;

	@NotBlank
	private final String version;

	@Valid
	@NotNull
	private final Resolution updaterResolution;

	@NotEmpty
	private final Set<@Valid @NotNull Resolution> gameResolutions;

	@Getter
	@ConstructorBinding
	@RequiredArgsConstructor
	public static class Resolution {

		@Positive
		private final int width;

		@Positive
		private final int height;
	}
}
