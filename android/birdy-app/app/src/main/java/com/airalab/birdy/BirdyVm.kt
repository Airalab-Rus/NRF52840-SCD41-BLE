package com.airalab.birdy

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** Состояние экрана. */
data class Ui(
    val devices: List<BirdyBle.Found> = emptyList(),
    val scanning: Boolean = false,
    val connecting: Boolean = false,
    val connected: Boolean = false,
    val co2: Int = 0,
    val dead: Boolean = false,
    val thr: Int = 600,
    val hyst: Int = 100,
    val alive: Int = 0,
    val deadAngle: Int = 100,
    val sec: Int = 8,
    val snd: Boolean = true,
    val batMv: Int = 0,
    val toff: Int = 0,      /* смещение температуры SCD41, 0.1 °C */
    val rssi: Int = 0,
    val servo: Int = 0,
    val answer: String = "",
    val error: String? = null,
) {
    /** Процент АКБ по кривой Li-Ion (3.3 В = 0 %, 4.2 В = 100 %). */
    val batPct: Int
        get() {
            val mv = batMv
            return when {
                mv <= 0 -> 0
                mv >= 4200 -> 100
                mv <= 3300 -> 0
                else -> {
                    val pts = intArrayOf(4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3400, 3300)
                    val pct = intArrayOf(100, 95, 88, 78, 66, 52, 38, 24, 12, 0)
                    var res = 0
                    for (i in 0 until pts.size - 1) {
                        if (mv <= pts[i] && mv >= pts[i + 1]) {
                            val span = pts[i] - pts[i + 1]
                            val dp = pct[i] - pct[i + 1]
                            res = pct[i] - (dp * (pts[i] - mv)) / span
                            break
                        }
                    }
                    res
                }
            }
        }

    val co2Color: Long
        get() = when {
            co2 >= thr -> 0xFFE5484D
            co2 >= thr - hyst -> 0xFFF5A524
            else -> 0xFF2FBF71
        }
}

class BirdyVm(app: Application) : AndroidViewModel(app) {

    private val ble = BirdyBle(app)
    private val _ui = MutableStateFlow(Ui())
    val ui: StateFlow<Ui> = _ui.asStateFlow()

    private var scanJob: Job? = null
    private var refreshJob: Job? = null
    private var previewJob: Job? = null
    private var rssiJob: Job? = null

    init {
        ble.onDevices = { list -> _ui.update { it.copy(devices = list, scanning = true) } }

        ble.onConnected = {
            _ui.update { it.copy(connected = true, connecting = false, error = null) }
            ble.readInfo()
            startRssiPolling()
        }

        ble.onDisconnected = {
            _ui.update { it.copy(connected = false, connecting = false) }
            rssiJob?.cancel()
            startScan()
        }

        ble.onInfo = { v -> parseInfo(v) }
        ble.onText = { t -> parseText(t) }
        ble.onRssi = { r -> _ui.update { it.copy(rssi = r) } }
        ble.onError = { e -> _ui.update { it.copy(error = e, connecting = false) } }
    }

    /* ------------------------------ разрешения/скан ------------------------------ */

    fun permissions(): Array<String> = ble.permissions()
    fun hasPermissions(): Boolean = ble.hasPermissions()
    fun bluetoothOn(): Boolean = ble.bluetoothOn()

    fun startScan() {
        if (!ble.hasPermissions()) return
        if (!ble.bluetoothOn()) {
            _ui.update { it.copy(error = "Включите Bluetooth") }
            return
        }
        _ui.update { it.copy(scanning = true) }
        ble.startScan()
        if (scanJob?.isActive != true) {
            scanJob = viewModelScope.launch {
                /* Устройство рекламируется окнами ~30 с раз в минуту — скан перезапускаем. */
                while (isActive && !_ui.value.connected) {
                    delay(15_000)
                    if (!_ui.value.connected) ble.restartScan()
                }
            }
        }
    }

    fun connect(address: String) {
        scanJob?.cancel()
        _ui.update { it.copy(connecting = true, scanning = false, error = null) }
        ble.connect(address)
    }

    fun disconnect() {
        ble.disconnect()
        _ui.update { it.copy(connected = false) }
    }

    fun clearError() = _ui.update { it.copy(error = null) }

    /* ---------------------------------- команды ---------------------------------- */

    /** Отправить текст и запросить обновление конфига. */
    fun send(text: String) {
        ble.send(text)
        _ui.update { it.copy(answer = "→ $text") }
        refreshJob?.cancel()
        refreshJob = viewModelScope.launch {
            delay(700)
            ble.readInfo()
        }
    }

