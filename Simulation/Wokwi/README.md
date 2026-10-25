# SAATHI chair: Wokwi demo

Wokwi Simulation Link: https://wokwi.com/projects/478814715951206401

The same firmware as `../saathi_chair`, built with `demo_mode.h` so the sensors are simulated and SMS and voice
prompts are printed to the serial monitor. Buttons are real.

**Run in VS Code:** install the Wokwi extension, open this folder, press F1 → "Wokwi: Start Simulator".
It uses the prebuilt `build/wokwi.ino.bin` (ESP32, Arduino core 3.0.7, DIO flash).

**Run on wokwi.com:** create an ESP32 project, upload every `.h`/`.cpp` file and `wokwi.ino` from this folder,
replace `diagram.json` and `libraries.txt` with these, and press Play.

**What to try (serial monitor, 115200):**

1. Type `demo-days 19` to fast-forward through the 14-day learning period and 5 normal days.
2. Press **GREEN**: the chair speaks (printed), measures, then asks 3 questions. Answer with GREEN (yes) or RED (no).
3. Keep pressing GREEN for days 20–24: the simulated patient starts retaining fluid on day 20 and the chair
   moves from NORMAL to WATCH to ALERT, with the reason and the SMS text shown.
4. Press **SOS** at any time.

Other commands: `help`, `status`, `dump` (prints the stored history as CSV), `erase`.

Not yet tested inside Wokwi itself; the same binary was run in Espressif's ESP32 emulator (QEMU),
log in `../qemu_demo_log.txt`.
