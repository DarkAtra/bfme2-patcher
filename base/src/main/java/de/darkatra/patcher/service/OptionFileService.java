package de.darkatra.patcher.service;

import de.darkatra.patcher.model.Context;
import lombok.extern.slf4j.Slf4j;
import org.springframework.lang.NonNull;
import org.springframework.stereotype.Service;
import org.springframework.util.FileCopyUtils;

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

	@NonNull
	public Optional<Context> readOptionsFile(@NonNull final File file) {
		if (file.exists()) {
			try (final BufferedReader br = new BufferedReader(new FileReader(file))) {
				final Context context = new Context();
				for (String line; (line = br.readLine()) != null; ) {
					final String[] split = line.split("=");
					if (split.length >= 2) {
						final String key = split[0].trim();
						final String value = split[1].trim();
						final Matcher matcher = floatingPointNumberPattern.matcher(value);
						if (matcher.matches()) {
							// try to get it as double
							try {
								context.put(key, Double.parseDouble(matcher.group()));
							} catch (NumberFormatException | NullPointerException e) {
								e.printStackTrace();
							}
						} else {
							// yes -> true, no -> false
							if (value.equalsIgnoreCase("yes") || value.equalsIgnoreCase("no")) {
								context.put(key, value.equalsIgnoreCase("yes"));
							} else {
								context.put(key, value);
							}
						}
					}
				}
				return Optional.of(context);
			} catch (IOException e) {
				log.error("IOException reading the file: " + file.getAbsolutePath(), e);
			}
		}
		return Optional.empty();
	}

	public void writeOptionsFile(@NonNull final File file, @NonNull final Context context) throws IOException {
		if (file.exists()) {
			final File optionsBak = new File(file.getAbsolutePath() + ".bak");
			if (!optionsBak.exists()) {
				FileCopyUtils.copy(file, optionsBak);
			}
		}
		try (final FileWriter fw = new FileWriter(file)) {
			final StringBuilder content = new StringBuilder();
			for (final Map.Entry<String, Object> entry : context.entrySet()) {
				if (entry.getValue().equals(Boolean.TRUE) || entry.getValue().equals(Boolean.FALSE)) {
					content.append(entry.getKey()).append(" = ").append(entry.getValue().equals(Boolean.TRUE) ? "yes" : "no").append("\n");
				} else {
					content.append(entry.getKey()).append(" = ").append(entry.getValue()).append("\n");
				}
			}
			fw.write(content.toString());
			fw.flush();
		}
	}

	@NonNull
	public Context buildDefaultOptionsIni() {
		return new Context()
			.put("AllHealthBars", true)
			.put("AmbientVolume", 30.0)
			.put("AudioLOD", "High")
			.put("Brightness", 50.0)
			.put("FlashTutorial", 0)
			.put("HasSeenLogoMovies", true)
			.put("IdealStaticGameLOD", "VeryLow")
			.put("MovieVolume", 50.0)
			.put("MusicVolume", 50.0)
			.put("Resolution", "1920 1080")
			.put("SFXVolume", 50.0)
			.put("ScrollFactor", 50)
			.put("SendDelay", false)
			.put("StaticGameLOD", "UltraHigh")
			.put("TimesInGame", 1)
			.put("UseEAX3", false)
			.put("VoiceVolume", 50.0);
	}
}
