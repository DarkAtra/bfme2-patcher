package de.darkatra.patcher.listener;

public class PatchEventAdapter implements PatchEventListener {
	@Override
	public void onServerPatchlistDownloaded() {}

	@Override
	public void onServerPatchlistRead() {}

	@Override
	public void onPatcherNeedsUpdate() {}

	@Override
	public void onDifferencesCalculated() {}

	@Override
	public void onFilesDeleted() {}

	@Override
	public void onPacketsDownloaded() {}

	@Override
	public void onPatchDone() {}

	@Override
	public void onPatchProgressChanged(long current, long target) {}
}