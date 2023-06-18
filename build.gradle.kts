allprojects {
    group = "de.darkatra.bfme2"
    version = "0.7.0"

    repositories {
        mavenLocal()
        google()
        mavenCentral()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
        maven("https://maven.pkg.github.com/DarkAtra/*") {
            credentials {
                username = "${extra["github.username"]}"
                password = "${extra["github.password"]}"
            }
        }
    }
}
