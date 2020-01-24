package de.darkatra.patcher.updater.util;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class ProcessUtils {

	public static Process run(final String command, final String... args) {
		return run(command, args);
	}

	public static Process run(final Path executable, final String... args) throws IOException {
		return run(executable.toFile(), args);
	}

	public static Process run(final File executable, final String... args) throws IOException {
		return run(executable, executable.getParentFile(), args);
	}

	public static Process run(final File executable, final File workingDirectory, final String... args) throws IOException {

		return run(executable.getAbsolutePath(), workingDirectory, args);
	}

	public static Process run(final String command, final File workingDirectory, final String... args) throws IOException {
		final ProcessBuilder processBuilder = new ProcessBuilder(Stream.concat(Stream.of(command), Stream.of(args)).collect(Collectors.toList()));
		processBuilder.directory(workingDirectory);
		return processBuilder.start();
	}
}
