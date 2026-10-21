package com.medibot.app

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

data class Slot(val time: String, val enabled: Boolean)

data class HistoryItem(val time: String, val status: String, val slot: Int)

data class RobotState(
    val online: Boolean = false,
    val state: String = "OFFLINE",
    val status: String = "Not connected",
    val line1: String = "",
    val line2: String = "",
    val time: String = "--:--:--",
    val synced: Boolean = false,
    val next: String = "--:--",
    val alert: Boolean = false,
    val distance: Int = 0,
    val slots: List<Slot> = List(4) { Slot("08:00", false) },
    val history: List<HistoryItem> = emptyList()
)

/** Talks to the ESP32 over plain HTTP. All calls are suspend functions (run on IO thread). */
class RobotApi(host: String) {

    var host: String = clean(host)

    private fun clean(h: String) = h.trim().removePrefix("http://").removeSuffix("/")

    fun setHostAddress(h: String) {
        host = clean(h)
    }

    suspend fun get(path: String): String = request(path, "GET", null)

    private suspend fun request(path: String, method: String, body: String?): String =
        withContext(Dispatchers.IO) {
            val conn = URL("http://$host$path").openConnection() as HttpURLConnection
            try {
                conn.connectTimeout = 2500
                conn.readTimeout = 3500
                conn.requestMethod = method
                if (body != null) {
                    conn.doOutput = true
                    conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                    conn.outputStream.use { it.write(body.toByteArray()) }
                }
                val code = conn.responseCode
                val stream = if (code in 200..299) conn.inputStream else conn.errorStream
                val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
                if (code !in 200..299) throw IOException("Robot said HTTP $code $text")
                text
            } finally {
                conn.disconnect()
            }
        }

    suspend fun getState(): RobotState {
        val o = JSONObject(get("/api/state"))

        val slots = mutableListOf<Slot>()
        val sa = o.optJSONArray("slots")
        if (sa != null) {
            for (i in 0 until sa.length()) {
                val s = sa.getJSONObject(i)
                slots.add(Slot(s.optString("t", "08:00"), s.optInt("e", 0) == 1))
            }
        }
        while (slots.size < 4) slots.add(Slot("08:00", false))

        val hist = mutableListOf<HistoryItem>()
        val ha = o.optJSONArray("history")
        if (ha != null) {
            for (i in 0 until ha.length()) {
                val h = ha.getJSONObject(i)
                hist.add(HistoryItem(h.optString("t"), h.optString("s"), h.optInt("slot", -1)))
            }
        }

        return RobotState(
            online = true,
            state = o.optString("state", "?"),
            status = o.optString("status", ""),
            line1 = o.optString("l1", ""),
            line2 = o.optString("l2", ""),
            time = o.optString("time", "--:--:--"),
            synced = o.optBoolean("synced", false),
            next = o.optString("next", "--:--"),
            alert = o.optBoolean("alert", false),
            distance = o.optInt("dist", 0),
            slots = slots,
            history = hist
        )
    }

    suspend fun saveSchedule(slots: List<Slot>) {
        val body = slots.mapIndexed { i, s ->
            "t$i=" + URLEncoder.encode(s.time, "UTF-8") + "&e$i=" + (if (s.enabled) 1 else 0)
        }.joinToString("&")
        request("/save", "POST", body)
    }

    suspend fun deliverNow() { get("/test") }
    suspend fun stop() { get("/api/stop") }
    suspend fun beep() { get("/api/beep") }
    suspend fun motorTest() { get("/api/motortest") }
    suspend fun clearHistory() { get("/api/clearhistory") }
    suspend fun setTime(epochSeconds: Long) { get("/api/settime?epoch=$epochSeconds") }
    suspend fun drive(dir: String, speed: Int) { get("/api/drive?d=$dir&sp=$speed") }
}
