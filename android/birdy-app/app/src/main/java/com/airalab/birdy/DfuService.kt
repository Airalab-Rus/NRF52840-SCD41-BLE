package com.airalab.birdy

import android.app.Activity
import no.nordicsemi.android.dfu.DfuBaseService

/** Сервис OTA-обновления (нужен библиотеке DFU). */
class DfuService : DfuBaseService() {
    override fun getNotificationTarget(): Class<out Activity> = MainActivity::class.java
}
