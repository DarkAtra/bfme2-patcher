package de.darkatra.patcher.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.extern.slf4j.Slf4j;

import javax.annotation.PreDestroy;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketAddress;
import java.util.LinkedHashSet;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

@Slf4j
public class CommunicationService {
	private final ObjectMapper objectMapper;
	private final Set<Consumer<String>> listeners;
	private final ServerSocket server;
	private Socket socket;
	private DataInputStream reader;
	private DataOutputStream writer;

	public CommunicationService(final ObjectMapper objectMapper, final SocketAddress endpoint) throws IOException {
		this.objectMapper = objectMapper;
		listeners = new LinkedHashSet<>();
		server = null;
		socket = new Socket();
		socket.connect(endpoint);
		reader = new DataInputStream(socket.getInputStream());
		writer = new DataOutputStream(socket.getOutputStream());
		final Executor executor = Executors.newSingleThreadExecutor();
		executor.execute(this::readSocket);
	}

	public CommunicationService(final ObjectMapper objectMapper) throws IOException {
		this.objectMapper = objectMapper;
		listeners = new LinkedHashSet<>();
		server = new ServerSocket(0);
		final Executor executor = Executors.newSingleThreadExecutor();
		executor.execute(this::accept);
	}

	@PreDestroy
	public void preDestory() {
		close();
		try {
			server.close();
		} catch (final IOException ignored) {
		}
	}

	public boolean addListener(final Consumer<String> onRead) {
		synchronized (listeners) {
			return listeners.add(onRead);
		}
	}

	public int getCommunicationPort() {
		return server != null ? server.getLocalPort() : -1;
	}

	public void sendMessage(final Object object) throws IOException {
		writer.writeUTF(objectMapper.writeValueAsString(object));
	}

	private void accept() {
		while (server != null && !server.isClosed()) {
			try {
				socket = server.accept();
				reader = new DataInputStream(socket.getInputStream());
				writer = new DataOutputStream(socket.getOutputStream());
				// only accept 1 connection at a time
				readSocket();
			} catch (final IOException e) {
				log.debug("Exception accepting a new connection.", e);
			}
		}
	}

	private void readSocket() {
		try {
			while (socket != null && !socket.isClosed()) {
				readMessage().ifPresent(json -> {
					synchronized (listeners) {
						listeners.forEach(listener -> listener.accept(json));
					}
				});
			}
		} catch (final IOException e) {
			log.debug("Exception communicating with the launcher. Possible the socket just got closed (which is okay).", e);
		} finally {
			close();
		}
	}

	private Optional<String> readMessage() throws IOException {
		final String in = reader.readUTF();
		if (in.trim().isEmpty()) {
			return Optional.empty();
		} else {
			return Optional.of(in);
		}
	}

	private void close() {
		try {
			reader.close();
		} catch (IOException ignored) {
		}
		try {
			writer.close();
		} catch (IOException ignored) {
		}
	}
}
