package de.darkatra.bfme2.patch

private val VERSION_PATTERN = Regex("^(\\d+)\\.(\\d+)\\.(\\d+)").toPattern()

data class SemanticVersion(
    val major: Int,
    val minor: Int,
    val patch: Int
) {

    companion object {

        fun getComparator(): Comparator<SemanticVersion> {
            return Comparator.comparing(SemanticVersion::major)
                .thenComparing(SemanticVersion::minor)
                .thenComparing(SemanticVersion::patch)
        }

        fun ofString(version: String): SemanticVersion {

            val matcher = VERSION_PATTERN.matcher(version)
            if (!matcher.find() || matcher.groupCount() != 3) {
                error("Could not parse version '$version' as SemanticVersion.")
            }

            val result = matcher.toMatchResult()
            return SemanticVersion(
                major = result.group(1).toInt(),
                minor = result.group(2).toInt(),
                patch = result.group(3).toInt()
            )
        }
    }

    override fun toString(): String {
        return "$major.$minor.$patch"
    }
}

operator fun SemanticVersion.compareTo(other: SemanticVersion): Int {
    return SemanticVersion.getComparator().compare(this, other)
}
