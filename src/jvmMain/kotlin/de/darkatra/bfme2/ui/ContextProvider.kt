package de.darkatra.bfme2.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import de.darkatra.bfme2.game.OptionFileService
import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.registry.RegistryService
import de.darkatra.bfme2.selfupdate.SelfUpdateService

private val applicationName = staticCompositionLocalOf<String> { error("applicationName not defined") }
private val applicationVersion = staticCompositionLocalOf<String> { error("applicationVersion not defined") }
private val context = staticCompositionLocalOf<Context> { error("context not defined") }
private val registryService = staticCompositionLocalOf<RegistryService> { error("registryService not defined") }
private val downloadService = staticCompositionLocalOf<DownloadService> { error("downloadService not defined") }
private val hashingService = staticCompositionLocalOf<HashingService> { error("hashingService not defined") }
private val optionFileService = staticCompositionLocalOf<OptionFileService> { error("optionFileService not defined") }
private val patchService = staticCompositionLocalOf<PatchService> { error("patchService not defined") }
private val selfUpdateService = staticCompositionLocalOf<SelfUpdateService> { error("selfUpdateService not defined") }

object UpdaterContext {

    val applicationName
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.applicationName.current

    val applicationVersion
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.applicationVersion.current

    val context
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.context.current

    val registryService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.registryService.current

    val downloadService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.downloadService.current

    val hashingService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.hashingService.current

    val optionFileService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.optionFileService.current

    val patchService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.patchService.current

    val selfUpdateService
        @Composable
        @ReadOnlyComposable
        get() = de.darkatra.bfme2.ui.selfUpdateService.current
}

@Composable
fun UpdaterContextProvider(
    applicationName: String = "BfME Mod Launcher",
    applicationVersion: String = UpdaterContext::class.java.getPackage().implementationVersion ?: "dev",
    context: Context,
    registryService: RegistryService = RegistryService(),
    downloadService: DownloadService = DownloadService(),
    hashingService: HashingService = HashingService(),
    optionFileService: OptionFileService = OptionFileService(),
    patchService: PatchService = PatchService(context, downloadService, hashingService),
    selfUpdateService: SelfUpdateService = SelfUpdateService(context, downloadService, hashingService),
    content: @Composable () -> Unit
) {
    CompositionLocalProvider(
        de.darkatra.bfme2.ui.applicationName provides applicationName,
        de.darkatra.bfme2.ui.applicationVersion provides applicationVersion,
        de.darkatra.bfme2.ui.context provides context,
        de.darkatra.bfme2.ui.registryService provides registryService,
        de.darkatra.bfme2.ui.downloadService provides downloadService,
        de.darkatra.bfme2.ui.hashingService provides hashingService,
        de.darkatra.bfme2.ui.optionFileService provides optionFileService,
        de.darkatra.bfme2.ui.patchService provides patchService,
        de.darkatra.bfme2.ui.selfUpdateService provides selfUpdateService,
        content = content
    )
}
