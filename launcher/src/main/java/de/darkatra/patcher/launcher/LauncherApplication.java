package de.darkatra.patcher.launcher;

import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

@Slf4j
@SpringBootApplication
public class LauncherApplication implements CommandLineRunner {
	@Override
	public void run(String... args) throws Exception {
		// TODO: launch the updater
	}

	public static void main(String[] args) {
		SpringApplication.run(LauncherApplication.class, args);
	}
}