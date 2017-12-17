package de.darkatra.patcher.config;

import de.darkatra.patcher.properties.Config;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;

@Configuration
@EnableConfigurationProperties(Config.class)
@Import({ ContextConfiguration.class, GsonConfiguration.class, ServiceConfiguration.class })
public class BaseAutoConfiguration {
}
