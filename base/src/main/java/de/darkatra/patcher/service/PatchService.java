package de.darkatra.patcher.service;

import com.google.gson.Gson;
import de.darkatra.patcher.model.Context;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import javafx.scene.Parent;
import lombok.extern.slf4j.Slf4j;
import org.jetbrains.annotations.NotNull;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.util.List;
import java.util.Optional;

@Slf4j
@Service
public class PatchService {
	private Gson gson;
	private String prefix, suffix;
	private HashingService hashingService;

	@Autowired
	public PatchService(Gson gson, HashingService hashingService) {
		this.gson = gson;
		prefix = "${";
		suffix = "}";
		this.hashingService = hashingService;
	}

	public Optional<Patch> patchOf(@NotNull String json) {
		try {
			final Patch patch = gson.fromJson(json, Patch.class);
			patch.getPackets().stream().map(Packet::getDest).forEach(dest -> patch.getFileIndex().add(dest));
			return Optional.of(patch);
		} catch (Exception e) {
			return Optional.empty();
		}
	}

	public Patch applyContextToPatch(@NotNull Context context, @NotNull Patch patch) throws URISyntaxException {
		Patch returnPatch = new Patch(new Version(patch.getVersion()));
		for (Packet packet : patch.getPackets()) {
			String src = packet.getSrc();
			String dest = packet.getDest();
			for (String key : context.keySet()) {
				final Optional<String> value = context.getString(key);
				if (value.isPresent()) {
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
		for (String destToRemove : patch.getFileIndex()) {
			for (String key : context.keySet()) {
				final Optional<String> value = context.getString(key);
				if (value.isPresent()) {
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


	public Patch applyPathToContext(@NotNull Context context, @NotNull List<File> items, Version version) throws IOException, InterruptedException{
		Patch returnPatch = new Patch(version);
		Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
		System.out.println(currentDir.toString());
		for (File file :
				items) {
			String filePath = file.toPath().toString();
			try{
				final Optional<String> checkSum = this.hashingService.getSHA3Checksum(file);
				if (checkSum.isPresent()) {
					final Path relativePathToCWD = currentDir.relativize(file.toPath()).normalize();
					System.out.println(relativePathToCWD.toString());
					for (String key :
							context.keySet()) {
						Optional<String> value = context.getString(key);
						if (value.isPresent()){
							String src = relativePathToCWD.toString().replace(value.get(),"");
							System.out.println(src);
								returnPatch.getPackets().add(new Packet(prefix+"server"+suffix+src.toString(),prefix+key+relativePathToCWD+suffix, file.length(), LocalDateTime.now(),checkSum.get(),false));

						}

					}
				}
			}catch (IOException|InterruptedException e){
				if (e instanceof IOException){
					throw e;
				}
			}
//			returnPatch.getPackets().add(new Packet(src, dst, packet.getPacketSize(), packet.getDateTime(), packet.getChecksum(), packet.isBackupExisting()));
		}

		return returnPatch;
	}


	//generatecontextfrom.. reale pfade durch filler ersetzen
}