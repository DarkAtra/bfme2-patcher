package de.darkatra.patcher.model;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.annotation.JsonPOJOBuilder;
import lombok.Builder;
import lombok.Data;

import java.util.HashSet;
import java.util.Set;

@Data
@Builder
@JsonDeserialize(builder = Patch.PatchBuilder.class)
public class Patch {
	@Builder.Default
	private final Set<String> fileIndex = new HashSet<>();
	@Builder.Default
	private final Set<Packet> packets = new HashSet<>();
	private final Version version;

	@JsonPOJOBuilder(withPrefix = "")
	public static final class PatchBuilder {
	}
}
