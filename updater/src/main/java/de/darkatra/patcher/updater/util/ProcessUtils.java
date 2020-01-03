package de.darkatra.patcher.updater.util;

import java.io.File;
import java.nio.file.Path;

public class ProcessUtils {
	public static boolean run(final Path path) {
		try {
			final File scripFile = path.toFile();
			final ProcessBuilder processBuilder = new ProcessBuilder(scripFile.getAbsolutePath());
			processBuilder.directory(scripFile.getParentFile());
			processBuilder.start();
			return true;
		} catch (final Exception ex) {
			return false;
		}
	}
}
