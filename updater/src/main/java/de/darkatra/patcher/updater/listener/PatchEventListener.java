package de.darkatra.patcher.updater.listener;

public interface PatchEventListener {
	void preDownloadServerPatchlist();

	void postDownloadServerPatchlist();

	void preReadServerPatchlist();

	void postReadServerPatchlist();

	void onPatcherNeedsUpdate(boolean requiresUpdate);

	void preCalculateDifferences();

	void postCalculateDifferences();

	void preDeleteFiles();

	void postDeleteFiles();

	void prePacketsDownload();

	void postPacketsDownload();

	void onPatchDone();

	void onPatchProgressChange(long current, long target);

	void onValidatingPacket();
}