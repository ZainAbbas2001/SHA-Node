# SHA-Node Firmware

ESP32-S3 edge audio recorder with store-and-forward telemetry over MQTT/TLS.

Built with **ESP-IDF v5.3**, **FreeRTOS**, **C99**.

---

## How it works

SHA-Node records audio from a PDM microphone, buffers telemetry records to SD card, and forwards them to an MQTT broker over Wi-Fi/TLS when a connection is available. A power state machine (FSM) governs the entire lifecycle — from recording through sync to deep sleep — with battery-level safety interlocks at each transition.

### Boot sequence (`app_main`)

```
rtc_state_load()          ← restore boot_count + wake_reason from RTC memory
power_rails_init()        ← configure rail-switch GPIOs, drive all LOW
nvs_flash_init()          ← erase on corruption
haptic_init()             ← must precede any safety_emergency_flush() call
fuel_gauge_init()         ← non-fatal; SOC=0 on failure (most-conservative path)
sd_storage_init()         ← non-fatal; telemetry buffering disabled if absent
wifi_manager_init()       ← starts async connect loop
mqtt_client_init()        ← starts async MQTT loop
power_fsm_init()          ← creates audio EventGroup
mic_dma_init()            ← must precede task_audio_start()
task_audio_start()        ← pins to core 1
task_telemetry_start()    ← pins to core 0
power_fsm_run()           ← never returns
```

### Power FSM states

```
BOOT ──────────────────────────────────────────────────────────────┐
  │  SOC ≥ 15% (or USB present)                                    │
  ▼                                                                 │
ACTIVE_RECORD   ← triggers audio task, records for 10 s           │ SOC < 15%
  │  recording done                                                 │
  ▼                                                                 │
ACTIVE_SYNC     ← store_forward drains SD queue to MQTT           │
  │  sync done                                                      │
  ▼                                                                 │
LOW_POWER_FLUSH ← safety_emergency_flush() flushes haptic, SD    ◄─┘
  │
  ▼
DEEP_SLEEP      ← power_rails_all_off, rtc_state_save, 30 s sleep
```

### Component layers

```
main  ──────►  tasks  ──────►  net  ──────►  core  ──────►  drivers
                                              (safety, FSM, RTC)
```

All compile-time constants (GPIOs, thresholds, stack sizes, timeouts) live in a single header: [include/sha_config.h](include/sha_config.h).

---

## Hardware

| Peripheral | Interface | GPIOs |
|---|---|---|
| Fuel gauge (MAX17048) | I2C0 400 kHz | SDA=8, SCL=9 |
| PDM microphone | I2S0 | DATA=4, CLK=5 |
| SD card | SPI2 | MOSI=11, MISO=13, CLK=12, CS=10 |
| Haptic motor | LEDC ch0 200 Hz | GPIO 6 |
| Rail: mic power | GPIO active-HIGH | GPIO 15 |
| Rail: SD power | GPIO active-HIGH | GPIO 16 |
| Rail: haptic power | GPIO active-HIGH | GPIO 17 |
| Rail: modem power | GPIO active-HIGH | GPIO 18 |
| USB VBUS sense | GPIO input | GPIO 21 |

### Safety interlocks

| Check | Threshold | Effect |
|---|---|---|
| OTA allowed | SOC ≥ 30% or USB present | blocks OTA below threshold |
| Haptic allowed | SOC ≥ 15% or USB present | disables haptic permanently this boot |
| Recording allowed | SOC ≥ 15% or USB present | FSM skips to LOW_POWER_FLUSH |

---

## Prerequisites

