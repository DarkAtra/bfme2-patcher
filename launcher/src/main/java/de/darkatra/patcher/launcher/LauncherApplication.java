package de.darkatra.patcher.launcher;

import com.google.gson.Gson;
import de.darkatra.patcher.launcher.properties.LauncherConfig;
import de.darkatra.patcher.model.communication.RequiresUpdateDto;
import de.darkatra.patcher.service.CommunicationService;
import de.darkatra.patcher.service.DownloadService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.scheduling.annotation.EnableAsync;

import javax.swing.JOptionPane;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

@Slf4j
@EnableAsync
@SpringBootApplication
public class LauncherApplication implements ApplicationRunner {
	private final DownloadService downloadService;
	private final CommunicationService communicationService;
	private final Gson gson;
	private final LauncherConfig launcherConfig;
	private boolean needsRestart = false;

	public LauncherApplication(LauncherConfig launcherConfig, DownloadService downloadService, CommunicationService communicationService, Gson gson) {
		this.launcherConfig = launcherConfig;
		this.downloadService = downloadService;
		this.communicationService = communicationService;
		this.gson = gson;
	}

	@Override
	public void run(ApplicationArguments args) throws Exception {
		final File patcherJarPath = Paths.get(launcherConfig.getPatcherUserDir(), "Patcher.jar").normalize().toFile();
		final String patcherSrc = launcherConfig.getPatcherServerFilePath();
		communicationService.addListener(message->{
			final RequiresUpdateDto requiresUpdateDto = gson.fromJson(message, RequiresUpdateDto.class);
			log.debug(requiresUpdateDto.toString());
			if(!requiresUpdateDto.isRequiresUpdate()) {
				log.debug("Exit launcher");
			} else {
				log.debug("Download and restart patcher");
				needsRestart = true;
			}
		});
		final boolean isInitialDownload = !patcherJarPath.exists();
		if(isInitialDownload) {
			downloadUpdater(patcherSrc, patcherJarPath.getAbsolutePath());
		}
		log.debug("LauncherPort: {}", communicationService.getCommunicationPort());
		launchUpdater(patcherJarPath.getAbsolutePath());
		while(needsRestart) {
			needsRestart = false;
			downloadUpdater(patcherSrc, patcherJarPath.getAbsolutePath());
			try {
				launchUpdater(patcherJarPath.getAbsolutePath());
			} catch(InterruptedException | IOException e) {
				e.printStackTrace();
				System.exit(1);
			}
		}
		System.exit(0);
	}

	private void downloadUpdater(String src, String dest) {
		try {
			final boolean downloadSucceed = downloadService.downloadFile(src, dest);
			if(!downloadSucceed) {
				JOptionPane.showMessageDialog(null, "Could not download the updater. Try again with admin privileges.", "Error", JOptionPane.ERROR_MESSAGE);
				System.exit(1);
			}
		} catch(InterruptedException e) {
			e.printStackTrace();
			System.exit(1);
		}
	}

	private void launchUpdater(String updaterPath) throws InterruptedException, IOException {
		final Process patcherProcess = startProcess(updaterPath, new String[] { "--updater.launcherPort=" + communicationService.getCommunicationPort() });
		patcherProcess.waitFor();
		if(patcherProcess.exitValue() != 0) {
			JOptionPane.showMessageDialog(null, "The Updater was closed by an unexpected error.", "Error", JOptionPane.ERROR_MESSAGE);
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
		final File outputFile = Paths.get(launcherConfig.getPatcherUserDir(), "output.log").normalize().toFile();
		final File errorFile = Paths.get(launcherConfig.getPatcherUserDir(), "error.log").normalize().toFile();
		pb.redirectOutput(outputFile);
		pb.redirectError(errorFile);
		return pb.start();
	}

	public static void main(String[] args) {
		final SpringApplicationBuilder builder = new SpringApplicationBuilder(LauncherApplication.class);
		builder.headless(false).run(args);
	}
}