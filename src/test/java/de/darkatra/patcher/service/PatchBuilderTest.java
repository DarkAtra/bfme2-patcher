package de.darkatra.patcher.service;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringRunner;

import java.util.Optional;

@RunWith(SpringRunner.class)
@SpringBootTest
public class PatchBuilderTest {
	private PatchBuilder patchBuilder;

	@Autowired
	public void setPatchBuilder(PatchBuilder patchBuilder) {
		this.patchBuilder = patchBuilder;
	}

	@Test
	public void testValidPatchOf() throws Exception {
		String json = "{\"packets\":[],\"version\":{\"majorVersion\":0,\"minorVersion\":0,\"buildVersion\":0}}";
		Version v0 = new Version(0, 0, 0);

		final Optional<Patch> result = patchBuilder.patchOf(json);

		assertTrue(result.isPresent());
		assertTrue(result.get().getPackets().isEmpty());
		assertEquals(result.get().getVersion(), v0);
	}

	@Test
	public void testInvalidPatchOf() throws Exception {
		String json = "";

		final Optional<Patch> result = patchBuilder.patchOf(json);

		assertFalse(result.isPresent());
	}
}