package de.darkatra.patcher.updater.listener;

public interface PatchEventListener {
	void preDownloadPatchlist();

	void postDownloadPatchlist();

	void preReadPatchlist();

	void postReadPatchlist();

	void preDeleteFiles();

	void postDeleteFiles();

	void preCalculateDifferences();

	void postCalculateDifferences();

	void prePacketsDownload();

	void postPacketsDownload();

	void onPatchDone();

	void onPatchProgressChange(final long current, final long total);

	void onValidatingPacket();
}
