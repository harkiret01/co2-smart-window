# CO₂ Smart Window: Indoor CO₂ Monitoring with IoT Ventilation Control

An ESP32-based device that monitors indoor CO₂, texts you when the air gets stale, and lets you open a window from your phone.

*EE 198B Senior Design Project, San José State University (Fall 2022)*

<p align="center">
  <img src="docs/images/prototype.jpg" alt="Prototype: protoboard with ESP32, SCD40 and A4988 driving a lead-screw window model" width="640">
</p>

<p align="center">
  <a href="https://youtu.be/F_ZmzqogyNA"><b>▶ Watch the working demo</b></a>
</p>

---

## Why

Research shows that indoor CO₂ concentration is a cheap, effective proxy for COVID-19 transmission risk: exhaled CO₂ builds up in poorly ventilated rooms, and so do airborne particles ([Peng & Jimenez, 2021](https://pubs.acs.org/doi/10.1021/acs.estlett.1c00183)). Around **2,000 ppm**, CO₂ also starts to impair cognitive function. We wanted a low-cost device that **measures** CO₂, **alerts** people when it's too high, and gives them a one-tap way to **fix it** by opening a window.

## How it works

1. A **Sensirion SCD40** sensor reports CO₂ (plus temperature and humidity) to an **ESP32** over I²C every 5 seconds.
2. When CO₂ goes above **2,000 ppm**, the ESP32 sends an **SMS through Twilio**. The text includes a link (the ESP32's local IP address) to a small web dashboard.
3. The dashboard, served by an **asynchronous web server** on the ESP32 so several people in a household can use it at once, shows the live reading and has **Open Window / Close Window** toggles.
4. Flipping a toggle makes the ESP32 drive an **A4988 stepper driver**, which turns a **NEMA 17 stepper motor** on a lead screw to slide the window open or closed.

| SMS alert | Web dashboard |
|:---:|:---:|
| <img src="docs/images/sms-alert.png" width="260"> | <img src="docs/images/webpage.png" width="360"> |

### System architecture

<p align="center">
  <img src="docs/images/block-diagram.png" alt="Block diagram" width="220">
  &nbsp;&nbsp;&nbsp;
  <img src="docs/images/program-flow.png" alt="Program flow" width="560">
</p>

The firmware loops between reading CO₂ and checking the webpage buttons. It only branches out to send an alert or move the motor when something happens.

## Hardware

| Component | Role | Why we chose it |
|---|---|---|
| **ESP32-DevKitC** | Microcontroller | Built-in WiFi for IoT; onboard LDO regulates 5 V → 3.3 V |
| **Sensirion SCD40** | CO₂ sensor | ±40 ppm accuracy, I²C, 2.4–5.5 V supply, small footprint. I²C also makes it easy to add more air-quality sensors later |
| **A4988** | Stepper driver | Cheap, 5 microstepping modes, adjustable current limit, over-current protection |
| **NEMA 17 stepper + lead screw** | Window actuator | Precise positioning and high torque (the same reasons 3D printers and standing desks use them) |
| **5 V / 3 A AC-DC adapter** | Power | Covers the ~1.5 A combined peak of all parts from a single supply |
| **100 µF electrolytic cap** | VMOT decoupling | Protects the A4988 from LC voltage spikes above its 35 V rating |

### Wiring

| From | To |
|---|---|
| SCD40 SCL | ESP32 GPIO22 (physical pin 36) |
| SCD40 SDA | ESP32 GPIO21 (physical pin 33) |
| SCD40 VDD / GND | 5 V / GND |
| A4988 STEP | ESP32 GPIO2 (physical pin 24) |
| A4988 DIR | ESP32 GPIO15 (physical pin 23) |
| A4988 SLEEP ↔ RESET | tied together |
| A4988 VDD, VMOT | 5 V (100 µF cap across VMOT–GND) |
| A4988 1A/1B/2A/2B | Stepper motor coils |

![Schematic](hardware/schematic.png)

See [`hardware/`](hardware/) for the full schematic and a design for a wireless, Li-Po powered version.

### Bill of materials

| Part | Price (USD) |
|---|---:|
| ESP32 microcontroller | $10 |
| SCD40 CO₂ sensor | $40 |
| NEMA 17 stepper motor | $30 |
| A4988 stepper driver | $5 |
| Protoboard | $7 |
| Header pins | $9 |
| AC/DC adapter | $12 |
| **Total (electronics)** | **$113** |
| Woodwork for window model | $25 |

## Getting started

### 1. Install libraries (Arduino IDE)

Install the [ESP32 board package](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html), then add these libraries:

- [Sensirion I2C SCD4x](https://github.com/Sensirion/arduino-i2c-scd4x) (v0.4.x, via the Library Manager)
- [twilio-esp32-client](https://github.com/ademuri/twilio-esp32-client), which provides `twilio.hpp`
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

> Prefer PlatformIO? `firmware/platformio.ini` is set up already, so `cd firmware && pio run -t upload`.

### 2. Add your credentials

```bash
cp firmware/co2_monitor/secrets.example.h firmware/co2_monitor/secrets.h
```

Fill in your WiFi SSID/password and your Twilio Account SID, Auth Token, Twilio number, and destination number. `secrets.h` is git-ignored.

> On a Twilio **trial** account, you can only text numbers you've verified in the console.

### 3. Flash and run

Open `firmware/co2_monitor/co2_monitor.ino`, select **ESP32 Dev Module**, and upload. Open the Serial Monitor at **115200 baud** to see the ESP32's IP address, then visit it from any device on the same network.

### Configuration

All in `co2_monitor.ino`:

| Constant | Default | Meaning |
|---|---|---|
| `co2_threshold` | `2000` | ppm level that triggers an SMS |
| `WINDOW_TRAVEL_STEPS` | `5250` | Steps between fully closed and fully open (tuned to our model) |
| `STEP_DELAY_US` | `900` | Half-period of each step pulse; larger is slower and gentler |

## Repository layout

```
├── firmware/
│   ├── co2_monitor/
│   │   ├── co2_monitor.ino      # Main sketch
│   │   └── secrets.example.h    # Template for WiFi + Twilio credentials
│   └── platformio.ini           # Optional PlatformIO build config
├── hardware/
│   ├── schematic.png            # Altium schematic
│   └── alt-lipo-power.png       # Alternative battery-powered design
└── docs/
    ├── REPORT.md                # Project write-up
    └── images/
```

## Results

- ✅ Accurate CO₂ readings from the SCD40
- ✅ SMS alerts via Twilio when CO₂ exceeds the threshold
- ✅ Live web dashboard hosted on the ESP32
- ✅ Remote open/close of a sliding window via stepper motor
- ✅ Whole circuit soldered onto a single protoboard
- ⚠️ Cost came to ~$113 for electronics, more than the mass-market price we were aiming for

## Future improvements

- Let the user choose *how far* to open the window (partial positions)
- Support other window types (e.g. swinging/casement)
- Add a local LCD so readings can be checked without a phone
- Improve the dashboard UI
- Battery-powered wireless version (3.7 V Li-Po + 5 V boost + reverse-polarity protection; see [`hardware/alt-lipo-power.png`](hardware/alt-lipo-power.png))
- Rate-limit SMS alerts (currently re-sends roughly every 15 s while CO₂ stays high)
- Use a cheaper CO₂ sensor and a higher-current driver for real, heavier windows

## Team

**Harkiret Singh** · **Sahil Shah** · **Christopher Lim** · **David Gomez**
Advisor: Dr. Sotoudeh Hamedi-Hagh, San José State University

## Acknowledgments

- Webpage HTML/JS adapted from [Rui Santos, Random Nerd Tutorials](https://randomnerdtutorials.com)
- [Pololu A4988 documentation](https://www.pololu.com/product/1182)
- Z. Peng and J. L. Jimenez, "Exhaled CO₂ as a COVID-19 infection risk proxy for different indoor environments and activities," *Environ. Sci. Technol. Lett.*, 8(5), 392–397, 2021.

## License

[MIT](LICENSE)
