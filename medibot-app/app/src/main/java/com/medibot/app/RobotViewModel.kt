package com.medibot.app

import android.app.Application
import android.content.Context
import android.widget.Toast
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class RobotViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences("medibot", Context.MODE_PRIVATE)
    private val api = RobotApi(prefs.getString("host", "192.168.4.1") ?: "192.168.4.1")

    var host by mutableStateOf(api.host)
        private set
    var state by mutableStateOf(RobotState())
        private set
    var speed by mutableFloatStateOf(170f)

    private var driveJob: Job? = null
    private var timeSent = false

    init {
        // poll the robot every 2 seconds
        viewModelScope.launch {
            while (true) {
                refresh()
                delay(2000)
            }
        }
    }

    private fun toast(msg: String) {
        Toast.makeText(getApplication<Application>(), msg, Toast.LENGTH_SHORT).show()
    }

    private suspend fun refresh() {
        try {
            val s = api.getState()
            state = s
            // robot has no internet time -> give it the phone's time
            if (!s.synced && !timeSent) {
                timeSent = true
                api.setTime(System.currentTimeMillis() / 1000)
            }
        } catch (e: Exception) {
            state = state.copy(online = false, state = "OFFLINE", status = "Cannot reach robot at ${api.host}")
        }
    }

    private fun act(okMsg: String, block: suspend () -> Unit) {
        viewModelScope.launch {
            try {
                block()
                if (okMsg.isNotEmpty()) toast(okMsg)
                refresh()
            } catch (e: Exception) {
                toast("Failed: ${e.message}")
            }
        }
    }

    fun updateHost(newHost: String) {
        api.setHostAddress(newHost)
        host = api.host
        prefs.edit().putString("host", host).apply()
        timeSent = false
        toast("Saved: $host")
        viewModelScope.launch { refresh() }
    }

    fun deliverNow() = act("Delivery started") { api.deliverNow() }
    fun stopRobot() = act("Stop sent") { api.stop() }
    fun beep() = act("") { api.beep() }
    fun motorTest() = act("Motor test started (wheels lifted!)") { api.motorTest() }
    fun clearHistory() = act("History cleared") { api.clearHistory() }

    fun saveSchedule(slots: List<Slot>) {
        state = state.copy(slots = slots)
        act("Schedule saved") { api.saveSchedule(slots) }
    }

    /** Called while a drive arrow is held down: sends the command every 200 ms. */
    fun startDrive(dir: String) {
        driveJob?.cancel()
        driveJob = viewModelScope.launch {
            while (true) {
                try {
                    api.drive(dir, speed.toInt())
                } catch (e: Exception) {
                    // ignore (robot may be busy)
                }
                delay(200)
            }
        }
    }

    fun stopDrive() {
        driveJob?.cancel()
        driveJob = null
        viewModelScope.launch {
            try {
                api.drive("S", 0)
            } catch (e: Exception) {
                // ignore
            }
        }
    }
}
