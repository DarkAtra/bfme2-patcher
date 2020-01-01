package de.darkatra.patcher.config;

import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.properties.Config;
import de.darkatra.patcher.service.RegistryService;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;

import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.nio.file.Paths;

@Configuration
public class ContextConfiguration {
	@Bean
	@Profile("!dev")
	@ConditionalOnMissingBean
	public Context getContext(final Config config, final RegistryService registryService) throws MalformedURLException, URISyntaxException {

		final Context applicationContext = new Context()
			.putIfAbsent("serverUrl", config.getPatchFileFolder())
			.putIfAbsent("patcherUserDir", config.getPatcherUserDir());
		registryService.findBfME2HomeDirectory().ifPresent(value -> applicationContext.putIfAbsent("bfme2HomeDir", value.toString()));
		registryService.findBfME2UserDirectory().ifPresent(value -> applicationContext.putIfAbsent("bfme2UserDir", value.toString()));
		registryService.findBfME2RotWKHomeDirectory().ifPresent(value -> applicationContext.putIfAbsent("rotwkHomeDir", value.toString()));
		registryService.findBfME2RotWKUserDirectory().ifPresent(value -> applicationContext.putIfAbsent("rotwkUserDir", value.toString()));
		return applicationContext;
	}

	@Bean
	@Profile("dev")
	@ConditionalOnMissingBean
	public Context getDevContext(final Config config) throws MalformedURLException, URISyntaxException {

		return new Context()
			.putIfAbsent("serverUrl", config.getPatchFileFolder())
			.putIfAbsent("patcherUserDir", config.getPatcherUserDir())
			.putIfAbsent("patcherUserDir", Paths.get(System.getProperty("user.home"), "Desktop\\Test" + "\\.patcher\\").normalize().toString())
			.putIfAbsent("bfme2HomeDir", Paths.get(System.getProperty("user.home"), "Desktop\\Test\\bfme2\\").normalize().toString())
			.putIfAbsent("bfme2UserDir", Paths.get(System.getProperty("user.home"), "Desktop\\Test\\userDirBfme2\\").normalize().toString())
			.putIfAbsent("rotwkHomeDir", Paths.get(System.getProperty("user.home"), "Desktop\\Test\\bfme2ep1\\").normalize().toString())
			.putIfAbsent("rotwkUserDir", Paths.get(System.getProperty("user.home"), "Desktop\\Test\\userDirBfme2Ep1\\").normalize().toString());
	}
}
