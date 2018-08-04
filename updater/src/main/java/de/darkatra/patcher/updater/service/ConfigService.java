package de.darkatra.patcher.updater.service;

import com.google.gson.Gson;
import de.darkatra.patcher.updater.service.model.PatcherConfig;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;

@Service
public class ConfigService {
	private Gson gson;

	@Autowired
	public void setGson(Gson gson) {
		this.gson = gson;
	}

	public PatcherConfig readConfig(final String configFilePath) throws IOException {
		return gson.fromJson(new FileReader(configFilePath), PatcherConfig.class);
	}

	public void writeConfig(final PatcherConfig patcherConfig, final String configFilePath) throws IOException {
		gson.toJson(patcherConfig, new FileWriter(configFilePath));
	}
}
