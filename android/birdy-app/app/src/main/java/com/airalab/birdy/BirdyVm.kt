package com.airalab.birdy

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import no.nordicsemi.android.dfu.DfuProgressListenerAdapter
import no.nordicsemi.android.dfu.DfuServiceInitiator
import no.nordicsemi.android.dfu.DfuServiceListenerHelper
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Запись журнала с платы (12 байт на устройстве). */
data class LogRec(
    val ts: Long,      /* локальное время (unix + смещение пояса), сек */
    val co2: Int,
    val temp: Float,
    val hum: Float,
    val mv: Int,
)

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
    val vol: Int = 70,      /* громкость трели, % */
    val night: Boolean = false,   /* ночной режим */
    val nightFrom: Int = 22,
    val nightTo: Int = 8,
    val clockOk: Boolean = false, /* часы синхронизированы */
    val nightNow: Boolean = false, /* сейчас внутри ночного периода */
    val fwVer: Int = 0,           /* версия конфига прошивки (INFO[0]) */
    val supportsNight: Boolean = false, /* прошивка умеет громкость/ночь (INFO >= 20) */
    val rssi: Int = 0,
    val servo: Int = 0,
    val answer: String = "",
    val error: String? = null,
    /* --- OTA DFU --- */
    val dfuUri: String? = null,
    val dfuName: String = "",
    val dfuProgress: Int = -1,
    val dfuStatus: String = "",
    val dfuError: String? = null,
    val dfuActive: Boolean = false,
    val downloading: Boolean = false,
    val dlProgress: Int = 0,
    /* --- журнал с платы --- */
    val logTier: Int = 1,          /* 0 = 3 часа, 1 = 30 суток, 2 = год */
    val logRecs: List<LogRec> = emptyList(),
    val logLoading: Boolean = false,
    val logProgress: Int = 0,
    val logError: String? = null,
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

private const val FIRMWARE_URL =
    "https://github.com/Airalab-Rus/NRF52840-SCD41-BLE/releases/latest/download/CO2-Birdy-2BLE-dfu.zip"

class BirdyVm(app: Application) : AndroidViewModel(app) {

    private val ble = BirdyBle(app)
    private val _ui = MutableStateFlow(Ui())
    val ui: StateFlow<Ui> = _ui.asStateFlow()

    private var lastAddress: String? = null
    private var logTotal = 0
    private var logNext = 0
    private var scanJob: Job? = null
    private var refreshJob: Job? = null
    private var previewJob: Job? = null
    private var rssiJob: Job? = null

    /* ------------------------------- OTA DFU ------------------------------- */

    private val dfuListener = object : DfuProgressListenerAdapter() {
        override fun onDeviceConnecting(a: String) {
            _ui.update { it.copy(dfuStatus = "Подключение…", dfuActive = true) }
        }
        override fun onEnablingDfuMode(a: String) {
            _ui.update { it.copy(dfuStatus = "Переход в бутлоадер…") }
        }
        override fun onDfuProcessStarting(a: String) {
            _ui.update { it.copy(dfuStatus = "Старт обновления…") }
        }
        override fun onFirmwareValidating(a: String) {
            _ui.update { it.copy(dfuStatus = "Проверка образа…") }
        }
        override fun onProgressChanged(
            a: String, percent: Int, speed: Float, avgSpeed: Float,
            currentPart: Int, partsTotal: Int
        ) {
            _ui.update { it.copy(dfuProgress = percent, dfuStatus = "Запись: $percent %") }
        }
        override fun onDfuCompleted(a: String) {
            _ui.update {
                it.copy(
                    dfuProgress = 100, dfuActive = false,
                    dfuStatus = "Готово! Плата перезагружается — подключитесь заново"
                )
            }
            startScan()
        }
        override fun onDfuAborted(a: String) {
            _ui.update { it.copy(dfuActive = false, dfuStatus = "Отменено") }
        }
        override fun onError(a: String, error: Int, errorType: Int, message: String?) {
            _ui.update {
                it.copy(
                    dfuActive = false,
                    dfuStatus = "Ошибка обновления",
                    dfuError = "тип $errorType, код $error: ${message ?: ""}"
                )
            }
        }
    }

