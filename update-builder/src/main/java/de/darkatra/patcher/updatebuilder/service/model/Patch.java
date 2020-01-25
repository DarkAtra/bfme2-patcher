package de.darkatra.patcher.updatebuilder.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.util.HashSet;
import java.util.Set;

@Data
@Accessors(chain = true)
public class Patch {
	private LatestUpdater latestUpdater;
	private Set<String> fileIndex = new HashSet<>();
	private Set<Packet> packets = new HashSet<>();
}
