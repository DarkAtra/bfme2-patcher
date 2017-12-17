package de.darkatra.patcher.service;

import static org.junit.Assert.assertTrue;
import de.darkatra.patcher.model.Context;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.test.context.ContextConfiguration;
import org.springframework.test.context.junit4.SpringJUnit4ClassRunner;

@RunWith(SpringJUnit4ClassRunner.class)
@ContextConfiguration
public class OptionFileServiceTest {
	private OptionFileService optionFileService;

	@Autowired
	public void setOptionFileService(OptionFileService optionFileService) {
		this.optionFileService = optionFileService;
	}

	@Test
	public void testValidBuildDefaultOptionsIni() throws Exception {
		final Context defaultOptionsIni = optionFileService.buildDefaultOptionsIni();

		assertTrue(defaultOptionsIni.getBoolean("AllHealthBars").isPresent());
		assertTrue(defaultOptionsIni.getDouble("AmbientVolume").isPresent());
		assertTrue(defaultOptionsIni.getString("AudioLOD").isPresent());
		assertTrue(defaultOptionsIni.getDouble("Brightness").isPresent());
		assertTrue(defaultOptionsIni.getInteger("FlashTutorial").isPresent());
		assertTrue(defaultOptionsIni.getBoolean("HasSeenLogoMovies").isPresent());
		assertTrue(defaultOptionsIni.getString("IdealStaticGameLOD").isPresent());
		assertTrue(defaultOptionsIni.getDouble("MovieVolume").isPresent());
		assertTrue(defaultOptionsIni.getDouble("MusicVolume").isPresent());
		assertTrue(defaultOptionsIni.getString("Resolution").isPresent());
		assertTrue(defaultOptionsIni.getDouble("SFXVolume").isPresent());
		assertTrue(defaultOptionsIni.getInteger("ScrollFactor").isPresent());
		assertTrue(defaultOptionsIni.getBoolean("SendDelay").isPresent());
		assertTrue(defaultOptionsIni.getString("StaticGameLOD").isPresent());
		assertTrue(defaultOptionsIni.getInteger("TimesInGame").isPresent());
		assertTrue(defaultOptionsIni.getBoolean("UseEAX3").isPresent());
		assertTrue(defaultOptionsIni.getDouble("VoiceVolume").isPresent());
	}

	@Configuration
	static class TestConfiguration {
		@Bean
		public OptionFileService patchService() {
			return new OptionFileService();
		}
	}
}