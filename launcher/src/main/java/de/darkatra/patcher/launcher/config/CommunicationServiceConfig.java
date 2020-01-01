package de.darkatra.patcher.launcher.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.service.CommunicationService;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.io.IOException;

@Configuration
public class CommunicationServiceConfig {

	@Bean
	public CommunicationService communicationService(final ObjectMapper objectMapper) throws IOException {
		return new CommunicationService(objectMapper);
	}
}
