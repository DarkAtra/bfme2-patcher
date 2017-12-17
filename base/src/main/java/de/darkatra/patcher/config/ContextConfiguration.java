package de.darkatra.patcher.config;

import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.properties.Config;
import de.darkatra.patcher.service.RegistryService;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.net.MalformedURLException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.file.Paths;

@Configuration
public class ContextConfiguration {
	@Bean
	@ConditionalOnMissingBean
	public Context getContext(Config config, RegistryService registryService) throws MalformedURLException, URISyntaxException {
		URL url = new URL(new URL(config.getServerUrl()), config.getPatchFilesFolderPath());
		{
			URI uri = new URI(url.getProtocol(), url.getUserInfo(), url.getHost(), url.getPort(), url.getPath(), url.getQuery(), url.getRef());
			url = uri.toURL();
		}
		Context applicationContext = new Context();
		applicationContext.putIfAbsent("serverUrl", url.toString());
		//		applicationContext.putIfAbsent("patcherUserDir", System.getenv("APPDATA") + "/.bfme2rotwkPatcher/");
		//		registryService.findBfME2HomeDirectory().ifPresent(value->applicationContext.putIfAbsent("bfme2HomeDir", value.toString()));
		//		registryService.findBfME2UserDirectory().ifPresent(value->applicationContext.putIfAbsent("bfme2UserDir", value.toString()));
		//		registryService.findBfME2RotWKHomeDirectory().ifPresent(value->applicationContext.putIfAbsent("rotwkHomeDir", value.toString()));
		//		registryService.findBfME2RotWKUserDirectory().ifPresent(value->applicationContext.putIfAbsent("rotwkUserDir", value.toString()));
		applicationContext.putIfAbsent("patcherUserDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test" + "\\.bfme2rotwkPatcher\\").normalize().toString());
		applicationContext.putIfAbsent("bfme2HomeDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test\\bfme2\\").normalize().toString());
		applicationContext.putIfAbsent("bfme2UserDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test\\userDirBfme2\\").normalize().toString());
		applicationContext.putIfAbsent("rotwkHomeDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test\\bfme2ep1\\").normalize().toString());
		applicationContext.putIfAbsent("rotwkUserDir", Paths.get("C:\\Users\\DarkAtra\\Desktop\\Test\\userDirBfme2Ep1\\").normalize().toString());
		return applicationContext;
	}
}