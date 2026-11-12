package com.airalab.birdy

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

private val Bg = Color(0xFF0E1116)
private val CardBg = Color(0xFF171C24)
private val Line = Color(0xFF232A35)
private val Green = Color(0xFF2FBF71)
private val Red = Color(0xFFE5484D)
private val Amber = Color(0xFFF5A524)
private val Blue = Color(0xFF4C8DFF)

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CrashLog.install(applicationContext)
        setContent {
            val vm: BirdyVm = viewModel()
            val ui by vm.ui.collectAsStateWithLifecycle()

            val permLauncher = rememberLauncherForActivityResult(
                ActivityResultContracts.RequestMultiplePermissions()
            ) { vm.startScan() }

            LaunchedEffect(Unit) {
                if (vm.hasPermissions()) vm.startScan() else permLauncher.launch(vm.permissions())
            }

            MaterialTheme(
                colorScheme = darkColorScheme(primary = Green, secondary = Blue, background = Bg)
            ) {
                Surface(Modifier.fillMaxSize(), color = Bg) {
                    if (ui.connected) {
                        ControlScreen(ui, vm)
                    } else {
                        ScanScreen(ui, vm, onGrant = { permLauncher.launch(vm.permissions()) })
                    }
                }
            }
        }
    }
}

/* ================================ Экран поиска ================================ */

