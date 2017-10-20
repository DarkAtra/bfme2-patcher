package de.darkatra.patcher.updater.config;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import org.springframework.context.annotation.Bean;
import org.springframework.stereotype.Component;

@Component
public class GsonConfigurator {
	@Bean
	public Gson buildGson() {
		return new GsonBuilder().setPrettyPrinting().serializeNulls().create();
	}
}