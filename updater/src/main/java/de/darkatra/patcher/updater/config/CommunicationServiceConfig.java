package de.darkatra.patcher.updater.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.service.CommunicationService;
import de.darkatra.patcher.updater.properties.UpdaterConfig;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.io.IOException;
import java.net.InetSocketAddress;

@Configuration
public class CommunicationServiceConfig {
	@Bean
	public CommunicationService communicationService(ObjectMapper objectMapper, UpdaterConfig updaterConfig) throws IOException {
		return new CommunicationService(objectMapper, new InetSocketAddress("localhost", updaterConfig.getLauncherPort()));
	}
}
