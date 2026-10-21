# Medibot: Software Guide (do these steps in order)

Everything in this zip:

| Folder / file | What it is |
|---|---|
| `firmware/medibot/medibot.ino` | Complete ESP32 firmware (one file, **no extra libraries**) |
| `medibot-app/` | Android Studio project (Kotlin + Jetpack Compose) |
| `MEDIBOT_TESTING_GUIDE.md` | Calibration + test procedure + how to fill the report table |

The firmware uses exactly the pin map from `MEDIBOT_WIRING_AND_POWERUP.md`. Do the **hardware power-up first** (that guide, sections 5, steps 1-4), then come back here.

---

## Part A: ESP32 firmware

### A1. Install the tools (once)
1. Install **Arduino IDE 2.x** from arduino.cc.
2. *File > Preferences > Additional boards manager URLs*, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. *Tools > Board > Boards Manager*, search **esp32** (by Espressif), install. Version 2.0.17 or 3.x both work.
4. *Tools > Board > ESP32 Arduino > **ESP32 Dev Module***. Pick the right COM port under *Tools > Port*.

No library installation is needed. LCD and MPU6050 are handled inside the sketch.

### A2. Edit the settings at the top of `medibot.ino`
```cpp
const char* WIFI_SSID  = "YOUR_WIFI_NAME";      // 2.4 GHz only (ESP32 has no 5 GHz)
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* NTFY_TOPIC = "YOUR_NTFY_TOPIC"; // make it unique, anyone who knows it can read alerts
```
Tips:
- A **phone hotspot** is fine (set it to 2.4 GHz). A college Wi-Fi that needs a login page will NOT work, so use a hotspot or a home router.
- Time zone is already India (`IST-5:30`).

### A3. Upload
1. Battery switch **OFF**, USB cable connected (never both at once).
2. Open `firmware/medibot/medibot.ino` (keep the folder name `medibot`), click **Upload**.
3. If it says *Connecting......*, hold the **BOOT** button on the ESP32 until upload starts.
4. Open *Serial Monitor* at **115200 baud**.

### A4. What you should see on first boot
1. LCD: `Medibot / Starting...`, then `Calibrating gyro / Keep robot still`
2. LCD: `Connecting WiFi`, then **`Robot IP address`**, e.g. `192.168.1.42`. **Write this IP down.** You need it in the app.
3. One beep, then the LCD shows the time and `No dose set`.

If Wi-Fi fails after 20 s the robot starts its own network **`Medibot` (password `medibot123`)** at IP **192.168.4.1** and keeps retrying your Wi-Fi. In that mode the app sends your phone's time to the robot, so the schedule still works (but there are no phone alerts without internet).

### A5. Test the electronics (USB connected, battery OFF)
Type a letter in the Serial Monitor and press Enter:

| Key | Test | Good result |
|---|---|---|
| `l` | LCD | "LCD test OK" on screen, address printed |
| `b` | Buzzer | 3 beeps |
| `y` | Button | "button PRESSED" when you press it |
| `u` | Ultrasonic | distance changes when you move your hand; **999 = no echo** (check wiring) |
| `g` | Gyro | rotate robot by hand; right turn gives **positive** degrees |
| `e` | Speed sensors | turn each wheel by hand, both counts rise |
| `i` | Info | IP, time, state |
| `h` | Menu | list of tests |

> **Important:** the `m` (motor) test needs the battery ON, and the wiring guide says never to connect USB and the battery together. So test the motors **wirelessly**: unplug USB, switch the battery ON, wheels lifted, then use the app **Drive** tab (or its **Motor test** button), or the web page. Do not run the serial `m` test while the battery is connected.

### A6. Calibrate (details in `MEDIBOT_TESTING_GUIDE.md`)
Edit these constants near the top of the sketch, re-upload, repeat:
- `LEFT_INVERT` / `RIGHT_INVERT`: if a whole side runs backwards.
- `CM_PER_PULSE`: set so a 100 cm test reads 100 cm.
- `GYRO_SIGN`: flip (-1 to +1) if turns go the wrong way.
- `DRIVE_PWM`: raise if the robot is too slow (motors only get about 5 V).
- `ROUTE[]`: your real distances and turns to the patient's room.

---

## Part B: Android app

### B1. Install Android Studio
Download Android Studio (latest stable) from developer.android.com/studio. First launch installs the SDK. You need internet for the first build.

### B2. Open and build
1. *File > Open*, choose the **`medibot-app`** folder.
2. Wait for **Gradle sync** to finish (a few minutes the first time). If Studio offers to *update the Android Gradle Plugin*, you can accept or ignore it. If sync complains about the SDK, click the link it shows to install *Android SDK Platform 34*.
3. **Install on your phone** (two ways):
   - **USB:** on the phone enable *Developer options > USB debugging*, plug in, pick the phone at the top of Studio, press the green **Run** button.
   - **APK file:** *Build > Build Bundle(s) / APK(s) > Build APK(s)*. The file is `app/build/outputs/apk/debug/app-debug.apk`. Send it to your phone and install (allow "unknown sources").