- [ESP-IDF v5.3](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/get-started/index.html) installed and `IDF_PATH` set
- CMake ≥ 3.16, Python ≥ 3.9
- (Optional) [Wokwi for VS Code](https://docs.wokwi.com/vscode/getting-started) for simulation

---

## Configuration

Before flashing, set your Wi-Fi credentials and MQTT broker in [include/sha_config.h](include/sha_config.h):

```c
#define WIFI_DEFAULT_SSID    "your-ssid-here"
#define WIFI_DEFAULT_PASS    "your-password-here"
#define MQTT_BROKER_URI      "mqtts://your-broker.example.com:8883"
```

Replace the placeholder CA certificate in [net/include/mqtt_client.h](net/include/mqtt_client.h):

```c
#define MQTT_CA_CERT_PEM "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
```

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
```

### Flash and monitor

```powershell
idf.py flash monitor
```

### Flash size and partitions

Target flash: **8 MB**. Custom partition table at [partitions.csv](partitions.csv):

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24 KB |
| otadata | data/ota | 0xF000 | 8 KB |
| factory | app/factory | 0x20000 | 1.5 MB |
| ota_0 | app/ota_0 | 0x1A0000 | 1.5 MB |
| ota_1 | app/ota_1 | 0x320000 | 1.5 MB |
| littlefs | data/littlefs | 0x4A0000 | 3.375 MB |

### Real hardware: re-enable PSRAM

`sdkconfig.defaults` disables PSRAM so the Wokwi simulation works (the emulated ESP32-S3 has no PSRAM chip). Re-enable for real hardware:

```powershell
idf.py menuconfig
# Component config → ESP PSRAM → Support for external, SPI-connected RAM
```

---

## Wokwi simulation

Open the project folder in VS Code with the Wokwi extension installed, then press **F1 → Wokwi: Start Simulator** (or use the play button in the Wokwi panel). The simulation uses [wokwi.toml](wokwi.toml) which references `build/flasher_args.json` — run `idf.py build` first.

The [diagram.json](diagram.json) wires up:

- ESP32-S3 DevKitC-1
- MicroSD card on SPI2
- I2C pull-ups on GPIO 8/9 (4.7 kΩ)
- LEDs on each power rail GPIO (GPIO 15–18) and haptic (GPIO 6)
- Push button + pull-down on GPIO 21 (VBUS sense)

**Expected simulation behaviour:** fuel gauge and SD card are absent in simulation, so the FSM enters `LOW_POWER_FLUSH → DEEP_SLEEP` immediately on every boot (SOC defaults to 0%, below the 15% recording threshold). To exercise the `ACTIVE_RECORD` path, press the **VBUS button** during boot — this sets `usb_connected=true` which bypasses the SOC gate.

---

## Testing

### 1. Unity tests on hardware

The `test/` component compiles `core/safety.c`, `core/power_fsm.c`, and `core/rtc_state.c` directly alongside hardware stubs. 23 test cases cover safety interlocks, FSM init, and EventGroup behaviour.

```powershell
idf.py -C test set-target esp32s3
idf.py -C test flash monitor
```

### 2. Static analysis

```powershell
cppcheck --enable=all --suppress=missingInclude core/ drivers/
```

Or with the ESP-IDF clang-check integration (ESP-IDF ≥ 5.2):

```powershell
idf.py clang-check
```

### 3. Host target (no hardware needed)

ESP-IDF v5.x supports a `linux` target. Compile and run `test/` on your PC with AddressSanitizer:

```powershell
idf.py --preview set-target linux
idf.py -C test build
./test/build/sha_node_test.elf
```

I2S/LEDC stubs become no-ops; GPIO state is controlled via `stub_gpio_set_level()` as in the hardware test.

---

## Project structure

```
├── include/              sha_config.h — single source of truth for all constants
├── core/                 safety.c, power_fsm.c, rtc_state.c
├── drivers/              fuel_gauge.c, mic_dma.c, sd_storage.c, haptic.c, power_rails.c
├── net/                  wifi_manager.c, mqtt_client.c, store_forward.c
│   └── include/          mqtt_client.h (wraps ESP-IDF header via #include_next)
├── tasks/                task_audio.c, task_telemetry.c
├── main/                 app_main() entry point
├── test/                 Unity test component + hardware stubs
├── docs/                 architecture.md — Mermaid diagrams, GPIO table, task map
├── partitions.csv        custom 8 MB partition layout
├── sdkconfig.defaults    baseline Kconfig (PSRAM disabled for Wokwi)
├── diagram.json          Wokwi circuit diagram
└── wokwi.toml            Wokwi simulator entry point
```

---

## Architecture docs

Full component dependency graph, FSM state diagram, audio recording sequence, and telemetry data path are in [docs/architecture.md](docs/architecture.md).
