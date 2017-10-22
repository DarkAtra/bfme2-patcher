package de.darkatra.patcher.launcher;

import de.darkatra.patcher.BaseConfiguration;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.service.DownloadService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Import;

import javax.swing.JOptionPane;
import java.io.File;
import java.nio.file.Paths;
import java.util.Optional;

@Slf4j
@SpringBootApplication
@Import(BaseConfiguration.class)
public class LauncherApplication implements ApplicationRunner {
	private final Context context;
	private final DownloadService downloadService;

	public LauncherApplication(@Qualifier("launcherContext") Context context, DownloadService downloadService) {
		this.context = context;
		this.downloadService = downloadService;
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
			Runtime.getRuntime().exec("java -jar " + patcherJarPath.getAbsolutePath());
			// TODO: wait for updater message via CommunicationService
		} else {
			System.exit(1);
		}
	}

	public static void main(String[] args) {
		final SpringApplicationBuilder builder = new SpringApplicationBuilder(LauncherApplication.class);
		builder.headless(false).run(args);
	}
}