package de.darkatra.patcher;

import de.darkatra.patcher.exception.ValidationException;
import de.darkatra.patcher.listener.PatchEventListener;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

import java.io.IOException;
import java.net.URISyntaxException;

@Slf4j
@SpringBootApplication
public class PatcherApplication {
	@Autowired
	public void onInit(PatchController patchController) {
		try {
			patchController.patch(new PatchEventListener() {
				@Override
				public void onServerPatchlistDownloaded() {
					System.out.println("Downloaded Patchlist");
				}

				@Override
				public void onServerPatchlistRead() {
					System.out.println("Patchlist read");
				}

				@Override
				public void onPatcherNeedsUpdate() {
					System.out.println("Patcher needs update");
				}

				@Override
				public void onDifferencesCalculated() {
					System.out.println("Calculated diffs");
				}

				@Override
				public void onFilesDeleted() {
					System.out.println("Deleted Files");
				}

				@Override
				public void onPacketsDownloaded() {
					System.out.println("Packets downloaded");
				}

				@Override
				public void onPatchDone() {
					System.out.println("Patch done");
				}

				@Override
				public void onPatchProgressChanged(long current, long target) {
					System.out.println(current + "/" + target);
				}
			});
		} catch(IOException | ValidationException | URISyntaxException e) {
			e.printStackTrace();
		}
	}

	public static void main(String[] args) {
		SpringApplication.run(PatcherApplication.class, args);
	}
}