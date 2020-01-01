package de.darkatra.patcher.service;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import de.darkatra.patcher.TestApplication;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringJUnit4ClassRunner;

import java.time.LocalDateTime;
import java.util.Optional;

@RunWith(SpringJUnit4ClassRunner.class)
@SpringBootTest(classes = TestApplication.class)
public class PatchServiceTest {
	private PatchService patchService;

	@Autowired
	public void setPatchService(PatchService patchService) {
		this.patchService = patchService;
	}

	@Test
	public void testValidPatchOf() {
		final String json = "{\"fileIndex\":[\"File\"],\"packets\":[],\"version\":{\"majorVersion\":0,\"minorVersion\":0,\"buildVersion\":0}}";
		final Version v0 = new Version(0, 0, 0);

		final Optional<Patch> result = patchService.patchOf(json);

		assertTrue(result.isPresent());
		assertEquals(result.get().getVersion(), v0);
		assertFalse(result.get().getFileIndex().isEmpty());
		assertTrue(result.get().getPackets().isEmpty());
	}

	@Test
	public void testValidPatchOfFileIndex() {
		final String json = "{\"fileIndex\":[],\"packets\":[{\"dest\":\"Test\"}],\"version\":{\"majorVersion\":0,\"minorVersion\":0,\"buildVersion\":0}}";
		final Version v0 = new Version(0, 0, 0);

		final Optional<Patch> result = patchService.patchOf(json);

		assertTrue(result.isPresent());
		assertEquals(result.get().getVersion(), v0);
		assertFalse(result.get().getFileIndex().isEmpty());
		assertFalse(result.get().getPackets().isEmpty());
	}

	@Test
	public void testInvalidPatchOf() {
		final String json = "";

		final Optional<Patch> result = patchService.patchOf(json);

		assertFalse(result.isPresent());
	}

	@Test
	public void testValidApplyContextToPatch() {
		final String key = "abc";
		final String value = "123";
		final Context context = new Context();
		context.put(key, value);
		final Patch patch = Patch.builder().version(new Version(0, 0, 0)).build();
		patch.getPackets().add(new Packet("${" + key + "}", "${" + key + "}", 0L, LocalDateTime.now(), "hash", false));

		final Patch contextualPatch = patchService.applyContextToPatch(context, patch);

		assertEquals(patch.getVersion(), contextualPatch.getVersion());
		assertFalse(contextualPatch.getPackets().isEmpty());
		assertEquals(value, contextualPatch.getPackets().iterator().next().getSrc());
		assertEquals(value, contextualPatch.getPackets().iterator().next().getDest());
		assertEquals(patch.getPackets().iterator().next().getChecksum(), contextualPatch.getPackets().iterator().next().getChecksum());
		assertEquals(patch.getPackets().iterator().next().getPacketSize(), contextualPatch.getPackets().iterator().next().getPacketSize());
		assertEquals(patch.getPackets().iterator().next().getDateTime(), contextualPatch.getPackets().iterator().next().getDateTime());
	}

	//	@Test
	//	public void testValidGenerateContextForPatch() throws Exception {
	//		suppress(constructor(File.class, String.class));
	//		final File fileMock = mock(File.class);
	//		whenNew(File.class).withAnyArguments().thenReturn(fileMock);
	//		when(fileMock.toPath()).thenReturn(Paths.get("C:\\Users\\Test\\workspace\\Patcher\\base\\"));
	//		final String key = "abc";
	//		final String value = "123";
	//		final String serverUrlKey = "serverUrl";
	//		final String serverUrlValue = "C:\\Users\\Test\\workspace\\Patcher\\base\\server";
	//		final Context context = new Context();
	//		context.put(key, value);
	//		context.put(serverUrlKey, serverUrlValue);
	//		final Patch patch = new Patch(new Version(0, 0, 0));
	//		patch.getPackets().add(new Packet(serverUrlValue, value, 0L, LocalDateTime.now(), "hash", false));
	//
	//		final Patch contextualPatch = patchService.generateContextForPatch(context, patch);
	//
	//		assertEquals(patch.getVersion(), contextualPatch.getVersion());
	//		assertFalse(contextualPatch.getPackets().isEmpty());
	//		assertEquals("${" + serverUrlKey + "}/server", contextualPatch.getPackets().iterator().next().getSrc());
	//		assertEquals("${" + key + "}", contextualPatch.getPackets().iterator().next().getDest());
	//		assertEquals(patch.getPackets().iterator().next().getChecksum(), contextualPatch.getPackets().iterator().next().getChecksum());
	//		assertEquals(patch.getPackets().iterator().next().getPacketSize(), contextualPatch.getPackets().iterator().next().getPacketSize());
	//		assertEquals(patch.getPackets().iterator().next().getDateTime(), contextualPatch.getPackets().iterator().next().getDateTime());
	//	}
}
