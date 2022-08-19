package de.darkatra.bfme2.patch

class Context : HashMap<String, String>() {
    companion object {
        const val PREFIX = "\${"
        const val SUFFIX = "}"
    }
}
