package de.darkatra.patcher.launcher.config;

import com.google.gson.Gson;
import de.darkatra.patcher.service.CommunicationService;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.io.IOException;

@Configuration
public class CommunicationServiceConfig {

	@Bean
	public CommunicationService communicationService(Gson gson) throws IOException {
		return new CommunicationService(gson);
	}
}
