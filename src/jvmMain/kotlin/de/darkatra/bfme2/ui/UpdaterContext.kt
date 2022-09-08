package de.darkatra.bfme2.ui

import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.registry.RegistryService
import java.nio.file.Path
import java.nio.file.Paths
import kotlin.io.path.isRegularFile

object UpdaterContext {

    const val applicationName: String = "BfME Mod Launcher"
    val applicationVersion: String = UpdaterContext::class.java.getPackage().implementationVersion ?: "dev"
    val applicationHome: Path = Path.of(javaClass.protectionDomain.codeSource.location.path)

    val context: Context

    init {
        context = when (applicationHome.isRegularFile()) {
            true -> getProductionContext()
            false -> getTestContext()
        }
    }

    private fun getTestContext(): Context {
        return Context().apply {
            putIfAbsent(Context.SERVER_URL_IDENTIFIER, "https://darkatra.de")
            putIfAbsent(Context.BFME2_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2/").normalize().toString())
            putIfAbsent(Context.BFME2_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2/").normalize().toString())
            putIfAbsent(Context.ROTWK_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2ep1/").normalize().toString())
            putIfAbsent(Context.ROTWK_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2Ep1/").normalize().toString())
            putIfAbsent(Context.PATCHER_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/.patcher").normalize().toString())
        }
    }

    private fun getProductionContext(): Context {
        return Context().apply {
            putIfAbsent(Context.SERVER_URL_IDENTIFIER, "https://darkatra.de")
            putIfAbsent(Context.BFME2_HOME_DIR_IDENTIFIER, RegistryService.findBaseGameHomeDirectory().normalize().toString())
            putIfAbsent(Context.BFME2_USER_DIR_IDENTIFIER, RegistryService.findBaseGameUserDirectory().normalize().toString())
            putIfAbsent(Context.ROTWK_HOME_DIR_IDENTIFIER, RegistryService.findExpansionHomeDirectory().normalize().toString())
            RegistryService.findExpansionUserDirectory().normalize().let { expansionUserDir ->
                putIfAbsent(Context.ROTWK_USER_DIR_IDENTIFIER, expansionUserDir.toString())
                putIfAbsent(Context.PATCHER_USER_DIR_IDENTIFIER, expansionUserDir.resolve(".patcher").normalize().toString())
            }
        }
    }
}
