# Medibot: Testing and Calibration Guide

Use this to calibrate the robot and to fill the table in section 8 of your report.
Keep a notebook: write every number down as you go.

## 1. Power test (wiring guide, power-up steps 1-3)
Measure with a multimeter: battery rail (6.6-8.4 V) and LM2596 output (5.0 V).
**Report:** `___ V in, ___ V out`

## 2. Motors (4 wheels, direction)
Wheels lifted, USB unplugged, battery ON. In the app: **Drive > Motor test**.
Order: left forward, left backward, right forward, right backward, both forward.
- One wheel wrong: swap that motor's 2 wires.
- Whole side wrong: set `LEFT_INVERT` / `RIGHT_INVERT` to `true`.
Then check the Drive arrows: Up = forward, Left = pivots left, Right = pivots right.
**Report:** "4 of 4 wheels correct" (or what you fixed).

## 3. Distance calibration (100 cm, 3 trials)
1. Serial test `e`: roll the robot **by hand exactly 100 cm** in a straight line, note the counts (left and right). Repeat 3 times and average.
2. `CM_PER_PULSE = 100 / average_count`. Example: 98 pulses gives 1.02. Put it in the sketch and upload.
3. Quick estimate if you have no ruler: wheel circumference / slots in the disc (65 mm wheel, 20 slots gives 20.4 / 20 = 1.02 cm).
4. Test run: set `ROUTE` to `{{'F',100}}`, press **Deliver now** (it will stop at the room, press the button, it comes back). Measure the real distance 3 times.

| Trial | Commanded | Measured | Error % |
|---|---|---|---|
| 1 | 100 | | |
| 2 | 100 | | |
| 3 | 100 | | |

**Report:** average error `___ %` = average of |measured - 100| / 100 x 100.

## 4. Turn calibration (90 degrees, 6 trials)
1. Serial test `g`: rotate by hand to the **right**. Heading must go **positive**. If negative, change `GYRO_SIGN` to `1.0f`.
2. Set `ROUTE` to `{{'T',90}}`. Run a delivery and mark the robot's direction before and after (tape lines on the floor, measure angle with a protractor or phone compass).
3. Turning stalls or is jerky: raise `TURN_MIN_PWM` (try 140-150). Overshoots: lower `TURN_PWM`.

| Trial | Target | Measured | Error (deg) |
|---|---|---|---|
| 1-6 | 90 | | |

**Report:** average error `___ degrees`.

## 5. Obstacle stop
Place a box in front of the robot while it drives. It must stop under 20 cm, show `Obstacle ahead`, and continue when removed. Measure the gap between the robot front and the box when it stops (3 tries).
**Report:** "Stopped at `___ cm` gap".

## 6. LCD, buzzer, button
Serial tests `l`, `b`, `y`, then a full delivery (message, beeps, confirm).
**Report:** "Pass".

## 7. Schedule
Set a slot for 2 minutes ahead. Check the delivery starts in the correct minute (compare to the robot time on the app Home tab).
**Report:** "Starts at the right minute: yes / no".

## 8. Missed-dose alert
Do not press the button. Use a stopwatch from the moment `MISSED DOSE!` appears (2 minutes after arrival) until the ntfy notification reaches the phone.
**Report:** `___ s after timeout`

## 9. App tests (12 tests)
Tick each one that passes:
1. Connects with IP, status turns green
2. LCD mirror matches real LCD
3. Next dose shown
4. Schedule edit saves
5. Schedule still correct after robot reboot
6. Slot switch on/off works
7. Deliver now
8. Stop during a delivery
9. Drive forward / back
10. Drive left / right
11. Buzzer test
12. History shows Taken / Taken late / Missed
**Report:** `___ of 12 tests passed`

## 10. Full run (5 runs)
Schedule, go to room, confirm, return home to the start mark (within about 15 cm and 10 degrees counts as OK).
**Report:** `___ of 5 runs OK`

## Tuning cheat sheet
| Symptom | Change |
|---|---|
| Robot drifts left/right while going straight | One side's motors weaker: lower `KP_HEADING` if it wiggles, raise to 6 if it still drifts |
| Too slow / stalls | raise `DRIVE_PWM` (max 255) |
| Stops too short or too far | recalibrate `CM_PER_PULSE` |
| Return trip ends offset | normal drift; shorten the route or tune the same constants |
| Turns not accurate | check `GYRO_SIGN`, keep robot still during the 2 s start calibration |