    fun setThr(v: Int) {
        _ui.update { it.copy(thr = v) }
        send("thr=$v")
    }

    fun setHyst(v: Int) {
        _ui.update { it.copy(hyst = v) }
        send("hyst=$v")
    }

    fun setSec(v: Int) {
        _ui.update { it.copy(sec = v) }
        send("sec=$v")
    }

    fun setSound(on: Boolean) {
        _ui.update { it.copy(snd = on) }
        send("snd=${if (on) 1 else 0}")
    }

    fun setAlive(v: Int) {
        _ui.update { it.copy(alive = v, servo = v) }
        send("alive=$v")
    }

    fun setDead(v: Int) {
        _ui.update { it.copy(deadAngle = v, servo = v) }
        send("dead=$v")
    }

    /** Смещение температуры датчика, 0.1 °C (-50..50). */
    fun setToff(v: Int) {
        _ui.update { it.copy(toff = v) }
        send("toff=$v")
    }

    /** Живой предпросмотр положения (пока тянешь ползунок). */
    fun preview(v: Int) {
        _ui.update { it.copy(servo = v) }
        previewJob?.cancel()
        previewJob = viewModelScope.launch {
            delay(220)
            ble.send("goto=$v")
        }
    }

    /** Произвольная текстовая команда (meas, sweep, up, down, cal=400, reset). */
    fun command(c: String) = send(c)

    fun refresh() {
        ble.readInfo()
        ble.readRssi()
        send("get")
    }

    /* ---------------------------------- разбор ---------------------------------- */

    private fun parseInfo(v: ByteArray) {
        if (v.size < 12) return
        val thr = (v[1].toInt() and 0xFF) or ((v[2].toInt() and 0xFF) shl 8)
        val alive = v[3].toInt()          /* int8 */
        val dead = v[4].toInt()
        val sec = v[5].toInt() and 0xFF
        val snd = (v[6].toInt() and 0xFF) != 0
        val deadState = (v[7].toInt() and 0xFF) != 0
        val co2 = (v[8].toInt() and 0xFF) or ((v[9].toInt() and 0xFF) shl 8)
        val hyst = (v[10].toInt() and 0xFF) or ((v[11].toInt() and 0xFF) shl 8)
        val mv = if (v.size >= 14) {
            (v[12].toInt() and 0xFF) or ((v[13].toInt() and 0xFF) shl 8)
        } else 0
        val toff = if (v.size >= 16) {
            ((v[14].toInt() and 0xFF) or ((v[15].toInt() and 0xFF) shl 8)).toShort().toInt()
        } else 0

        _ui.update {
            it.copy(
                thr = thr, alive = alive, deadAngle = dead, sec = sec, snd = snd,
                dead = deadState, co2 = co2, hyst = hyst, batMv = mv, toff = toff,
                servo = if (deadState) dead else alive
            )
        }
    }

    private fun parseText(t: String) {
        if (t.isBlank()) return
        val map = HashMap<String, String>()
        t.split(' ').forEach { part ->
            val i = part.indexOf('=')
            if (i > 0) map[part.substring(0, i)] = part.substring(i + 1)
        }
        _ui.update { s ->
            s.copy(
                answer = t,
                thr = map["thr"]?.toIntOrNull() ?: s.thr,
                hyst = map["hyst"]?.toIntOrNull() ?: s.hyst,
                alive = map["alive"]?.toIntOrNull() ?: s.alive,
                deadAngle = map["dead"]?.toIntOrNull() ?: s.deadAngle,
                sec = map["sec"]?.toIntOrNull() ?: s.sec,
                snd = map["snd"]?.let { it != "0" } ?: s.snd,
                dead = map["deadstate"]?.let { it == "1" } ?: s.dead,
                co2 = map["co2"]?.toIntOrNull() ?: s.co2,
                batMv = map["bat"]?.removeSuffix("mv")?.toIntOrNull() ?: s.batMv,
                toff = map["toff"]?.toIntOrNull() ?: s.toff,
            )
        }
    }

    private fun startRssiPolling() {
        rssiJob?.cancel()
        rssiJob = viewModelScope.launch {
            while (isActive && _ui.value.connected) {
                ble.readRssi()
                delay(5_000)
            }
        }
    }

    override fun onCleared() {
        ble.close()
        super.onCleared()
    }
}
