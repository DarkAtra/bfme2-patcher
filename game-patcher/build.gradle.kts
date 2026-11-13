plugins {
    id("cpp-library")
}

library {
    targetMachines = listOf(
        machines.windows.x86
    )

    tasks {
        withType<LinkSharedLibrary> {
            libs.from(
                "${projectDir}/libs/EasyHook32.lib",
                "${projectDir}/libs/user32.lib",
                "${projectDir}/libs/WS2_32.Lib"
            )
            if (name == "linkRelease") {
                // /DEBUG links incrementally by default: the shipped dll would carry padding that grows with every relink
                linkerArgs.add("/INCREMENTAL:NO")
            }
        }
    }
}

tasks.named("build") {
    dependsOn("assembleRelease")
}
