package de.darkatra.patcher.launcher.properties;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;

@Data
@Component
@ConfigurationProperties(prefix = "launcherConfig")
public class LauncherConfig {
	private String serverUrl;
	private String patcherFilePath;
	private String patcherUserDir;

	public String getPatcherServerFilePath() throws URISyntaxException, MalformedURLException {
		URL url = new URL(new URL(getServerUrl()), getPatcherFilePath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}
		return url.toString();
	}
}