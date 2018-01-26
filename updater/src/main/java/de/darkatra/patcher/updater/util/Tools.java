package de.darkatra.patcher.updater.util;

import java.io.File;

public class Tools {
	public static boolean runBat(String pfad) {
		try {
			final File scripFile = new File(pfad);
			final ProcessBuilder processBuilder = new ProcessBuilder(scripFile.getAbsolutePath());
			processBuilder.directory(scripFile.getParentFile());
			processBuilder.start();
			return true;
		} catch(Exception ex) {
			return false;
		}
	}
}
