package de.darkatra.patcher.config;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class GsonConfiguration {
	@Bean
	@ConditionalOnMissingBean
	public Gson buildGson() {
		return new GsonBuilder().setPrettyPrinting().serializeNulls().create();
	}
}