package de.darkatra.patcher.updater.service;

import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.util.FileCopyUtils;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Hashtable;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Slf4j
@Service
public class OptionFileService {
	private final Pattern floatingPointNumberPattern = Pattern.compile("[+-]?([0-9]*[.])?[0-9]+");

	public Hashtable<String, Object> readOptionsFile(final File file) throws IOException {
		final Hashtable<String, Object> options = new Hashtable<>();
		if (file.exists()) {
			try (final BufferedReader br = new BufferedReader(new FileReader(file))) {
				String line;
				while ((line = br.readLine()) != null) {
					final String[] split = line.split("=");
					if (split.length >= 2) {
						final String key = split[0].trim();
						final String value = split[1].trim();
						final Matcher matcher = floatingPointNumberPattern.matcher(value);
						if (matcher.matches()) {
							try {
								options.put(key, matcher.group());
							} catch (final NumberFormatException e) {
								log.error("NumberFormatException while reading the options file.", e);
							}
						} else {
							if (value.equalsIgnoreCase("yes") || value.equalsIgnoreCase("no")) {
								options.put(key, value.equalsIgnoreCase("yes"));
							} else {
								options.put(key, value);
							}
						}
					}
				}
			}
		}
		return options;
	}

	public void writeOptionsFile(final File file, final Hashtable<String, Object> options) throws IOException {
		if (file.exists()) {
			final File optionsBak = new File(file.getAbsolutePath() + ".bak");
			if (!optionsBak.exists()) {
				FileCopyUtils.copy(file, optionsBak);
			}
		}
		try (final FileWriter fw = new FileWriter(file)) {
			final StringBuilder content = new StringBuilder();
			for (final Map.Entry<String, Object> entry : options.entrySet()) {
				final String key = entry.getKey();
				final Object value = entry.getValue();
				if (entry.getValue() == Boolean.TRUE || entry.getValue() == Boolean.FALSE) {
					content.append(key).append(" = ").append(value == Boolean.TRUE ? "yes" : "no").append("\n");
				} else {
					content.append(key).append(" = ").append(value).append("\n");
				}
			}
			fw.write(content.toString());
			fw.flush();
		}
	}

	public Hashtable<String, Object> buildDefaultOptions() {
		final Hashtable<String, Object> options = new Hashtable<>();
		options.put("AllHealthBars", true);
		options.put("AmbientVolume", 30.0);
		options.put("AudioLOD", "High");
		options.put("Brightness", 50.0);
		options.put("FlashTutorial", 0);
		options.put("HasSeenLogoMovies", true);
		options.put("IdealStaticGameLOD", "VeryLow");
		options.put("MovieVolume", 50.0);
		options.put("MusicVolume", 50.0);
		options.put("Resolution", "1920 1080");
		options.put("SFXVolume", 50.0);
		options.put("ScrollFactor", 50);
		options.put("SendDelay", false);
		options.put("StaticGameLOD", "UltraHigh");
		options.put("TimesInGame", 1);
		options.put("UseEAX3", false);
		options.put("VoiceVolume", 50.0);
		return options;
	}
}
