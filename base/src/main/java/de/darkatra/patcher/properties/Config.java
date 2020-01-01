package de.darkatra.patcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;

import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Paths;

@Data
@ConfigurationProperties(prefix = "patcher-config")
public class Config {
	private String serverUrl = "https://localhost";
	private String patchFilesFolderPath = "patch";
	private String patchListPath = "patch/version.txt";
	private String updaterJarName = "Patcher.jar";
	private String patcherUserDir = Paths.get("patcher").toAbsolutePath().toString();

	public String getPatchFileFolder() throws MalformedURLException, URISyntaxException {
		URL url = new URL(new URL(getServerUrl()), getPatchFilesFolderPath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}
		return url.toString();
	}
}
