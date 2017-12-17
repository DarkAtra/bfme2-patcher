package de.darkatra.patcher.config;

import com.google.gson.Gson;
import de.darkatra.patcher.service.DownloadService;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.service.OptionFileService;
import de.darkatra.patcher.service.PatchService;
import de.darkatra.patcher.service.RegistryService;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class ServiceConfiguration {
	@Bean
	@ConditionalOnMissingBean
	public DownloadService downloadService() {
		return new DownloadService();
	}

	@Bean
	@ConditionalOnMissingBean
	public HashingService hashingService() {
		return new HashingService();
	}

	@Bean
	@ConditionalOnMissingBean
	public OptionFileService optionFileService() {
		return new OptionFileService();
	}

	@Bean
	@ConditionalOnMissingBean
	public PatchService patchService(Gson gson) {
		return new PatchService(gson);
	}

	@Bean
	@ConditionalOnMissingBean
	public RegistryService registryService() {
		return new RegistryService();
	}
}