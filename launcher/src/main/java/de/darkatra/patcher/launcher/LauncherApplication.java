package de.darkatra.patcher.launcher;

import de.darkatra.patcher.BaseConfiguration;
import de.darkatra.patcher.launcher.service.CommunicationService;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.service.DownloadService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Import;
import org.springframework.scheduling.annotation.EnableAsync;

import javax.swing.JOptionPane;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;

@Slf4j
@EnableAsync
@SpringBootApplication
@Import(BaseConfiguration.class)
public class LauncherApplication implements ApplicationRunner {
	private final Context context;
	private final DownloadService downloadService;
	private final CommunicationService communicationService;

	public LauncherApplication(@Qualifier("launcherContext") Context context, DownloadService downloadService, CommunicationService communicationService) {
		this.context = context;
		this.downloadService = downloadService;
		this.communicationService = communicationService;
		communicationService.addListener(message->{
			if(message.equalsIgnoreCase("true")) {
				log.debug("Exit");
				System.exit(0);
			}
		});
	}

	@Override
	public void run(ApplicationArguments args) throws Exception {
		final Optional<String> patcherUserDir = context.getString("patcherUserDir");
		final Optional<String> patcherServerFilePath = context.getString("patcherServerFilePath");
		if(patcherUserDir.isPresent() && patcherServerFilePath.isPresent()) {
			final File patcherJarPath = Paths.get(patcherUserDir.get(), "Patcher.jar").normalize().toFile();
			boolean isInitialDownload = !patcherJarPath.exists();
			if(isInitialDownload) {
				final boolean downloadSucceed = downloadService.downloadFile(patcherServerFilePath.get(), patcherJarPath.getAbsolutePath());
				if(!downloadSucceed) {
					JOptionPane.showMessageDialog(null, "Could not download the updater. Try again with admin privileges.", "Error", JOptionPane.ERROR_MESSAGE);
					System.exit(1);
				}
			}
			final Process patcherProcess = startProcess(patcherJarPath.getAbsolutePath(), new String[] { "--updater.launcherPort=" + communicationService.getCommunicationPort() });
			patcherProcess.waitFor();
			if(patcherProcess.exitValue() != 0) {
				JOptionPane.showMessageDialog(null, "The Updater was closed by an unexpected error.", "Error", JOptionPane.ERROR_MESSAGE);
				System.exit(1);
			}
		} else {
			System.exit(1);
		}
	}

	private Process startProcess(String jarPath, String[] args) throws IOException {
		final List<String> params = new ArrayList<>();
		params.add("java");
		params.add("-jar");
		params.add(jarPath);
		params.addAll(Arrays.asList(args));
		final ProcessBuilder pb = new ProcessBuilder(params);
		final Optional<String> patcherUserDir = context.getString("patcherUserDir");
		if(patcherUserDir.isPresent()) {
			final File outputFile = Paths.get(patcherUserDir.get(), "output.log").normalize().toFile();
			final File errorFile = Paths.get(patcherUserDir.get(), "error.log").normalize().toFile();
			pb.redirectOutput(outputFile);
			pb.redirectError(errorFile);
		} else {
			pb.redirectOutput(new File("output.log"));
			pb.redirectError(new File("error.log"));
		}
		return pb.start();
	}

	public static void main(String[] args) {
		final SpringApplicationBuilder builder = new SpringApplicationBuilder(LauncherApplication.class);
		builder.headless(false).run(args);
	}
}