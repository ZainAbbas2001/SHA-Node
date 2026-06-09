# SHA-Node Firmware Architecture

ESP32-S3 dual-core (Xtensa LX7), ESP-IDF v5.x, FreeRTOS, C99.

---

## Component Dependency Graph

Arrows point from dependant to dependency.  No circular dependencies exist.

```mermaid
graph TD
    main["main\n(app_main)"]
    tasks["tasks\n(task_audio, task_telemetry)"]
    net["net\n(wifi_manager, mqtt_client,\nstore_forward, ota_manager)"]
    core["core\n(power_fsm, safety, rtc_state)"]
    drivers["drivers\n(fuel_gauge, mic_dma,\nsd_storage, haptic, power_rails)"]
    idf["ESP-IDF\n(FreeRTOS, LEDC, I2S, SPI,\nNVS, OTA, MQTT, TLS)"]

    main --> tasks
    main --> net
    main --> core
    main --> drivers
    tasks --> core
    tasks --> drivers
    net --> core
    net --> drivers
    core --> drivers
    drivers --> idf
    core --> idf
    net --> idf
    tasks --> idf
    main --> idf
```

---

## Power State Machine

```mermaid
stateDiagram-v2
    [*] --> BOOT

    BOOT --> ACTIVE_RECORD : SOC ≥ 15%\nor fuel gauge unavailable
    BOOT --> LOW_POWER_FLUSH : SOC < 15%

    ACTIVE_RECORD --> ACTIVE_RECORD : AUDIO_DONE_BIT not set\n(500 ms poll)
    ACTIVE_RECORD --> ACTIVE_SYNC : AUDIO_DONE_BIT set
    ACTIVE_RECORD --> LOW_POWER_FLUSH : mid-cycle SOC < 15%

    ACTIVE_SYNC --> ACTIVE_SYNC : sync_done not set\n& timeout not elapsed
    ACTIVE_SYNC --> DEEP_SLEEP : sync_done set\nor 30 s timeout
    ACTIVE_SYNC --> LOW_POWER_FLUSH : mid-cycle SOC < 15%

    LOW_POWER_FLUSH --> DEEP_SLEEP : safety_emergency_flush() complete

    DEEP_SLEEP --> [*] : esp_deep_sleep_start()\n(chip resets after 30 s)
```

Entry to `DEEP_SLEEP` always runs:
1. `safety_emergency_flush()` — haptic off → HAPTIC+MODEM rails cut → SD queue flushed
2. `power_rails_all_off()` — all four rails driven LOW
3. `rtc_state_save()` — boot count + pending records persisted to RTC slow memory
4. `esp_deep_sleep_start()` — timer wakeup after `DEEP_SLEEP_DURATION_US` (30 s)

---

## Audio Recording Pipeline

```mermaid
sequenceDiagram
    participant FSM as power_fsm\n(core 0)
    participant Audio as task_audio\n(core 1)
    participant Rail as power_rails
    participant Mic as mic_dma\n(I2S0 PDM)
    participant SD as sd_storage\n(SPI2 FATFS)

    FSM->>Audio: xEventGroupSetBits(AUDIO_START_BIT)
    Audio->>Rail: power_rails_enable(RAIL_MIC)
    Audio->>Audio: vTaskDelay(10 ms settle)
    Audio->>Mic: mic_dma_start()
    loop 10 000 ms recording window
        Mic-->>Audio: ISR posts audio_chunk_t to queue\n(zero-copy DMA pointer)
        Audio->>SD: sd_write_audio_chunk(chunk)
    end
    Audio->>Mic: mic_dma_stop()
    Audio->>Audio: drain tail chunks (20 ms)
    Audio->>Rail: power_rails_disable(RAIL_MIC)
    Audio->>FSM: xEventGroupSetBits(AUDIO_DONE_BIT)
    FSM->>FSM: transition ACTIVE_RECORD → ACTIVE_SYNC
```

---

## Telemetry Data Path

```mermaid
sequenceDiagram
    participant Tel as task_telemetry\n(core 0, 10 s cadence)
    participant MQTT as mqtt_client
    participant SD as sd_storage
    participant SF as store_forward
    participant Broker as MQTT Broker\n(TLS 8883)

    alt Wi-Fi connected
        Tel->>MQTT: mqtt_client_publish(topic, json)
        MQTT-->>Broker: QoS 1 PUBLISH
        Broker-->>MQTT: PUBACK
        MQTT->>SF: xSemaphoreGive(puback_sem)
    else Wi-Fi disconnected
        Tel->>SD: sd_queue_telemetry_record(json)
        Tel->>Tel: g_rtc_state.pending_records++
    end

    note over SF: On Wi-Fi reconnect
    SF->>SD: sd_pop_telemetry_record()
    SD-->>SF: oldest JSON record
    SF->>MQTT: mqtt_client_publish()
    SF->>SF: xSemaphoreTake(puback_sem, 5 s)
    SF->>SF: pending_records-- on success
    SF->>FSM: power_fsm_notify_sync_done()
```

---

## Key Hardware Assignments

| Peripheral | GPIOs | ESP-IDF Driver |
|---|---|---|
| MAX17048 fuel gauge | SDA=8, SCL=9 | I2C master (new v5 API) |
| PDM microphone | DATA=4, CLK=5 | I2S0 RX, ping-pong DMA |
| SD card | MOSI=11, MISO=13, CLK=12, CS=10 | SPI2 + FATFS |
| Haptic motor | GPIO 6 | LEDC ch0, 10-bit duty, `esp_timer` sequencing |
| Power rail MIC | GPIO 15 | GPIO output, active-HIGH |
| Power rail SD | GPIO 16 | GPIO output, active-HIGH |
| Power rail HAPTIC | GPIO 17 | GPIO output, active-HIGH |
| Power rail MODEM | GPIO 18 | GPIO output, active-HIGH |
| USB VBUS sense | GPIO 21 | GPIO input, read in safety checks |

---

## FreeRTOS Task Map

| Task | Core | Priority | Stack | Created by |
|---|---|---|---|---|
| `task_audio` | 1 | 5 | 8 192 B | `task_audio_start()` |
| `mqtt_pub` | 0 | 4 | 6 144 B | `mqtt_client_init()` |
| `store_fwd` | 0 | 4 | 4 096 B | `store_forward_on_reconnect()` (one-shot) |
| `task_telemetry` | 0 | 3 | 4 096 B | `task_telemetry_start()` |
| `fuel_gauge_bg` | any | 2 | 2 048 B | `fuel_gauge_init()` |

`power_fsm_run()` executes in the `app_main` task (core 0, priority 1) and never returns.

---

## Safety Interlocks Summary

| Gate | Condition | Fallback |
|---|---|---|
| OTA start | SOC ≥ 30 % **or** USB VBUS HIGH | Return `ESP_ERR_INVALID_STATE` |
| Haptic pulse | SOC ≥ 15 % | Return `ESP_ERR_NOT_ALLOWED` |
| Mid-cycle low battery | SOC < 15 % (any state except BOOT/FLUSH/SLEEP) | Force `LOW_POWER_FLUSH` |
| Emergency flush | Always — SOC irrelevant | Runs haptic→rails→SD in order; returns first error |

Fuel-gauge read failure in safety checks defaults SOC to 0 (most conservative path).
