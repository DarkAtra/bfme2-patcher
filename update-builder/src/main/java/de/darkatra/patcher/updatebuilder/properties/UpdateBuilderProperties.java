package de.darkatra.patcher.updatebuilder.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

import javax.validation.constraints.NotBlank;
import javax.validation.constraints.NotNull;
import java.net.URL;

@Data
@Validated
@ConfigurationProperties(prefix = "update-builder-properties")
public class UpdateBuilderProperties {

	@NotNull
	private URL baseUrl;

	@NotBlank
	private String version;
}
