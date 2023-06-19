plugins {
    `cpp-library`
}

library {
    targetMachines.set(
        listOf(
            machines.windows.x86
        )
    )

    binaries.configureEach(CppSharedLibrary::class.java) {
        linkTask.get().libs.from("${projectDir}/libs/detours.lib", "${projectDir}/libs/user32.lib")
    }
}
