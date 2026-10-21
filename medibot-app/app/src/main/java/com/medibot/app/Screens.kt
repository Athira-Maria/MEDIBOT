package com.medibot.app

import android.app.TimePickerDialog
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// ---------------------------------------------------------------- HOME
@Composable
fun HomeScreen(vm: RobotViewModel) {
    val s = vm.state
    var ip by remember { mutableStateOf(vm.host) }

    val bg = when {
        !s.online -> Color(0xFFE0E0E0)
        s.alert -> Color(0xFFFFCDD2)
        s.state == "IDLE" -> Color(0xFFC8E6C9)
        else -> Color(0xFFFFF3C4)
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("Medibot", style = MaterialTheme.typography.headlineMedium)

        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = bg)
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(s.status, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                Text("State: ${s.state}")
                if (s.alert) {
                    Text("Patient has NOT confirmed the dose!", color = Color(0xFFB71C1C), fontWeight = FontWeight.Bold)
                }
            }
        }

        // mirror of the robot's LCD
        Box(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(Color(0xFF0D1B0D))
                .padding(14.dp)
        ) {
            Column {
                Text(s.line1.ifEmpty { " " }, color = Color(0xFF7CFC00), fontFamily = FontFamily.Monospace, fontSize = 20.sp)
                Text(s.line2.ifEmpty { " " }, color = Color(0xFF7CFC00), fontFamily = FontFamily.Monospace, fontSize = 20.sp)
            }
        }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Robot time: ${s.time}" + if (s.synced) "" else "  (not synced)")
                Text("Next dose: ${s.next}")
                Text("Distance ahead: ${s.distance} cm")
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = { vm.deliverNow() }, enabled = s.online) { Text("Deliver now") }
            Button(
                onClick = { vm.stopRobot() },
                enabled = s.online,
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFC62828))
            ) { Text("Stop") }
        }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Robot connection", fontWeight = FontWeight.Bold)
                Text("Enter the IP address shown on the robot's LCD at start-up (or 192.168.4.1 if you joined the 'Medibot' Wi-Fi).")
                OutlinedTextField(
                    value = ip,
                    onValueChange = { ip = it },
                    label = { Text("Robot IP address") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Button(onClick = { vm.updateHost(ip) }) { Text("Save & connect") }
            }
        }
    }
}

// ---------------------------------------------------------------- SCHEDULE
@Composable
fun ScheduleScreen(vm: RobotViewModel) {
    val ctx = LocalContext.current
    var edit by remember { mutableStateOf<List<Slot>?>(null) }
    val slots = edit ?: vm.state.slots

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("Dose schedule", style = MaterialTheme.typography.headlineMedium)
        Text("Tap a time to change it, use the switch to turn a slot on or off.")

        slots.forEachIndexed { i, slot ->
            Card(Modifier.fillMaxWidth()) {
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Slot ${i + 1}", style = MaterialTheme.typography.titleMedium)
                    OutlinedButton(onClick = {
                        val parts = slot.time.split(":")
                        val h = parts.getOrNull(0)?.toIntOrNull() ?: 8
                        val m = parts.getOrNull(1)?.toIntOrNull() ?: 0
                        TimePickerDialog(ctx, { _, hh, mm ->
                            val list = slots.toMutableList()
                            list[i] = slot.copy(time = "%02d:%02d".format(hh, mm))
                            edit = list
                        }, h, m, true).show()
                    }) { Text(slot.time) }
                    Switch(
                        checked = slot.enabled,
                        onCheckedChange = { checked ->
                            val list = slots.toMutableList()
                            list[i] = slot.copy(enabled = checked)
                            edit = list
                        }
                    )
                }
            }
        }

        Button(
            onClick = {
                vm.saveSchedule(slots)
                edit = null
            },
            enabled = vm.state.online,
            modifier = Modifier.fillMaxWidth()
        ) { Text("Save schedule to robot") }
    }
}

// ---------------------------------------------------------------- HISTORY
@Composable
fun HistoryScreen(vm: RobotViewModel) {
    val rows = vm.state.history.reversed()

    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text("Dose history", style = MaterialTheme.typography.headlineMedium)
            OutlinedButton(onClick = { vm.clearHistory() }, enabled = vm.state.online) { Text("Clear") }
        }
        if (rows.isEmpty()) {
            Text("No doses recorded yet.")
        }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(rows) { h ->
                val color = when (h.status) {
                    "taken" -> Color(0xFF2E7D32)
                    "taken_late" -> Color(0xFFEF6C00)
                    else -> Color(0xFFC62828)
                }
                val label = when (h.status) {
                    "taken" -> "Taken"
                    "taken_late" -> "Taken late"
                    "missed" -> "Missed"
                    else -> h.status
                }
                Card(Modifier.fillMaxWidth()) {
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(h.time)
                        Text(label, color = color, fontWeight = FontWeight.Bold)
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- DRIVE
@Composable
fun DriveButton(label: String, dir: String, vm: RobotViewModel) {
    Box(
        modifier = Modifier
            .size(88.dp)
            .clip(RoundedCornerShape(20.dp))
            .background(MaterialTheme.colorScheme.primary)
            .pointerInput(dir) {
                detectTapGestures(onPress = {
                    vm.startDrive(dir)
                    try {
                        tryAwaitRelease()
                    } finally {
                        vm.stopDrive()
                    }
                })
            },
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = MaterialTheme.colorScheme.onPrimary, fontSize = 30.sp)
    }
}

@Composable
fun DriveScreen(vm: RobotViewModel) {
    Column(
        Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text("Manual drive", style = MaterialTheme.typography.headlineMedium)
        Text("Hold an arrow to move. The robot stops when you let go.")
        Text("Speed: ${vm.speed.toInt()}")
        Slider(
            value = vm.speed,
            onValueChange = { vm.speed = it },
            valueRange = 90f..255f
        )

        DriveButton("\u25B2", "F", vm)
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            DriveButton("\u25C0", "L", vm)
            Spacer(Modifier.size(88.dp))
            DriveButton("\u25B6", "R", vm)
        }
        DriveButton("\u25BC", "B", vm)

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(
                onClick = { vm.stopRobot() },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFC62828))
            ) { Text("STOP") }
            OutlinedButton(onClick = { vm.beep() }) { Text("Buzzer test") }
            OutlinedButton(onClick = { vm.motorTest() }) { Text("Motor test") }
        }
        if (!vm.state.online) {
            Text("Robot is offline - check the IP on the Home tab.", color = Color(0xFFC62828))
        }
    }
}
