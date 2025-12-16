plugins {
    `cpp-library`
}

library {
    targetMachines = listOf(
        machines.windows.x86
    )

    tasks {
        withType<LinkSharedLibrary> {
            libs.from("${projectDir}/libs/EasyHook32.lib", "${projectDir}/libs/user32.lib", "${projectDir}/libs/WS2_32.Lib")
        }
    }
}
