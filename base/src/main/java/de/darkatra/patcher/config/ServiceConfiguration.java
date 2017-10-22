package de.darkatra.patcher.config;

import com.google.gson.Gson;
import de.darkatra.patcher.service.DownloadService;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.service.OptionFileService;
import de.darkatra.patcher.service.PatchService;
import de.darkatra.patcher.service.RegistryService;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class ServiceConfiguration {
	@Bean
	public DownloadService downloadService() {
		return new DownloadService();
	}
	@Bean
	public HashingService hashingService() {
		return new HashingService();
	}
	@Bean
	public OptionFileService optionFileService() {
		return new OptionFileService();
	}
	@Bean
	public PatchService patchService(Gson gson) {
		return new PatchService(gson);
	}
	@Bean
	public RegistryService registryService() {
		return new RegistryService();
	}
}