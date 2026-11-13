package com.airalab.birdy

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * История измерений в памяти телефона, привязанная к MAC устройства.
 *
 * Плата хранит только «хвост» (2 ч по минутам, 20 суток по часам).
 * Приложение при каждом подключении докачивает новые записи и накапливает
 * их здесь — получается длинная история без нагрузки на BLE.
 */
object HistoryStore {
    private const val MAX_RECS = 20000

    private fun file(ctx: Context, mac: String) =
        File(ctx.filesDir, "history_${mac.replace(":", "")}.json")

    fun load(ctx: Context, mac: String): List<LogRec> {
        val f = file(ctx, mac)
        if (!f.exists()) return emptyList()
        return try {
            val arr = JSONArray(f.readText())
            val out = ArrayList<LogRec>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                out.add(
                    LogRec(
                        o.optLong("t"),
                        o.optInt("c"),
                        o.optDouble("tc", 0.0).toFloat(),
                        o.optDouble("h", 0.0).toFloat(),
                        o.optInt("mv")
                    )
                )
            }
            out
        } catch (e: Exception) {
            emptyList()
        }
    }

    /** Добавить записи (дубликаты по времени отбрасываются), вернуть всю историю. */
    fun append(ctx: Context, mac: String, recs: List<LogRec>): List<LogRec> {
        val existing = load(ctx, mac).associateBy { it.ts }.toMutableMap()
        for (r in recs) if (r.ts > 0) existing[r.ts] = r
        val merged = existing.values.sortedBy { it.ts }.takeLast(MAX_RECS)
        try {
            val arr = JSONArray()
            for (r in merged) {
                arr.put(
                    JSONObject()
                        .put("t", r.ts).put("c", r.co2)
                        .put("tc", r.temp.toDouble()).put("h", r.hum.toDouble())
                        .put("mv", r.mv)
                )
            }
            file(ctx, mac).writeText(arr.toString())
        } catch (_: Exception) {
        }
        return merged
    }

    fun clear(ctx: Context, mac: String) {
        try {
            file(ctx, mac).delete()
        } catch (_: Exception) {
        }
    }
}
