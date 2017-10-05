package de.darkatra.patcher.service;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringRunner;

import java.time.LocalDateTime;
import java.util.Optional;

@RunWith(SpringRunner.class)
@SpringBootTest
public class PatchServiceTest {
	private PatchService patchService;

	@Autowired
	public void setPatchService(PatchService patchService) {
		this.patchService = patchService;
	}

	@Test
	public void testValidPatchOf() throws Exception {
		final String json = "{\"packets\":[],\"version\":{\"majorVersion\":0,\"minorVersion\":0,\"buildVersion\":0}}";
		final Version v0 = new Version(0, 0, 0);

		final Optional<Patch> result = patchService.patchOf(json);

		assertTrue(result.isPresent());
		assertTrue(result.get().getPackets().isEmpty());
		assertEquals(result.get().getVersion(), v0);
	}

	@Test
	public void testInvalidPatchOf() throws Exception {
		final String json = "";

		final Optional<Patch> result = patchService.patchOf(json);

		assertFalse(result.isPresent());
	}

	@Test
	public void testValidApplyContextToPatch() throws Exception {
		final String key = "abc";
		final String value = "123";
		final Context context = new Context();
		context.put(key, value);
		final Patch patch = new Patch(new Version(0, 0, 0));
		patch.getPackets().add(new Packet("${" + key + "}", "${" + key + "}", 0L, LocalDateTime.now(), "hash", false));

		final Patch contextualPatch = patchService.applyContextToPatch(context, patch);

		assertEquals(patch.getVersion(), contextualPatch.getVersion());
		assertFalse(contextualPatch.getPackets().isEmpty());
		assertEquals(value, contextualPatch.getPackets().get(0).getSrc());
		assertEquals(value, contextualPatch.getPackets().get(0).getDest());
		assertEquals(patch.getPackets().get(0).getChecksum(), contextualPatch.getPackets().get(0).getChecksum());
		assertEquals(patch.getPackets().get(0).getPacketSize(), contextualPatch.getPackets().get(0).getPacketSize());
		assertEquals(patch.getPackets().get(0).getDateTime(), contextualPatch.getPackets().get(0).getDateTime());
	}
}