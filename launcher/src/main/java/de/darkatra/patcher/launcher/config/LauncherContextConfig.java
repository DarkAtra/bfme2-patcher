package de.darkatra.patcher.launcher.config;

import de.darkatra.patcher.model.Context;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Paths;

@Configuration
public class LauncherContextConfig {
	@Bean
	public Context launcherContext(LauncherConfig config) throws MalformedURLException, URISyntaxException {
		URL url = new URL(new URL(config.getServerUrl()), config.getPatcherFilePath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}
		Context applicationContext = new Context();
		applicationContext.putIfAbsent("patcherServerFilePath", url.toString());
		//		applicationContext.putIfAbsent("patcherUserDir", System.getenv("APPDATA") + "/.bfme2rotwkPatcher/");
		applicationContext.putIfAbsent("patcherUserDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test" + "\\.bfme2rotwkPatcher\\").normalize().toString());
		return applicationContext;
	}
}