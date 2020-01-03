package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.util.HashSet;
import java.util.Set;

@Data
@Accessors(chain = true)
public class Patch {
	private Set<String> fileIndex = new HashSet<>();
	private Set<Packet> packets = new HashSet<>();
	private Version version;
}
