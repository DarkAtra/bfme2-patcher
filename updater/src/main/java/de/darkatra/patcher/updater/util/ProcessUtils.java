package de.darkatra.patcher.updater.util;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;

public class ProcessUtils {

	public static Process run(final Path executable) throws IOException {
		return run(executable.toFile());
	}

	public static Process run(final File executable) throws IOException {
		return run(executable, executable.getParentFile());
	}

	public static Process run(final File executable, final File workingDirectory) throws IOException {
		final ProcessBuilder processBuilder = new ProcessBuilder(executable.getAbsolutePath());
		processBuilder.directory(workingDirectory);
		return processBuilder.start();
	}
}
