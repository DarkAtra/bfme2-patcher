package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.util.HashSet;
import java.util.Set;

@Data
@Accessors(chain = true)
public class Patch implements ContextAware {
	private LatestUpdater latestUpdater;
	private Set<ObsoleteFile> obsoleteFiles = new HashSet<>();
	private Set<Packet> packets = new HashSet<>();

	@Override
	public void applyContext(Context context) {
		getLatestUpdater().applyContext(context);
		getPackets().forEach(packet -> packet.applyContext(context));
		getObsoleteFiles().forEach(obsoleteFile -> obsoleteFile.applyContext(context));
	}
}
