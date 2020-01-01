package de.darkatra.patcher.service;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import de.darkatra.patcher.TestApplication;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringJUnit4ClassRunner;

import java.io.File;
import java.util.Optional;

@RunWith(SpringJUnit4ClassRunner.class)
@SpringBootTest(classes = TestApplication.class)
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

	//	@Test
	//	public void testIOExceptionGetSHA3Checksum() throws Exception {
	//		suppress(constructor(FileInputStream.class, File.class));
	//		final FileInputStream fileInputStreamMock = mock(FileInputStream.class);
	//		whenNew(FileInputStream.class).withAnyArguments().thenReturn(fileInputStreamMock);
	//		when(fileInputStreamMock.read(any(byte[].class))).thenThrow(new IOException("IOException reading the file."));
	//		final File file = mock(File.class);
	//		when(file.isFile()).thenReturn(true);
	//
	//		final ThrowableAssert.ThrowingCallable operation = () -> hashingService.getSHA3Checksum(file);
	//
	//		assertThatThrownBy(operation).isInstanceOf(IOException.class);
	//	}
	//
	//	@Test
	//	public void testInterrupt() throws Exception {
	//		final Thread threadMock = mock(Thread.class);
	//		mockStatic(Thread.class);
	//		when(Thread.currentThread()).thenReturn(threadMock);
	//		when(threadMock.isInterrupted()).thenReturn(true);
	//		final File file = new File(HashingServiceTest.class.getResource("/hashTest.txt").toURI());
	//
	//		final ThrowableAssert.ThrowingCallable operation = () -> hashingService.getSHA3Checksum(file);
	//
	//		assertThatThrownBy(operation).isInstanceOf(InterruptedException.class);
	//	}
}
