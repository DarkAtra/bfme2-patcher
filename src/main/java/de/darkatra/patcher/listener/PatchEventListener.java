package de.darkatra.patcher.listener;

public interface PatchEventListener {
	void onServerPatchlistDownloaded();

	void onServerPatchlistRead();

	void onPatcherNeedsUpdate();

	void onDifferencesCalculated();

	void onFilesDeleted();

	void onPacketsDownloaded();

	void onPatchDone();

	void onPatchProgressChanged(long current, long target);
}