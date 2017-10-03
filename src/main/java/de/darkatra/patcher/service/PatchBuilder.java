package de.darkatra.patcher.service;

import com.google.gson.Gson;
import de.darkatra.patcher.model.Patch;
import org.jetbrains.annotations.NotNull;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.util.Optional;

@Service
public class PatchBuilder {
	private Gson gson;

	@Autowired
	public void setGson(Gson gson) {
		this.gson = gson;
	}

	public Optional<Patch> patchOf(@NotNull String json) {
		try {
			return Optional.of(gson.fromJson(json, Patch.class));
		} catch(Exception e) {
			return Optional.empty();
		}
	}
}