@Composable
fun ScanScreen(ui: Ui, vm: BirdyVm, onGrant: () -> Unit) {
    val birdy = ui.devices.firstOrNull { it.isBirdy }
    val others = ui.devices.filter { !it.isBirdy }

    Column(Modifier.fillMaxSize().padding(20.dp)) {
        Text("CO2-Birdy", fontSize = 30.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(4.dp))
        Text(
            when {
                ui.connecting -> "Подключаюсь…"
                ui.scanning -> "Ищу устройство в эфире…"
                else -> "Скан остановлен"
            },
            color = Color(0xFF98A2B3), fontSize = 14.sp
        )

        if (ui.error != null) {
            Spacer(Modifier.height(14.dp))
            Card(
                colors = CardDefaults.cardColors(containerColor = Color(0xFF3A1A1C)),
                shape = RoundedCornerShape(16.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(Modifier.padding(14.dp)) {
                    Text(ui.error, color = Color(0xFFFFB4B4), fontSize = 14.sp)
                    Row {
                        TextButton(onClick = { vm.clearError(); vm.startScan() }) { Text("Повторить") }
                        TextButton(onClick = onGrant) { Text("Разрешения") }
                    }
                }
            }
        }

        Spacer(Modifier.height(18.dp))

        if (birdy != null) {
            Card(
                colors = CardDefaults.cardColors(containerColor = CardBg),
                shape = RoundedCornerShape(20.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(Modifier.padding(18.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            Modifier.size(10.dp).clip(RoundedCornerShape(5.dp)).background(Green)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text("НАЙДЕНО", color = Green, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(birdy.name, color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                    Text("${birdy.address}    RSSI ${birdy.rssi} dBm", color = Color(0xFF98A2B3), fontSize = 12.sp)
                    Spacer(Modifier.height(14.dp))
                    Button(
                        onClick = { vm.connect(birdy.address) },
                        enabled = !ui.connecting,
                        colors = ButtonDefaults.buttonColors(containerColor = Green),
                        shape = RoundedCornerShape(14.dp),
                        modifier = Modifier.fillMaxWidth().height(52.dp)
                    ) {
                        if (ui.connecting) {
                            CircularProgressIndicator(Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                        } else {
                            Text("ПОДКЛЮЧИТЬСЯ", fontSize = 16.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
        } else {
            Card(
                colors = CardDefaults.cardColors(containerColor = CardBg),
                shape = RoundedCornerShape(20.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(Modifier.padding(18.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(Modifier.size(28.dp), color = Blue, strokeWidth = 3.dp)
                    Spacer(Modifier.height(12.dp))
                    Text("Ждём CO2-Birdy…", color = Color.White, fontSize = 16.sp)
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "Плата выходит в эфир окнами ~30 секунд раз в минуту.\nОбычно нужно подождать до 1 минуты.",
                        color = Color(0xFF98A2B3), fontSize = 12.sp, textAlign = TextAlign.Center
                    )
                }
            }
        }

        Spacer(Modifier.height(20.dp))

        if (others.isNotEmpty()) {
            Text("Другие устройства", color = Color(0xFF6B7280), fontSize = 12.sp)
            Spacer(Modifier.height(6.dp))
            LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
                items(others) { d ->
                    Row(
                        Modifier.fillMaxWidth()
                            .clickable { vm.connect(d.address) }
                            .padding(vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(d.name, color = Color(0xFFD0D5DD), fontSize = 14.sp, modifier = Modifier.weight(1f))
                        Text("${d.rssi}", color = Color(0xFF6B7280), fontSize = 12.sp)
                    }
                }
            }
        } else {
            Spacer(Modifier.weight(1f))
        }

        TextButton(onClick = { vm.startScan() }, modifier = Modifier.fillMaxWidth()) {
            Text("Обновить поиск", color = Blue)
        }
    }
}

/* ================================ Экран настроек ================================ */

@Composable
fun ControlScreen(ui: Ui, vm: BirdyVm) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    var crash by remember { mutableStateOf(CrashLog.read(ctx)) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("CO2-Birdy", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Text(
                    "подключено · RSSI ${ui.rssi} dBm",
                    color = Color(0xFF98A2B3), fontSize = 12.sp
                )
            }
            TextButton(onClick = { vm.disconnect() }) { Text("Выйти", color = Red) }
        }

        Spacer(Modifier.height(8.dp))

        Card(
            colors = CardDefaults.cardColors(containerColor = CardBg),
            shape = RoundedCornerShape(22.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(Modifier.padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                BirdyGauge(
                    pos = ui.servo,
                    alive = ui.alive,
                    dead = ui.deadAngle,
                    isDead = ui.dead
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    if (ui.dead) "ПТИЧКА МЁРТВА" else "ПТИЧКА ЖИВА",
                    color = if (ui.dead) Red else Green,
                    fontSize = 18.sp, fontWeight = FontWeight.Bold
                )
                Spacer(Modifier.height(10.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    Text("${ui.co2}", color = Color.White, fontSize = 40.sp, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.width(6.dp))
                    Text("ppm", color = Color(0xFF98A2B3), fontSize = 14.sp, modifier = Modifier.padding(bottom = 8.dp))
                }
                Spacer(Modifier.height(10.dp))
                Co2Bar(co2 = ui.co2, thr = ui.thr, color = Color(ui.co2Color))
                Spacer(Modifier.height(6.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("400", color = Color(0xFF6B7280), fontSize = 11.sp)
                    Text("порог ${ui.thr} ppm", color = Color(0xFF98A2B3), fontSize = 11.sp)
                    Text("2000", color = Color(0xFF6B7280), fontSize = 11.sp)
                }
                Spacer(Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("АКБ  ", color = Color(0xFF98A2B3), fontSize = 13.sp)
                    Text(
                        if (ui.batMv > 0) "%.2f В · %d %%".format(ui.batMv / 1000f, ui.batPct) else "—",
                        color = when {
                            ui.batMv in 1..3300 -> Red
                            ui.batMv in 3301..3500 -> Amber
                            ui.batMv > 0 -> Green
                            else -> Color(0xFF98A2B3)
                        },
                        fontSize = 13.sp, fontWeight = FontWeight.Bold
                    )
                }
            }
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Пороги") {
            ParamRow(
                title = "Порог CO₂ (птичка умирает)",
                value = ui.thr, unit = "ppm", range = 300..3000, step = 50
            ) { v -> vm.setThr(v) }
            ParamRow(
                title = "Гистерезис (оживает ниже)",
                value = ui.hyst, unit = "ppm", range = 0..500, step = 10
            ) { v -> vm.setHyst(v) }
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Положение птички") {
            ParamRow(
                title = "Угол ЖИВА",
                value = ui.alive, unit = "%", range = -100..100, step = 1,
                onPreview = { v -> vm.preview(v) }
            ) { v -> vm.setAlive(v) }
            ParamRow(
                title = "Угол МЁРТВА",
                value = ui.deadAngle, unit = "%", range = -100..100, step = 1,
                onPreview = { v -> vm.preview(v) }
            ) { v -> vm.setDead(v) }
            ParamRow(
                title = "Время хода",
                value = ui.sec, unit = "с", range = 0..30, step = 1
            ) { v -> vm.setSec(v) }
            Row(
                Modifier.fillMaxWidth().padding(top = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Звук (трель)", color = Color.White, fontSize = 14.sp, modifier = Modifier.weight(1f))
                Switch(checked = ui.snd, onCheckedChange = { vm.setSound(it) })
            }
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Датчик: температура") {
            var t by remember(ui.toff) { mutableStateOf(ui.toff.toFloat()) }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "Смещение температуры",
                    color = Color.White, fontSize = 14.sp, modifier = Modifier.weight(1f)
                )
                Text(
                    "%.1f °C".format(t / 10f),
                    color = Green, fontSize = 14.sp, fontWeight = FontWeight.Bold
                )
            }
            Slider(
                value = t,
                onValueChange = { t = it },
                onValueChangeFinished = { vm.setToff(t.roundToInt()) },
                valueRange = -50f..50f,
                steps = 99
            )
            Text(
                "Калибровка CO₂ не требуется: датчик откалиброван на заводе (автокалибровка AFC работает сама).",
                color = Color(0xFF6B7280), fontSize = 11.sp
            )
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Звук") {
            var vol by remember(ui.vol) { mutableStateOf(ui.vol.toFloat()) }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("Громкость трели", color = Color.White, fontSize = 14.sp, modifier = Modifier.weight(1f))
                Text("${vol.roundToInt()} %", color = Green, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            }
            Slider(
                value = vol,
                onValueChange = { vol = it },
                onValueChangeFinished = { vm.setVol(vol.roundToInt()) },
                valueRange = 0f..100f,
                steps = 19,
                enabled = ui.supportsNight
            )

            Row(
                Modifier.fillMaxWidth().padding(top = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Ночной режим", color = Color.White, fontSize = 14.sp)
                    Text(
                        when {
                            !ui.clockOk -> "часы не синхронизированы — режим ждёт синхронизации"
                            ui.nightNow -> "сейчас тишина"
                            else -> "сейчас звук разрешён"
                        },
                        color = if (ui.nightNow) Amber else Color(0xFF6B7280), fontSize = 11.sp
                    )
                }
                Switch(
                    checked = ui.night,
                    onCheckedChange = { vm.setNight(it) },
                    enabled = ui.supportsNight
                )
            }
            if (!ui.supportsNight) {
                Spacer(Modifier.height(6.dp))
                Text(
                    "Прошивка платы v${ui.fwVer} не поддерживает громкость/ночной режим — обновите прошивку (OTA).",
                    color = Amber, fontSize = 11.sp
                )
            }

            if (ui.night) {
                HourRow("Тишина с", ui.nightFrom) { vm.setNightFrom(it) }
                HourRow("Тишина до", ui.nightTo) { vm.setNightTo(it) }
            }
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Проверка") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SmallButton("Замер", Modifier.weight(1f)) { vm.command("meas") }
                SmallButton("Свип", Modifier.weight(1f)) { vm.command("sweep") }
                SmallButton("Трель ↑", Modifier.weight(1f)) { vm.command("up") }
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SmallButton("Трель ↓", Modifier.weight(1f)) { vm.command("down") }
                SmallButton("Сброс настроек", Modifier.weight(1f)) { vm.command("reset") }
            }
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("Прошивка (OTA)") {
            Text(
                "На плате: конфиг v${ui.fwVer}" + if (ui.supportsNight) " (актуальная)" else " — устарела",
                color = if (ui.supportsNight) Color(0xFF6B7280) else Amber,
                fontSize = 11.sp
            )
            Spacer(Modifier.height(6.dp))
            val picker = rememberLauncherForActivityResult(
                ActivityResultContracts.OpenDocument()
            ) { uri -> if (uri != null) vm.setDfuFile(uri) }

            Text(
                if (ui.dfuName.isBlank()) "Файл не выбран" else ui.dfuName,
                color = if (ui.dfuName.isBlank()) Color(0xFF6B7280) else Color.White,
                fontSize = 13.sp
            )
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SmallButton(
                    if (ui.downloading) "Качаю…" else "Скачать с GitHub",
                    Modifier.weight(1f)
                ) {
                    if (!ui.downloading) vm.downloadFirmware()
                }
                SmallButton("Выбрать .zip", Modifier.weight(1f)) {
                    picker.launch(arrayOf("application/zip", "application/octet-stream", "*/*"))
                }
            }
            Spacer(Modifier.height(8.dp))
            SmallButton(
                if (ui.dfuActive) "Идёт обновление…" else "Обновить по воздуху",
                Modifier.fillMaxWidth()
            ) {
                if (!ui.dfuActive) vm.startDfu()
            }
            if (ui.downloading || ui.dfuActive || ui.dfuProgress >= 0) {
                val p = if (ui.downloading) ui.dlProgress else ui.dfuProgress
                Spacer(Modifier.height(10.dp))
                LinearProgressIndicator(
                    progress = (p.coerceIn(0, 100)) / 100f,
                    modifier = Modifier.fillMaxWidth().height(8.dp)
                )
                Spacer(Modifier.height(6.dp))
                Text(
                    if (ui.downloading) "Скачивание: ${ui.dlProgress} %" else ui.dfuStatus,
                    color = Color(0xFF98A2B3), fontSize = 11.sp
                )
            }
            if (ui.dfuError != null) {
                Spacer(Modifier.height(6.dp))
                Text(ui.dfuError, color = Red, fontSize = 11.sp)
            }
            if (crash != null) {
                Spacer(Modifier.height(6.dp))
                Text("Прошлый запуск завершился ошибкой:", color = Amber, fontSize = 11.sp)
                Text(crash ?: "", color = Color(0xFF6B7280), fontSize = 9.sp)
                TextButton(onClick = { CrashLog.clear(ctx); crash = null }) { Text("Скрыть") }
            }
            Spacer(Modifier.height(6.dp))
            Text(
                "Нужен .zip из adafruit-nrfutil. Плата перезагрузится; телефон держи рядом ~1–2 мин.",
                color = Color(0xFF6B7280), fontSize = 11.sp
            )
        }

        Spacer(Modifier.height(14.dp))
        SectionCard("История (в памяти платы)") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                TierChip("3 часа", ui.logTier == 0, Modifier.weight(1f)) { vm.loadLog(0) }
                TierChip("30 дней", ui.logTier == 1, Modifier.weight(1f)) { vm.loadLog(1) }
                TierChip("год", ui.logTier == 2, Modifier.weight(1f)) { vm.loadLog(2) }
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SmallButton(if (ui.logLoading) "Загружаю…" else "Загрузить", Modifier.weight(1f)) {
                    if (!ui.logLoading) vm.loadLog(ui.logTier)
                }
                SmallButton("Очистить", Modifier.weight(1f)) { vm.clearLog() }
            }
            if (ui.logLoading) {
                Spacer(Modifier.height(8.dp))
                LinearProgressIndicator(
                    progress = (ui.logProgress.coerceIn(0, 100)) / 100f,
                    modifier = Modifier.fillMaxWidth().height(8.dp)
                )
            }
            if (ui.logError != null) {
                Spacer(Modifier.height(6.dp))
                Text(ui.logError, color = Red, fontSize = 11.sp)
            }
            if (ui.logRecs.isNotEmpty()) {
                Spacer(Modifier.height(10.dp))
                LogChart(ui.logRecs, ui.thr)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(fmtTs(ui.logRecs.first().ts, ui.logTier), color = Color(0xFF6B7280), fontSize = 9.sp)
                    Text(fmtTs(ui.logRecs.last().ts, ui.logTier), color = Color(0xFF6B7280), fontSize = 9.sp)
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    "${ui.logRecs.size} записей, последние:",
                    color = Color(0xFF6B7280), fontSize = 11.sp
                )
                ui.logRecs.takeLast(6).asReversed().forEach { r ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                        Text(fmtTs(r.ts, ui.logTier), color = Color(0xFF98A2B3), fontSize = 10.sp,
                            modifier = Modifier.width(84.dp))
                        Text("${r.co2}", color = Color.White, fontSize = 11.sp,
                            modifier = Modifier.width(52.dp))
                        Text("%.1f°C".format(r.temp), color = Color(0xFF98A2B3), fontSize = 10.sp,
                            modifier = Modifier.width(50.dp))
                        Text("%.0f%%".format(r.hum), color = Color(0xFF98A2B3), fontSize = 10.sp,
                            modifier = Modifier.width(40.dp))
                        Text("%.2fВ".format(r.mv / 1000f), color = Color(0xFF98A2B3), fontSize = 10.sp)
                    }
                }
            } else if (!ui.logLoading) {
                Spacer(Modifier.height(6.dp))
                Text(
                    "Плата хранит: 3 часа по минутам, 30 суток по часам, год по суткам.",
                    color = Color(0xFF6B7280), fontSize = 11.sp
                )
            }
        }

        Spacer(Modifier.height(14.dp))
        Row(Modifier.fillMaxWidth().padding(bottom = 24.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SmallButton("Обновить данные", Modifier.weight(1f)) { vm.refresh() }
            SmallButton("Выход", Modifier.weight(1f)) { vm.disconnect() }
        }

        if (ui.answer.isNotBlank()) {
            Text("Ответ: ${ui.answer}", color = Color(0xFF6B7280), fontSize = 11.sp)
            Spacer(Modifier.height(20.dp))
        }
        if (ui.error != null) {
            Text(ui.error, color = Red, fontSize = 12.sp)
            Spacer(Modifier.height(20.dp))
        }
    }
}

/* ================================== элементы ================================== */

@Composable
fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(
        colors = CardDefaults.cardColors(containerColor = CardBg),
        shape = RoundedCornerShape(20.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(title, color = Color(0xFF98A2B3), fontSize = 12.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
fun ParamRow(
    title: String,
    value: Int,
    unit: String,
    range: IntRange,
    step: Int,
    onPreview: ((Int) -> Unit)? = null,
    onCommit: (Int) -> Unit
) {
    var local by remember(value) { mutableStateOf(value.toFloat()) }
    val steps = ((range.last - range.first) / step) - 1

    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(title, color = Color.White, fontSize = 14.sp, modifier = Modifier.weight(1f))
            Text("${local.roundToInt()} $unit", color = Green, fontSize = 14.sp, fontWeight = FontWeight.Bold)
        }
        Slider(
            value = local,
            onValueChange = {
                local = it
                onPreview?.invoke(it.roundToInt())
            },
            onValueChangeFinished = { onCommit(local.roundToInt()) },
            valueRange = range.first.toFloat()..range.last.toFloat(),
            steps = if (steps > 0) steps else 0
        )
    }
}

@Composable
fun HourRow(title: String, hour: Int, onChange: (Int) -> Unit) {
    var h by remember(hour) { mutableStateOf(hour.toFloat()) }
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(title, color = Color.White, fontSize = 14.sp, modifier = Modifier.weight(1f))
        Text("%02d:00".format(h.roundToInt()), color = Blue, fontSize = 14.sp, fontWeight = FontWeight.Bold)
    }
    Slider(
        value = h,
        onValueChange = { h = it },
        onValueChangeFinished = { onChange(h.roundToInt()) },
        valueRange = 0f..23f,
        steps = 22
    )
}

@Composable
fun SmallButton(text: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        modifier = modifier.height(44.dp),
        shape = RoundedCornerShape(12.dp)
    ) {
        Text(text, fontSize = 12.sp, color = Color.White, maxLines = 1)
    }
}

@Composable
fun Co2Bar(co2: Int, thr: Int, color: Color) {
    val max = 2000f
    val min = 400f
    val frac = ((co2 - min) / (max - min)).coerceIn(0f, 1f)
    val thrFrac = ((thr - min) / (max - min)).coerceIn(0f, 1f)

    Box(
        Modifier.fillMaxWidth().height(26.dp)
            .clip(RoundedCornerShape(13.dp))
            .background(Line)
    ) {
        Box(
            Modifier.fillMaxWidth(frac).fillMaxHeight().background(color)
        )
        Canvas(Modifier.fillMaxSize()) {
            val x = size.width * thrFrac
            if (x > 2f && x < size.width - 2f) {
                drawLine(Color.White, Offset(x, 0f), Offset(x, size.height), strokeWidth = 3f)
            }
        }
    }
}

@Composable
fun BirdyGauge(pos: Int, alive: Int, dead: Int, isDead: Boolean) {
    Canvas(
        Modifier.fillMaxWidth().height(180.dp)
    ) {
        val stroke = 18.dp.toPx()
        val c = Offset(size.width / 2f, size.height * 0.95f)
        val r = minOf(size.width / 2f - stroke, size.height * 0.95f - stroke)
        val tl = Offset(c.x - r, c.y - r)
        val sz = Size(r * 2f, r * 2f)

        fun ang(p: Int) = 180f + ((p + 100f) / 200f) * 180f

        drawArc(
            color = Line, startAngle = 180f, sweepAngle = 180f, useCenter = false,
            style = Stroke(width = stroke, cap = StrokeCap.Round), topLeft = tl, size = sz
        )
        /* метки целевых положений */
        drawArc(
            color = Green, startAngle = ang(alive) - 5f, sweepAngle = 10f, useCenter = false,
            style = Stroke(width = stroke, cap = StrokeCap.Round), topLeft = tl, size = sz
        )
        drawArc(
            color = Red, startAngle = ang(dead) - 5f, sweepAngle = 10f, useCenter = false,
            style = Stroke(width = stroke, cap = StrokeCap.Round), topLeft = tl, size = sz
        )

        /* стрелка текущего положения */
        val a = Math.toRadians(ang(pos).toDouble())
        val end = Offset(
            c.x + (r - stroke) * cos(a).toFloat(),
            c.y + (r - stroke) * sin(a).toFloat()
        )
        val needle = if (isDead) Red else Green
        drawLine(needle, c, end, strokeWidth = 7.dp.toPx(), cap = StrokeCap.Round)
        drawCircle(needle, radius = 9.dp.toPx(), center = c)
    }
}

@Composable
fun TierChip(text: String, selected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        modifier = modifier.height(38.dp),
        shape = RoundedCornerShape(12.dp),
        colors = ButtonDefaults.outlinedButtonColors(
            containerColor = if (selected) Green.copy(alpha = 0.20f) else Color.Transparent
        )
    ) {
        Text(
            text,
            fontSize = 11.sp,
            color = if (selected) Green else Color(0xFF98A2B3),
            maxLines = 1
        )
    }
}

/** Время записи: ts уже в «локальном» виде (unix + смещение пояса). */
fun fmtTs(ts: Long, tier: Int): String {
    if (ts <= 0L) return "--"
    val pattern = if (tier == 2) "dd.MM.yy" else "dd.MM HH:mm"
    val f = java.text.SimpleDateFormat(pattern, java.util.Locale.getDefault())
    f.timeZone = java.util.TimeZone.getTimeZone("UTC")
    return f.format(java.util.Date(ts * 1000L))
}

@Composable
fun LogChart(recs: List<LogRec>, thr: Int) {
    val pts = recs.takeLast(200)
    Canvas(Modifier.fillMaxWidth().height(120.dp)) {
        val w = size.width
        val h = size.height
        val mn = 380f
        val mx = maxOf(2000f, pts.maxOfOrNull { it.co2.toFloat() } ?: 2000f)

        fun y(v: Float): Float = h - ((v - mn) / (mx - mn)).coerceIn(0f, 1f) * h
        fun x(i: Int): Float = if (pts.size <= 1) 0f else w * i / (pts.size - 1).toFloat()

        drawLine(Line.copy(alpha = 0.6f), Offset(0f, y(600f)), Offset(w, y(600f)), strokeWidth = 1f)
        drawLine(Line.copy(alpha = 0.6f), Offset(0f, y(1000f)), Offset(w, y(1000f)), strokeWidth = 1f)
        drawLine(
            Amber.copy(alpha = 0.8f), Offset(0f, y(thr.toFloat())), Offset(w, y(thr.toFloat())),
            strokeWidth = 2f
        )
        for (i in 1 until pts.size) {
            val c = if (pts[i].co2 >= thr) Red else Green
            drawLine(
                c,
                Offset(x(i - 1), y(pts[i - 1].co2.toFloat())),
                Offset(x(i), y(pts[i].co2.toFloat())),
                strokeWidth = 2.5f
            )
        }
    }
}
