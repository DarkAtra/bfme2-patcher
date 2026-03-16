package de.darkatra.bfme2.patch

import io.goodforgod.graalvm.hint.annotation.ReflectionHint
import java.nio.file.Path

@ReflectionHint(ReflectionHint.AccessType.ALL_DECLARED_CONSTRUCTORS)
data class ObsoleteFile(
    var dest: String
) : ContextAware {

    /**
     * Applies the given [Context] to the [ObsoleteFile], replacing placeholders with their actual value.
     * Ideally this operation should only be performed once per [ObsoleteFile].
     *
     * @param context the [Context] containing values for placeholders
     */
    override fun applyContext(context: Context) {

        val origDest: String = dest

        context.forEach { (key, value) ->
            dest = Path.of(dest.replace("${Context.PREFIX}$key${Context.SUFFIX}", value)).normalize().toString()
        }

        if (dest == origDest) {
            error("Invalid ObsoleteFile without placeholder was found. (Dest: '$dest')")
        }
    }
}
