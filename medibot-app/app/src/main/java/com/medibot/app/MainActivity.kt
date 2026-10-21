package com.medibot.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                MedibotApp()
            }
        }
    }
}

@Composable
fun MedibotApp(vm: RobotViewModel = viewModel()) {
    var tab by remember { mutableIntStateOf(0) }
    val names = listOf("Home", "Schedule", "History", "Drive")
    val icons = listOf("\uD83C\uDFE0", "\u23F0", "\uD83D\uDCCB", "\uD83C\uDFAE")

    Scaffold(
        bottomBar = {
            NavigationBar {
                names.forEachIndexed { i, name ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Text(icons[i]) },
                        label = { Text(name) }
                    )
                }
            }
        }
    ) { padding ->
        Box(Modifier.padding(padding)) {
            when (tab) {
                0 -> HomeScreen(vm)
                1 -> ScheduleScreen(vm)
                2 -> HistoryScreen(vm)
                else -> DriveScreen(vm)
            }
        }
    }
}
