package de.darkatra.bfme2.util

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll

context(scope: CoroutineScope)
suspend fun <T> Collection<T>.forEachParallel(action: suspend (T) -> Unit) = map { scope.async { action(it) } }.awaitAll()