    init {
        ble.onDevices = { list -> _ui.update { it.copy(devices = list, scanning = true) } }

        ble.onConnected = {
            _ui.update { it.copy(connected = true, connecting = false, error = null) }
            ble.readInfo()
            startRssiPolling()
            /* Часы платы синхронизируем с телефоном (для ночного режима). */
            ble.send("time=" + java.time.LocalTime.now()
                .format(java.time.format.DateTimeFormatter.ofPattern("HH:mm")))
            /* Эпоха = unix + смещение пояса: журнал и ночной режим получают реальное время */
            val tzSec = java.util.TimeZone.getDefault()
                .getOffset(System.currentTimeMillis()) / 1000L
            ble.send("epoch=" + (System.currentTimeMillis() / 1000L + tzSec))
        }

        ble.onDisconnected = {
            _ui.update { it.copy(connected = false, connecting = false) }
            rssiJob?.cancel()
            startScan()
        }

        ble.onInfo = { v -> parseInfo(v) }
        ble.onText = { t -> parseText(t) }
        ble.onRssi = { r -> _ui.update { it.copy(rssi = r) } }
        ble.onLog = { v -> handleLog(v) }
        ble.onError = { e -> _ui.update { it.copy(error = e, connecting = false) } }

        DfuServiceListenerHelper.registerProgressListener(app, dfuListener)
    }

    /** Выбранный .zip (DFU-пакет). */
    fun setDfuFile(uri: Uri) {
        var name = uri.lastPathSegment ?: "firmware.zip"
        try {
            getApplication<Application>().contentResolver
                .query(uri, null, null, null, null)?.use { c ->
                    val i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (i >= 0 && c.moveToFirst()) name = c.getString(i) ?: name
                }
        } catch (_: Exception) {
        }
        try {
            getApplication<Application>().contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        } catch (_: Exception) {
        }
        _ui.update { it.copy(dfuUri = uri.toString(), dfuName = name, dfuError = null, dfuStatus = "") }
    }

