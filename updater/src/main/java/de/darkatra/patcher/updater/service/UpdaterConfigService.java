package de.darkatra.patcher.updater.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.updater.service.model.UpdaterConfig;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;

@Service
@RequiredArgsConstructor
public class UpdaterConfigService {
	private final ObjectMapper objectMapper;

	public UpdaterConfig readConfig(final String configFilePath) throws IOException {
		return objectMapper.readValue(new FileReader(configFilePath), UpdaterConfig.class);
	}

	public void writeConfig(final UpdaterConfig updaterConfig, final String configFilePath) throws IOException {
		objectMapper.writeValue(new File(configFilePath), updaterConfig);
	}
}
