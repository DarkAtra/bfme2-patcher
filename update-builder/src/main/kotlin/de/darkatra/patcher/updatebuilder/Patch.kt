package de.darkatra.patcher.updatebuilder

class Patch(obsoleteFiles: Set<ObsoleteFile>, packets: Set<Packet>) {
	var obsoleteFiles: Set<ObsoleteFile> = HashSet()
	var packets: Set<Packet> = HashSet()

	init {
		this.obsoleteFiles = obsoleteFiles
		this.packets = packets
	}
}
