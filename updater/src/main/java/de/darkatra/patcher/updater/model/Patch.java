package de.darkatra.patcher.updater.model;

import com.google.common.collect.Constraints;
import lombok.Data;

import java.util.HashSet;
import java.util.Set;

@Data
public class Patch {
	private final Set<String> fileIndex = Constraints.constrainedSet(new HashSet<>(), Constraints.notNull());
	private final Set<Packet> packets = Constraints.constrainedSet(new HashSet<>(), Constraints.notNull());
	private final Version version;
}