    /** Скачать последний DFU-пакет с GitHub Releases. */
    fun downloadFirmware() {
        if (_ui.value.downloading) return
        _ui.update {
            it.copy(downloading = true, dlProgress = 0, dfuError = null, dfuStatus = "Скачивание с GitHub…")
        }
        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) {
                    val app = getApplication<Application>()
                    val conn = (URL(FIRMWARE_URL).openConnection() as HttpURLConnection).apply {
                        instanceFollowRedirects = true
                        connectTimeout = 15000
                        readTimeout = 30000
                        setRequestProperty("User-Agent", "BirdyApp")
                    }
                    conn.connect()
                    val code = conn.responseCode
                    if (code !in 200..299) throw IOException("HTTP $code (релиз не найден?)")
                    val total = conn.contentLengthLong
                    val file = File(app.cacheDir, "firmware-dfu.zip")
                    conn.inputStream.use { input ->
                        file.outputStream().use { out ->
                            val buf = ByteArray(16 * 1024)
                            var done = 0L
                            while (true) {
                                val read = input.read(buf)
                                if (read <= 0) break
                                out.write(buf, 0, read)
                                done += read
                                if (total > 0) {
                                    val p = (done * 100 / total).toInt()
                                    _ui.update { it.copy(dlProgress = p) }
                                }
                            }
                        }
                    }
                    conn.disconnect()

                    /* базовые проверки: это должен быть zip-пакет DFU */
                    if (file.length() < 20_000) throw IOException("файл слишком маленький")
                    val uri = FileProvider.getUriForFile(
                        app, "${app.packageName}.fileprovider", file
                    )
                    _ui.update {
                        it.copy(
                            downloading = false, dlProgress = 100,
                            dfuUri = uri.toString(),
                            dfuName = "CO2-Birdy-2BLE-dfu.zip · ${file.length() / 1024} КБ",
                            dfuStatus = "Скачано — можно обновлять"
                        )
                    }
                }
            } catch (e: Exception) {
                _ui.update {
                    it.copy(
                        downloading = false,
                        dfuError = "Не удалось скачать: ${e.message}"
                    )
                }
            }
        }
    }

    /** Запуск OTA: отключаем GATT и отдаём адрес библиотеке DFU. */
    fun startDfu() {
        val s = _ui.value
        val uriStr = s.dfuUri
        val addr = lastAddress
        if (uriStr == null) {
            _ui.update { it.copy(dfuError = "Сначала выберите .zip с прошивкой") }
            return
        }
        if (addr == null) {
            _ui.update { it.copy(dfuError = "Сначала подключитесь к плате") }
            return
        }

        ble.close() /* освобождаем GATT для DFU */
        _ui.update {
            it.copy(
                connected = false, dfuActive = true, dfuProgress = 0,
                dfuStatus = "Подготовка…", dfuError = null
            )
        }
        try {
            /* канал уведомлений создаём заранее: без него startForeground в сервисе падает */
            try {
                DfuServiceInitiator.createDfuNotificationChannel(getApplication())
            } catch (_: Exception) {
            }
            DfuServiceInitiator(addr)
                .setDeviceName("DfuTarg")
                .setKeepBond(false)
                .setForeground(true)
                .setPacketsReceiptNotificationsEnabled(true)
                .setPacketsReceiptNotificationsValue(12)
                .setForceScanningForNewAddressInLegacyDfu(true)
                .setScanTimeout(30000)
                .setZip(Uri.parse(uriStr))
                .start(getApplication(), DfuService::class.java)
        } catch (e: Throwable) {
            _ui.update {
                it.copy(
                    dfuActive = false, dfuStatus = "",
                    dfuError = "Не удалось запустить DFU: ${e.javaClass.simpleName}: ${e.message}"
                )
            }
        }
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
        lastAddress = address
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

    fun setVol(v: Int) {
        if (!_ui.value.supportsNight) {
            _ui.update {
                it.copy(dfuError = "Прошивка платы v${it.fwVer} не знает громкость — обновите прошивку (OTA)")
            }
            return
        }
        _ui.update { it.copy(vol = v) }
        send("vol=$v")
    }

    fun setNight(on: Boolean) {
        if (!_ui.value.supportsNight) {
            _ui.update {
                it.copy(dfuError = "Прошивка платы v${it.fwVer} не знает ночной режим — обновите прошивку (OTA)")
            }
            return
        }
        _ui.update { it.copy(night = on) }
        send("night=${if (on) 1 else 0}")
    }

    fun setNightFrom(h: Int) {
        _ui.update { it.copy(nightFrom = h) }
        send("nfrom=$h")
    }

    fun setNightTo(h: Int) {
        _ui.update { it.copy(nightTo = h) }
        send("nto=$h")
    }

    /* -------------------------------- журнал -------------------------------- */

    /** Загрузить журнал выбранного уровня: 0 = 3 ч, 1 = 30 сут, 2 = год. */
    fun loadLog(tier: Int = _ui.value.logTier) {
        if (!_ui.value.connected) {
            _ui.update { it.copy(logError = "Нет подключения к плате") }
            return
        }
        logTotal = 0
        logNext = 0
        _ui.update {
            it.copy(
                logTier = tier, logLoading = true, logRecs = emptyList(),
                logProgress = 0, logError = null
            )
        }
        if (!ble.logRequest(byteArrayOf(0x01, tier.toByte()))) {
            _ui.update { it.copy(logLoading = false, logError = "Плата не поддерживает журнал") }
            return
        }
        viewModelScope.launch {
            delay(4000)
            if (_ui.value.logLoading && _ui.value.logRecs.isEmpty()) {
                _ui.update { it.copy(logLoading = false, logError = "Нет ответа от платы") }
            }
        }
    }

    fun clearLog() {
        ble.logRequest(byteArrayOf(0x03))
        _ui.update { it.copy(logRecs = emptyList(), logProgress = 100, logError = null) }
    }

    private fun requestChunk() {
        val idx = logNext
        ble.logRequest(
            byteArrayOf(
                0x02, _ui.value.logTier.toByte(),
                (idx and 0xFF).toByte(), ((idx shr 8) and 0xFF).toByte()
            )
        )
    }

    private fun handleLog(v: ByteArray) {
        if (v.size < 4) return
        when (v[0].toInt() and 0xFF) {
            0x01 -> {
                logTotal = (v[2].toInt() and 0xFF) or ((v[3].toInt() and 0xFF) shl 8)
                if (logTotal == 0) {
                    _ui.update { it.copy(logLoading = false, logProgress = 100) }
                    return
                }
                logNext = 0
                requestChunk()
            }
            0x02 -> {
                if (v.size < 6) return
                val idx = (v[2].toInt() and 0xFF) or ((v[3].toInt() and 0xFF) shl 8)
                val n = v[4].toInt() and 0xFF
                if (idx != logNext || n == 0) {
                    _ui.update { it.copy(logLoading = false, logError = "Сбой приёма (idx=$idx)") }
                    return
                }
                val recs = ArrayList<LogRec>(n)
                var off = 6
                for (i in 0 until n) {
                    if (off + 12 > v.size) break
                    fun u8(k: Int) = v[off + k].toLong() and 0xFF
                    val ts = (u8(0) or (u8(1) shl 8) or (u8(2) shl 16) or (u8(3) shl 24))
                    val co2 = (u8(4) or (u8(5) shl 8)).toInt()
                    val tRaw = (u8(6) or (u8(7) shl 8)).toInt().toShort().toInt()
                    val hum = (u8(8) or (u8(9) shl 8)).toInt() / 100f
                    val mv = (u8(10) or (u8(11) shl 8)).toInt()
                    recs.add(LogRec(ts, co2, tRaw / 100f, hum, mv))
                    off += 12
                }
                logNext += recs.size
                _ui.update {
                    it.copy(
                        logRecs = it.logRecs + recs,
                        logProgress = if (logTotal > 0) logNext * 100 / logTotal else 0
                    )
                }
                if (logNext < logTotal) requestChunk()
                else _ui.update { it.copy(logLoading = false, logProgress = 100) }
            }
        }
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
        val vol = if (v.size >= 17) v[16].toInt() and 0xFF else 70
        val flags = if (v.size >= 18) v[17].toInt() and 0xFF else 0
        val nfrom = if (v.size >= 19) v[18].toInt() and 0xFF else 22
        val nto = if (v.size >= 20) v[19].toInt() and 0xFF else 8
        val fwVer = v[0].toInt() and 0xFF
        val supports = v.size >= 20

        _ui.update {
            it.copy(
                thr = thr, alive = alive, deadAngle = dead, sec = sec, snd = snd,
                dead = deadState, co2 = co2, hyst = hyst, batMv = mv, toff = toff,
                vol = vol, night = (flags and 1) != 0, clockOk = (flags and 2) != 0,
                nightNow = (flags and 4) != 0, nightFrom = nfrom, nightTo = nto,
                fwVer = fwVer, supportsNight = supports,
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
                vol = map["vol"]?.toIntOrNull() ?: s.vol,
                night = map["night"]?.toIntOrNull()?.let { it != 0 } ?: s.night,
                nightFrom = map["nfrom"]?.toIntOrNull() ?: s.nightFrom,
                nightTo = map["nto"]?.toIntOrNull() ?: s.nightTo,
                clockOk = map["clock"]?.let { it == "ok" } ?: s.clockOk,
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
        try {
            DfuServiceListenerHelper.unregisterProgressListener(getApplication(), dfuListener)
        } catch (_: Exception) {
        }
        ble.close()
        super.onCleared()
    }
}
