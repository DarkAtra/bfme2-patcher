package de.darkatra.patcher.updater.service;

import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.powermock.api.mockito.PowerMockito.whenNew;
import static org.powermock.api.support.membermodification.MemberMatcher.constructor;
import static org.powermock.api.support.membermodification.MemberModifier.suppress;
import org.assertj.core.api.ThrowableAssert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;
import org.powermock.modules.junit4.PowerMockRunnerDelegate;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringRunner;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Optional;

@RunWith(PowerMockRunner.class)
@SpringBootTest
@PowerMockRunnerDelegate(SpringRunner.class)
@PrepareForTest({ HashingService.class })
@PowerMockIgnore("javax.management.*")
public class HashingServiceTest {
	private HashingService hashingService;

	@Autowired
	public void setHashingService(HashingService hashingService) {
		this.hashingService = hashingService;
	}

	@Test
	public void testValidGetSHA3Checksum() throws Exception {
		final File file = new File(HashingServiceTest.class.getResource("/hashTest.txt").toURI());
		final String hash = "7yNeiW9BCjmMGlEjV8GLsb0Uf06LUXTvHwsM0MhTcmI=";

		final Optional<String> checksum = hashingService.getSHA3Checksum(file);

		assertTrue(checksum.isPresent());
		assertEquals(hash, checksum.get());
	}

	@Test
	public void testInvalidGetSHA3Checksum() throws Exception {
		final File file = new File(".");

		final Optional<String> checksum = hashingService.getSHA3Checksum(file);

		assertFalse(checksum.isPresent());
	}

	@Test
	public void testIOExceptionGetSHA3Checksum() throws Exception {
		suppress(constructor(FileInputStream.class, File.class));
		final FileInputStream fileInputStreamMock = mock(FileInputStream.class);
		whenNew(FileInputStream.class).withAnyArguments().thenReturn(fileInputStreamMock);
		when(fileInputStreamMock.read(any(byte[].class))).thenThrow(new IOException("IOException reading the file."));
		final File file = mock(File.class);
		when(file.isFile()).thenReturn(true);

		final ThrowableAssert.ThrowingCallable operation = ()->hashingService.getSHA3Checksum(file);

		assertThatThrownBy(operation).isInstanceOf(IOException.class);
	}
}