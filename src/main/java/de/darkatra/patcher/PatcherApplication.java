package de.darkatra.patcher;

import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

@Slf4j
@SpringBootApplication
public class PatcherApplication {
	@Autowired
	public void onInit(PatchController patchController) {
		patchController.initializePatch();
	}

	public static void main(String[] args) {
		SpringApplication.run(PatcherApplication.class, args);
	}
}