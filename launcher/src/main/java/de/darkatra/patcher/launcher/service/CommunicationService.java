package de.darkatra.patcher.launcher.service;

import com.google.gson.Gson;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.LinkedHashSet;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

@Service
@Slf4j
public class CommunicationService {
	private final Gson gson;
	private final Set<Consumer<String>> listeners;
	private final ServerSocket socket;
	private BufferedReader reader;
	private BufferedWriter writer;

	public CommunicationService(Gson gson) throws IOException {
		this.gson = gson;
		listeners = new LinkedHashSet<>();
		socket = new ServerSocket(0);
		Executor executor = Executors.newSingleThreadExecutor();
		executor.execute(this::readSocket);
	}

	public boolean addListener(Consumer<String> listener) {
		synchronized(listeners) {
			return listeners.add(listener);
		}
	}

	public void readSocket() {
		try {
			final Socket patcher = socket.accept();
			reader = new BufferedReader(new InputStreamReader(patcher.getInputStream()));
			writer = new BufferedWriter(new OutputStreamWriter(patcher.getOutputStream()));
			while(!socket.isClosed()) {
				final Optional<String> message = readMessage();
				message.ifPresent(json->{
					synchronized(listeners) {
						listeners.forEach(listener->listener.accept(json));
					}
				});
			}
		} catch(IOException e) {
			log.error("Exception communicating with the updater: ", e);
		}
	}

	public int getCommunicationPort() {
		return socket.getLocalPort();
	}

	public void sendMessage(Object object) throws IOException {
		final String json = gson.toJson(object);
		writer.write(json, 0, json.length());
	}

	private Optional<String> readMessage() throws IOException {
		final String in = IOUtils.toString(reader);
		if(in.trim().isEmpty()) {
			return Optional.empty();
		} else {
			return Optional.of(in);
		}
	}
}
