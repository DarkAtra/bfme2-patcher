package de.darkatra.patcher.updater.config;

import de.darkatra.patcher.updater.feign.LauncherClient;
import feign.Feign;
import feign.gson.GsonDecoder;
import feign.gson.GsonEncoder;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class FeignClientConfig {
	@Bean
	public GsonEncoder gsonEncoder() {
		return new GsonEncoder();
	}

	@Bean
	public GsonDecoder gsonDecoder() {
		return new GsonDecoder();
	}

	@Bean
	public LauncherClient launcherClient() {
		return Feign.builder().encoder(gsonEncoder()).decoder(gsonDecoder()).target(LauncherClient.class, "localhost");
	}
}