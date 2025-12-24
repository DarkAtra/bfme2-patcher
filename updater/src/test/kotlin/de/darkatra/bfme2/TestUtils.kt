package de.darkatra.bfme2

import kotlin.random.Random

private const val DICTIONARY = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

fun randomString(length: Int): String {
    return (0..<length)
        .map { DICTIONARY[Random.nextInt(0, DICTIONARY.length)] }
        .joinToString("")
}
