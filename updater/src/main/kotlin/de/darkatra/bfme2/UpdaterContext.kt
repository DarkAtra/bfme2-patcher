package de.darkatra.bfme2

import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.patch.PatchConstants
import de.darkatra.bfme2.registry.RegistryService
import java.nio.file.Path
import java.nio.file.Paths
import kotlin.io.path.isRegularFile
import kotlin.io.path.toPath

object UpdaterContext {

    const val APPLICATION_NAME: String = "BfME Mod Launcher"
    const val ROTWK_EXE_NAME: String = "lotrbfme2ep1.exe"

    val applicationVersion: String = UpdaterContext::class.java.getPackage().implementationVersion ?: "dev"
    val applicationHome: Path = javaClass.protectionDomain.codeSource.location.toURI().toPath()

    val context: Context = when (isRunningAsJar()) {
        true -> getProductionContext()
        false -> getTestContext()
    }

    fun isRunningAsJar(): Boolean {
        return applicationHome.isRegularFile()
    }

    private fun getTestContext(): Context {
        return Context().apply {
            putIfAbsent(Context.SERVER_URL_IDENTIFIER, PatchConstants.SERVER_URL)
            putIfAbsent(Context.BFME2_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/updater/bfme2/").normalize().toString())
            putIfAbsent(Context.BFME2_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/updater/userDirBfme2/").normalize().toString())
            putIfAbsent(Context.ROTWK_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/updater/bfme2ep1/").normalize().toString())
            putIfAbsent(
                Context.ROTWK_USER_DIR_IDENTIFIER,
                Paths.get(System.getProperty("user.home"), "Desktop/updater/userDirBfme2Ep1/").normalize().toString()
            )
            putIfAbsent(Context.PATCHER_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/updater/.patcher").normalize().toString())
        }
    }

    private fun getProductionContext(): Context {
        return Context().apply {
            putIfAbsent(Context.SERVER_URL_IDENTIFIER, PatchConstants.SERVER_URL)
            putIfAbsent(Context.BFME2_HOME_DIR_IDENTIFIER, RegistryService.findBaseGameHomeDirectory().normalize().toString())
            putIfAbsent(Context.BFME2_USER_DIR_IDENTIFIER, RegistryService.findBaseGameUserDirectory().normalize().toString())
            RegistryService.findExpansionHomeDirectory().normalize().let { expansionHomeDir ->
                putIfAbsent(Context.ROTWK_HOME_DIR_IDENTIFIER, expansionHomeDir.toString())
                putIfAbsent(Context.PATCHER_USER_DIR_IDENTIFIER, expansionHomeDir.resolve(".patcher").normalize().toString())
            }
            putIfAbsent(Context.ROTWK_USER_DIR_IDENTIFIER, RegistryService.findExpansionUserDirectory().normalize().toString())
        }
    }
}
