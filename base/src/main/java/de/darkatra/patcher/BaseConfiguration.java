package de.darkatra.patcher;

import de.darkatra.patcher.config.Config;
import de.darkatra.patcher.config.ContextConfig;
import de.darkatra.patcher.config.ServiceConfiguration;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;

@Configuration
@Import({ Config.class, ContextConfig.class, ServiceConfiguration.class })
public class BaseConfiguration {}
