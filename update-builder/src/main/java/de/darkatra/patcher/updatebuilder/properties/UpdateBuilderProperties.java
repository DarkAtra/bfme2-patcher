package de.darkatra.patcher.updatebuilder.properties;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.ConstructorBinding;

import javax.validation.constraints.NotBlank;
import javax.validation.constraints.NotNull;
import java.net.URL;

@Getter
@ConstructorBinding
@RequiredArgsConstructor
//@Validated // FIXME: currently does not work with the latest spring boot version and jigsaw modules
@ConfigurationProperties(prefix = "update-builder-properties")
public class UpdateBuilderProperties {

	@NotNull
	private final URL baseUrl;

	@NotBlank
	private final String version;
}
