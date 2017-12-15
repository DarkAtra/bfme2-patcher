package de.darkatra.patcher.service;

import com.google.gson.Gson;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import lombok.extern.slf4j.Slf4j;
import org.jetbrains.annotations.NotNull;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Optional;

@Slf4j
@Service
public class PatchService {
	private Gson gson;
	private String prefix, suffix;

	@Autowired
	public PatchService(Gson gson) {
		this.gson = gson;
		prefix = "${";
		suffix = "}";
	}

	public Optional<Patch> patchOf(@NotNull String json) {
		try {
			final Patch patch = gson.fromJson(json, Patch.class);
			patch.getPackets().stream().map(Packet::getDest).forEach(dest->patch.getFileIndex().add(dest));
			return Optional.of(patch);
		} catch(Exception e) {
			return Optional.empty();
		}
	}

	public Patch applyContextToPatch(@NotNull Context context, @NotNull Patch patch) {
		final Patch returnPatch = new Patch(new Version(patch.getVersion()));
		for(Packet packet : patch.getPackets()) {
			String src = packet.getSrc();
			String dest = packet.getDest();
			for(String key : context.keySet()) {
				final Optional<String> value = context.getString(key);
				if(value.isPresent()) {
					src = src.replace(prefix + key + suffix, value.get());
					dest = dest.replace(prefix + key + suffix, value.get());
				} else {
					log.error("Unexpected Error while applying the context {} for patch {}", context, patch);
				}
			}
			// normalize windows path
			dest = Paths.get(dest).normalize().toString();
			returnPatch.getPackets().add(new Packet(src, dest, packet.getPacketSize(), packet.getDateTime(), packet.getChecksum(), packet.isBackupExisting()));
		}
		for(String destToRemove : patch.getFileIndex()) {
			for(String key : context.keySet()) {
				final Optional<String> value = context.getString(key);
				if(value.isPresent()) {
					destToRemove = destToRemove.replace(prefix + key + suffix, value.get());
				} else {
					log.error("Unexpected Error while applying the context {} for patch {}", context, patch);
				}
			}
			// normalize windows path
			destToRemove = Paths.get(destToRemove).normalize().toString();
			returnPatch.getFileIndex().add(destToRemove);
		}
		return returnPatch;
	}

	public Patch generateContextForPatch(@NotNull Context context, @NotNull Patch patch) {
		final Patch returnPatch = new Patch(new Version(patch.getVersion()));
		final Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
		for(Packet packet : patch.getPackets()) {
			final Path relativePathToCWD = currentDir.relativize(Paths.get(packet.getSrc())).normalize();
			final String src = prefix + "serverUrl" + suffix + "/" + relativePathToCWD.toString();
			String dest = packet.getDest();
			for(String key : context.keySet()) {
				try {
					Optional<String> value = context.getString(key);
					if(value.isPresent()) {
						String temp = dest.replace(value.get(), prefix + key + suffix);
						if(!temp.equalsIgnoreCase(dest)) {
							dest = temp;
							break;
						}
					}
				} catch(ClassCastException e) {
					log.error("Unexpected Error while generating the context {} for patch {}", context, patch);
					log.debug("ClassCastException", e);
				}
			}
			returnPatch.getPackets().add(new Packet(src, dest, packet.getPacketSize(), packet.getDateTime(), packet.getChecksum(), packet.isBackupExisting()));
		}
		return returnPatch;
	}
}