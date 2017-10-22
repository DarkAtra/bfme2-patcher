package de.darkatra.patcher.service;

import de.darkatra.patcher.model.Context;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.jetbrains.annotations.NotNull;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Map;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Slf4j
@Service
public class OptionFileService {
	private final Pattern floatingPointNumberPattern = Pattern.compile("[+-]?([0-9]*[.])?[0-9]+");

	public Optional<Context> readOptionsFile(@NotNull File file) {
		if(file.exists()) {
			try(BufferedReader br = new BufferedReader(new FileReader(file))) {
				Context context = new Context();
				for(String line; (line = br.readLine()) != null; ) {
					String[] split = line.split("=");
					if(split.length >= 2) {
						String key = split[0].trim();
						String value = split[1].trim();
						Matcher matcher = floatingPointNumberPattern.matcher(value);
						if(matcher.matches()) {
							// try to get it as double
							try {
								context.put(key, Double.parseDouble(matcher.group()));
							} catch(NumberFormatException | NullPointerException e) {
								e.printStackTrace();
							}
						} else {
							// yes -> true, no -> false
							if(value.equalsIgnoreCase("yes") || value.equalsIgnoreCase("no")) {
								context.put(key, value.equalsIgnoreCase("yes"));
							} else {
								context.put(key, value);
							}
						}
					}
				}
				return Optional.of(context);
			} catch(IOException e) {
				log.error("IOException reading the file: " + file.getAbsolutePath(), e);
			}
		}
		return Optional.empty();
	}

	public void writeOptionsFile(@NotNull File file, @NotNull Context context) throws IOException {
		if(file.exists()) {
			File optionsBak = new File(file.getAbsolutePath() + ".bak");
			if(!optionsBak.exists()) {
				FileUtils.copyFile(file, optionsBak);
			}
		}
		try(FileWriter fw = new FileWriter(file)) {
			StringBuilder content = new StringBuilder();
			for(Map.Entry<String, Object> entry : context.entrySet()) {
				if(entry.getValue().equals(Boolean.TRUE) || entry.getValue().equals(Boolean.FALSE)) {
					content.append(entry.getKey()).append(" = ").append(entry.getValue().equals(Boolean.TRUE) ? "yes" : "no").append("\n");
				} else {
					content.append(entry.getKey()).append(" = ").append(entry.getValue()).append("\n");
				}
			}
			fw.write(content.toString());
			fw.flush();
		}
	}

	public Context buildDefaultOptionsIni() {
		Context context = new Context();
		context.put("AllHealthBars", true);
		context.put("AmbientVolume", 30.0);
		context.put("AudioLOD", "High");
		context.put("Brightness", 50.0);
		context.put("FlashTutorial", 0);
		context.put("HasSeenLogoMovies", true);
		context.put("IdealStaticGameLOD", "VeryLow");
		context.put("MovieVolume", 50.0);
		context.put("MusicVolume", 50.0);
		context.put("Resolution", "1920 1080");
		context.put("SFXVolume", 50.0);
		context.put("ScrollFactor", 50);
		context.put("SendDelay", false);
		context.put("StaticGameLOD", "UltraHigh");
		context.put("TimesInGame", 1);
		context.put("UseEAX3", false);
		context.put("VoiceVolume", 50.0);
		return context;
	}
}