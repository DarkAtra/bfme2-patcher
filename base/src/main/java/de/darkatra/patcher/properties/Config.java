package de.darkatra.patcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

import javax.validation.constraints.NotNull;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Paths;

@Data
@Validated
@ConfigurationProperties(prefix = "patcher-config")
public class Config {
	@NotNull
	private URL serverUrl;
	private String patchFilesFolderPath = "patch";
	private String patchListPath = "patch/version.txt";
	private String updaterJarName = "Patcher.jar";
	private String patcherUserDir = Paths.get("patcher").toAbsolutePath().toString();

	public String getPatchFileFolder() throws MalformedURLException, URISyntaxException {
		final URL url = new URL(getServerUrl(), getPatchFilesFolderPath());
		return new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef()).toURL().toString();
	}
}
