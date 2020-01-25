package de.darkatra.patcher.updatebuilder.config;

import de.darkatra.patcher.updatebuilder.properties.UpdateBuilderProperties;
import de.darkatra.patcher.updatebuilder.service.RegistryService;
import de.darkatra.patcher.updatebuilder.service.model.Context;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;

import java.nio.file.Paths;

@Configuration
@EnableConfigurationProperties(UpdateBuilderProperties.class)
public class ContextConfiguration {

	@Bean
	@Profile("!dev")
	@ConditionalOnMissingBean
	public Context getContext(final UpdateBuilderProperties updateBuilderProperties, final RegistryService registryService) {

		final Context applicationContext = new Context();
		applicationContext.putIfAbsent("serverUrl", updateBuilderProperties.getBaseUrl().toString());
		registryService.findBfME2HomeDirectory().ifPresent(value -> applicationContext.putIfAbsent("bfme2HomeDir", value.toString()));
		registryService.findBfME2UserDirectory().ifPresent(value -> applicationContext.putIfAbsent("bfme2UserDir", value.toString()));
		registryService.findBfME2RotWKHomeDirectory().ifPresent(value -> {
			applicationContext.putIfAbsent("rotwkHomeDir", value.toString());
			applicationContext.putIfAbsent("patcherUserDir", value.resolve(".patcher").toString());
		});
		registryService.findBfME2RotWKUserDirectory().ifPresent(value -> applicationContext.putIfAbsent("rotwkUserDir", value.toString()));
		return applicationContext;
	}

	@Bean
	@Profile("dev")
	@ConditionalOnMissingBean
	public Context getDevContext(final UpdateBuilderProperties updateBuilderProperties) {

		final Context applicationContext = new Context();
		applicationContext.putIfAbsent("serverUrl", updateBuilderProperties.getBaseUrl().toString());
		applicationContext.putIfAbsent("bfme2HomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2/").normalize().toString());
		applicationContext.putIfAbsent("bfme2UserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2/").normalize().toString());
		applicationContext.putIfAbsent("rotwkHomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2ep1/").normalize().toString());
		applicationContext.putIfAbsent("patcherUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2ep1/.patcher").normalize().toString());
		applicationContext.putIfAbsent("rotwkUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2Ep1/").normalize().toString());
		return applicationContext;
	}
}
