package de.darkatra.patcher.updater.listener;

public interface PatchEventListener {
	void onServerPatchlistDownloaded();

	void onServerPatchlistRead();

	void onPatcherNeedsUpdate(boolean requiresUpdate);

	void onDifferencesCalculated();

	void onFilesDeleted();

	void onPacketsDownloaded();

	void onPatchDone();

	void onPatchProgressChanged(long current, long target);

	void onValidatingPacket();
}