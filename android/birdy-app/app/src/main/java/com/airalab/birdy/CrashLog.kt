package com.airalab.birdy

import android.content.Context
import android.util.Log
import java.io.File

/** Последний краш сохраняем в кэш — чтобы показать в приложении и переслать разработчику. */
object CrashLog {
    private const val NAME = "last_crash.txt"

    fun install(ctx: Context) {
        val file = File(ctx.cacheDir, NAME)
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, e ->
            try {
                file.writeText(Log.getStackTraceString(e))
            } catch (_: Exception) {
            }
            prev?.uncaughtException(thread, e)
        }
    }

    fun read(ctx: Context): String? {
        val f = File(ctx.cacheDir, NAME)
        if (!f.exists()) return null
        return try {
            f.readText().lines().take(6).joinToString("\n")
        } catch (e: Exception) {
            null
        }
    }

    fun clear(ctx: Context) {
        try {
            File(ctx.cacheDir, NAME).delete()
        } catch (_: Exception) {
        }
    }
}
