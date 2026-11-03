package com.airalab.birdy

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.util.UUID

/**
 * BLE-клиент «CO2-Birdy».
 *
 * Сервис 0xFFF0:
 *   FFF1 write  — команды (бинарь или текст, прошивка определяет сама)
 *   FFF2 notify — бинарный статус (6 байт)
 *   FFF3 read   — конфиг (14 байт)
 *   FFF4 write  — текстовые команды
 *   FFF5 notify — текстовый ответ
 */
class BirdyBle(private val ctx: Context) {

    companion object {
        private fun uuid16(v: Int): UUID =
            UUID.fromString(String.format("%08x-0000-1000-8000-00805f9b34fb", v))

        val SVC: UUID = uuid16(0xFFF0)
        val CMD: UUID = uuid16(0xFFF1)
        val STAT: UUID = uuid16(0xFFF2)
        val INFO: UUID = uuid16(0xFFF3)
        val TCMD: UUID = uuid16(0xFFF4)
        val TSTAT: UUID = uuid16(0xFFF5)
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    data class Found(val address: String, val name: String, val rssi: Int) {
        val isBirdy: Boolean get() = name.contains("birdy", ignoreCase = true)
    }

    var onDevices: ((List<Found>) -> Unit)? = null
    var onConnected: (() -> Unit)? = null
    var onDisconnected: (() -> Unit)? = null
    var onInfo: ((ByteArray) -> Unit)? = null
    var onText: ((String) -> Unit)? = null
    var onRssi: ((Int) -> Unit)? = null
    var onError: ((String) -> Unit)? = null

    private val manager = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter get() = manager.adapter

    private val found = LinkedHashMap<String, Found>()
    private var scanning = false

    private var gatt: BluetoothGatt? = null
    private var cmdChr: BluetoothGattCharacteristic? = null
    private var txtChr: BluetoothGattCharacteristic? = null
    private var infoChr: BluetoothGattCharacteristic? = null

    /* ------------------------------- разрешения ------------------------------- */

    fun permissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                android.Manifest.permission.BLUETOOTH_SCAN,
                android.Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        }

    fun hasPermissions(): Boolean =
        permissions().all { ctx.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    fun bluetoothOn(): Boolean = try {
        adapter?.isEnabled == true
    } catch (e: SecurityException) {
        false
    }

    /* ---------------------------------- скан ---------------------------------- */

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.scanRecord?.deviceName ?: ""
            if (name.isBlank()) return
            found[result.device.address] = Found(result.device.address, name, result.rssi)
            publish()
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES, it) }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            onError?.invoke("Скан не запустился (код $errorCode)")
        }
    }

    private fun publish() {
        val list = found.values.sortedWith(
            compareByDescending<Found> { it.isBirdy }.thenByDescending { it.rssi }
        )
        onDevices?.invoke(list)
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!hasPermissions()) return
        val a = adapter ?: run { onError?.invoke("Bluetooth не поддерживается"); return }
        if (!a.isEnabled) {
            onError?.invoke("Включите Bluetooth")
            return
        }
        if (scanning) return
        val scanner = a.bluetoothLeScanner ?: return
        scanning = true
        scanner.startScan(
            null,
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
            scanCallback
        )
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            /* игнор */
        }
    }

    fun restartScan() {
        stopScan()
        startScan()
    }

    fun isScanning(): Boolean = scanning

    /* ------------------------------- подключение ------------------------------- */

    @SuppressLint("MissingPermission")
    fun connect(address: String) {
        stopScan()
        val a = adapter ?: return
        val dev: BluetoothDevice = try {
            a.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            onError?.invoke("Неверный адрес устройства")
            return
        }
        gatt?.close()
        gatt = dev.connectGatt(ctx, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: SecurityException) {
            /* игнор */
        }
        gatt = null
        cmdChr = null
        txtChr = null
        infoChr = null
    }

    fun close() {
        stopScan()
        disconnect()
    }

    private val gattCallback = object : BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                gatt = null
                onDisconnected?.invoke()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                onError?.invoke("Не удалось получить сервисы (код $status)")
                return
            }
            val svc = g.getService(SVC)
            if (svc == null) {
                onError?.invoke("Это не Birdy: сервис FFF0 не найден")
                g.disconnect()
                return
            }
            cmdChr = svc.getCharacteristic(CMD)
            txtChr = svc.getCharacteristic(TCMD)
            infoChr = svc.getCharacteristic(INFO)
            svc.getCharacteristic(STAT)?.let { enableNotify(g, it) }
            svc.getCharacteristic(TSTAT)?.let { enableNotify(g, it) }
            onConnected?.invoke()
            readInfo()
            g.readRemoteRssi()
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handleValue(ch, value)
        }

        @Deprecated("API < 33")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            val v = ch.value
            if (v != null) handleValue(ch, v)
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) handleValue(ch, value)
        }

        @Deprecated("API < 33")
        @Suppress("DEPRECATION")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            ch: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val v = ch.value
                if (v != null) handleValue(ch, v)
            }
        }

        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) onRssi?.invoke(rssi)
        }
    }

    private fun handleValue(ch: BluetoothGattCharacteristic, v: ByteArray) {
        when (ch.uuid) {
            INFO -> onInfo?.invoke(v)
            TSTAT -> onText?.invoke(String(v, Charsets.UTF_8).trim())
            else -> { /* бинарный STAT игнорируем: info перечитывается отдельно */ }
        }
    }

    @SuppressLint("MissingPermission")
    private fun enableNotify(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
        g.setCharacteristicNotification(ch, true)
        val d = ch.getDescriptor(CCCD) ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            @Suppress("DEPRECATION")
            d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            g.writeDescriptor(d)
        }
    }

    @SuppressLint("MissingPermission")
    fun readInfo() {
        val g = gatt ?: return
        val ch = infoChr ?: return
        g.readCharacteristic(ch)
    }

    @SuppressLint("MissingPermission")
    fun readRssi() {
        gatt?.readRemoteRssi()
    }

    /** Команда текстом (прошивка принимает текст и в FFF4, и в FFF1). */
    @SuppressLint("MissingPermission")
    fun send(text: String) {
        val g = gatt ?: return
        val ch = txtChr ?: cmdChr ?: run {
            onError?.invoke("Нет характеристики для записи")
            return
        }
        val data = text.toByteArray(Charsets.UTF_8)
        val ok: Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(
                ch, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            run {
                ch.value = data
                ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                g.writeCharacteristic(ch)
            }
        }
        if (!ok) onError?.invoke("Не удалось отправить: $text")
    }
}
