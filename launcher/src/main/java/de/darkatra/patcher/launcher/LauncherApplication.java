package de.darkatra.patcher.launcher;

import com.google.gson.Gson;
import de.darkatra.patcher.model.communication.RequiresUpdateDto;
import de.darkatra.patcher.properties.Config;
import de.darkatra.patcher.service.CommunicationService;
import de.darkatra.patcher.service.DownloadService;
import lombok.extern.slf4j.Slf4j;
import org.bouncycastle.util.test.Test;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.scheduling.annotation.EnableAsync;

import javax.swing.JOptionPane;
import java.io.File;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;

@Slf4j
@EnableAsync
@SpringBootApplication
public class LauncherApplication implements ApplicationRunner {
	private final DownloadService downloadService;
	private final CommunicationService communicationService;
	private final Gson gson;
	private final Config config;
	private boolean needsRestart = false;

	public LauncherApplication(Config config, DownloadService downloadService, CommunicationService communicationService, Gson gson) {
		this.config = config;
		this.downloadService = downloadService;
		this.communicationService = communicationService;
		this.gson = gson;
		log.debug("LauncherPort: {}", communicationService.getCommunicationPort());
	}

	@Override
	public void run(ApplicationArguments args) throws Exception {
		final File patcherJarPath = Paths.get(config.getPatcherUserDir(), config.getUpdaterJarName()).normalize().toFile();
		final String patcherSrc = config.getPatchFileFolder() + "/" + config.getUpdaterJarName();
		communicationService.addListener(message->{
			final RequiresUpdateDto requiresUpdateDto = gson.fromJson(message, RequiresUpdateDto.class);
			if(requiresUpdateDto.isRequiresUpdate()) {
				needsRestart = true;
			} else {
				System.exit(0);
			}
		});
		final boolean isInitialDownload = !patcherJarPath.exists();
		if(isInitialDownload) {
			downloadUpdater(patcherSrc, patcherJarPath.getAbsolutePath());
		}
		launchUpdater(patcherJarPath.getAbsolutePath());
		while(needsRestart) {
			needsRestart = false;
			downloadUpdater(patcherSrc, patcherJarPath.getAbsolutePath());
			try {
				launchUpdater(patcherJarPath.getAbsolutePath());
			} catch(InterruptedException | IOException e) {
				log.debug("Exception launching the updater.", e);
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
			log.debug("Exception downloading the updater.", e);
			System.exit(1);
		}
	}

	private void launchUpdater(String updaterPath) throws InterruptedException, IOException {
		final Process patcherProcess = startProcess(updaterPath, new String[] {
				"--updater.launcherPort=" + communicationService.getCommunicationPort(),
				"--updater.launcherLocation=" + getJarLocation()
		});
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
		final File outputFile = Paths.get(config.getPatcherUserDir(), "output.log").normalize().toFile();
		final File errorFile = Paths.get(config.getPatcherUserDir(), "error.log").normalize().toFile();
		pb.redirectOutput(outputFile);
		pb.redirectError(errorFile);
		return pb.start();
	}

	private String getJarLocation() throws UnsupportedEncodingException {
		String path = Test.class.getProtectionDomain().getCodeSource().getLocation().getPath();
		return new String(Base64.getEncoder().encode(URLDecoder.decode(path, String.valueOf(StandardCharsets.UTF_8)).getBytes()), StandardCharsets.UTF_8);
	}

	public static void main(String[] args) {
		final SpringApplicationBuilder builder = new SpringApplicationBuilder(LauncherApplication.class);
		builder.headless(false).run(args);
	}
}