### B3. Connect the app to the robot
1. Phone and robot must be on the **same Wi-Fi** (or the phone joins the `Medibot` network).
2. Open the app, **Home** tab, type the robot's IP (from the LCD), press **Save & connect**.
3. The status card turns green and shows the robot's state. The LCD mirror shows what the real LCD shows.

> If you use the robot's own `Medibot` network, switch **mobile data off** on the phone. Otherwise Android may send app traffic through mobile data and never reach the robot.

### B4. App pages
| Page | What it does |
|---|---|
| **Home** | Live status (polled every 2 s), LCD mirror, next dose, distance, **Deliver now**, **Stop**, robot IP |
| **Schedule** | 4 time slots with switches. Tap time, set it, tap **Save schedule to robot** |
| **History** | Dose log: Taken / Taken late / Missed (last 20, stored in robot flash) |
| **Drive** | Hold arrows to drive (robot stops by itself when you release or the link drops), speed slider, STOP, buzzer test, motor test |

### B5. Phone alerts when the app is closed (ntfy)
1. Install the **ntfy** app (Play Store).
2. Tap **+**, subscribe to the exact topic you put in `NTFY_TOPIC`.
3. Test from any browser: open `https://ntfy.sh/YOUR_TOPIC` and send a message, or run a delivery and don't press the button for 2 minutes.

---

## Part C: Web page (no app needed)
Open `http://ROBOT_IP/` in any phone/PC browser on the same Wi-Fi. It has the same Home, Schedule, Drive and History features. Good backup if the app misbehaves during your demo.

---

## Part D: Full-run demo checklist
1. Robot at home position, facing the same direction every time, wheels on the floor, battery charged.
2. Set the next slot **2 minutes from now** in the app, Save.
3. LCD: `Next dose HH:MM`. At that minute the robot calibrates the gyro (keep it still, about 2 s), then drives the route.
4. Put your hand in front of it: it stops, LCD `Obstacle ahead`, and continues when you remove it.
5. At the room: buzzer beeps, LCD `Take your / medicine`. Press the button: `Thank you!`, dose logged **Taken**.
6. Repeat and do **not** press for 2 min: faster beeps, `MISSED DOSE!`, app turns red, phone gets the ntfy alert. Press the button later: logged **Taken late**.
7. Robot turns 180, drives the route in reverse, turns 180 and is home.

Press **Stop** (app) or `s` (serial) any time to abort. The robot stays where it is; drive it home from the Drive tab.

---

## HTTP API (for the report, section 7.2)
| Request | Purpose |
|---|---|
| `GET /` | Built-in web page |
| `GET /api/state` | JSON: status, LCD text, next dose, schedule, history, distance |
| `POST /save` | Save dose times. Form fields `t0..t3` (`HH:MM`) and `e0..e3` (`1`/`0`) |
| `GET /test` | Start a delivery now |
| `GET /api/drive?d=F/B/L/R/S&sp=170` | Manual drive (auto-stops after 0.5 s without commands) |
| `GET /api/beep` | Test the buzzer |
| `GET /api/stop` | Abort delivery / stop motors |
| `GET /api/motortest` | Run the motor test (wheels lifted) |
| `GET /api/clearhistory` | Erase dose history |
| `GET /api/settime?epoch=N` | Set the clock (only used if there is no internet time) |

---

## Troubleshooting
| Problem | Fix |
|---|---|
| Upload fails | Hold BOOT while "Connecting...", try another USB cable (data cable), check port |
| LCD shows IP `0.0.0.0` or time `--:--:--` | Wrong Wi-Fi name/password, or 5 GHz network. Use 2.4 GHz hotspot |
| LCD blank | Contrast screw on backpack, check level converter. Serial prints `[LCD] not found` if I2C address is missing |
| App says "Cannot reach robot" | Wrong IP, different Wi-Fi, mobile data on (when using the Medibot network), or robot not powered |
| Robot goes straight but turns are wrong direction | Flip `GYRO_SIGN` |
| Robot spins in place instead of going forward | One side inverted: set `LEFT_INVERT` or `RIGHT_INVERT` |
| Stops after 4 s with "stuck / speed sensors" alert | Speed sensor wiring (3V3, OUT on 34/35, disc in slot) or wheels blocked |
| Always "Obstacle ahead" | Ultrasonic echo divider wrong, or something close to the sensor. Check with test `u` |
| No missed-dose phone alert | Robot not connected to internet Wi-Fi, topic mismatch, or ntfy app notifications blocked |
| ESP32 resets when motors start | 470 uF capacitor, charged battery, thicker wires (wiring guide section 6) |

## Honest notes
- Route driving is **dead reckoning** (wheel pulses + gyro). It is repeatable on a smooth floor but drifts on long routes. Always start from the same spot and direction.
- The firmware could not be tested on your hardware here, so expect to spend time on the calibration step. Everything is a clearly named constant at the top of `medibot.ino`.
- Prototype only; not for real medication.
