# Platform Audio Research

**Document Version:** 1.0
**Date:** 2026-04-01
**Status:** Research Document

---

## Executive Summary

This document details audio mechanism research for iOS and ESP32 platforms to inform the design of a modular, cross-platform audio architecture for the engine-sim bridge.

**Key Findings:**
- iOS uses **AVAudioEngine** (not macOS's CoreAudio AudioQueue)
- ESP32 uses **I2S peripheral** with DMA and optional PDM support
- Both support **callback-based rendering** patterns
- Buffer management strategies differ significantly between platforms

---

## iOS Audio Architecture

### 1. Primary Audio APIs

iOS does NOT use macOS's CoreAudio AudioQueue. Instead, iOS provides:

| API Level | Technology | Use Case |
|-----------|------------|----------|
| **High-level** | AVFoundation/AVAudioPlayer | Simple playback |
| **Mid-level** | **AVAudioEngine** | Real-time processing, simultaneous I/O |
| **Low-level** | AudioUnit (Core Audio) | Fine-grained control |
| **Session** | AVAudioSession | Audio session configuration |

### 2. AVAudioEngine Architecture

**Key Characteristics:**
- Node-based audio graph architecture
- Mid-level API (simpler than raw AudioUnit, more flexible than AVAudioPlayer)
- Supports simultaneous playback and recording
- Built-in format conversion with `AVAudioConverter`

**Node-Based Graph:**
```
Input Node → [Processing Nodes] → Output Node
     ↓
  installTap() for real-time processing
```

### 3. Real-Time Audio Processing

**installTap Mechanism:**
```swift
// Real-time audio processing callback
engine.inputNode.installTap(
    onBus: 0,
    bufferSize: 1024,
    format: inputFormat
) { buffer, time in
    // Process audio buffer here
    // Called in real-time audio context
}
```

**Key Points:**
- Tap installed on any `AVAudioNode`
- Receives `AVAudioPCMBuffer` objects
- Called in real-time context (avoid blocking operations)
- Buffer size configurable (typically 512-4096 frames)

### 4. Sample Rate and Format Handling

**Typical Configurations:**
- Sample rates: 16kHz, 44.1kHz, 48kHz
- Bit depths: 16-bit, 24-bit, 32-bit float
- Channels: Mono or Stereo

**Format Conversion:**
```swift
// AVAudioConverter for sample rate conversion
let converter = AVAudioConverter(
    from: inputFormat,
    to: outputFormat
)
```

**Important Notes:**
- Format conversion requires callback-based approach
- Output buffer may have variable number of samples
- Must handle multiple converter calls to fill output buffer

### 5. Audio Session Management

**AVAudioSession Configuration:**
```swift
let session = AVAudioSession.sharedInstance()

// For simultaneous playback and recording
try session.setCategory(
    .playAndRecord,
    mode: .voiceChat,
    options: [.defaultToSpeaker]
)

// Handle configuration changes
NotificationCenter.default.addObserver(
    self,
    selector: #selector(handleInterruption),
    name: AVAudioEngineConfigurationChange,
    object: engine
)
```

**Important Considerations:**
- Voice Processing I/O (`setVoiceProcessingEnabled`) for echo cancellation
- Configuration changes can stop the engine
- Must monitor and restart on configuration changes

### 6. Limitations and Gotchas

1. **Thread Safety**: Audio callbacks run on real-time audio thread - avoid blocking
2. **Buffer Management**: Automatic but requires careful sizing for low latency
3. **Format Conversion**: Callback-based approach adds complexity
4. **Interruptions**: Phone calls, alarms can interrupt audio
5. **Latency**: Hardware buffers add unavoidable latency

---

## ESP32 Audio Architecture

### 1. I2S Peripheral Overview

**ESP32 I2S Features:**
- 2x I2S peripherals (ESP32-S3 has I2S0 and I2S1 with more features)
- DMA controller for zero-copy audio streaming
- Master/Slave operation
- Transmitter/Receiver modes
- Full-duplex support (TX + RX share clock)

**Supported Modes:**

| Mode | Description | Chips |
|------|-------------|-------|
| **Standard** | Philips/MSB/PCM formats | All |
| **PDM** | Pulse-density modulation | ESP32, ESP32-S3 |
| **TDM** | Time-division multiplexing | ESP32-S3 |
| **ADC/DAC** | Direct internal ADC/DAC | ESP32 only |
| **LCD/Camera** | Parallel bus mode | ESP32 only |

### 2. I2S Driver Architecture

**New Driver Structure (ESP-IDF v5+):**

```
i2s.h (legacy)     → Don't use, will be removed
i2s_std.h          → Standard mode (recommended)
i2s_pdm.h          → PDM mode
i2s_common.h       → Common utilities
i2s_types.h        → Type definitions
```

**Channel-Based API:**
```c
// Create channel
i2s_chan_handle_t tx_handle;
i2s_new_channel(&chan_cfg, &tx_handle, NULL);

// Initialize mode
i2s_channel_init_std_mode(tx_handle, &std_cfg);

// Enable/start
i2s_channel_enable(tx_handle);

// Write data (blocking)
i2s_channel_write(tx_handle, src_buf, size, &bytes_written, timeout);

// Or register callback (async)
i2s_channel_register_event_callback(tx_handle, &callbacks, user_ctx);
```

### 3. DMA Buffer Management

**Key Configuration Parameters:**

```c
typedef struct {
    int id;                      // I2S port (0, 1, or AUTO)
    i2s_role_t role;             // MASTER or SLAVE
    uint32_t dma_desc_num;       // Number of DMA descriptors
    uint32_t dma_frame_num;      // Frames per DMA buffer
    bool auto_clear;             // Auto-clear TX buffers
    bool allow_pd;               // Allow power management
    int intr_priority;           // Interrupt priority
} i2s_chan_config_t;
```

**DMA Buffer Sizing:**
```
dma_buffer_size = dma_frame_num * slot_num * slot_bit_width / 8

Total buffer memory = dma_desc_num * dma_buffer_size
```

**Sizing Guidelines (from ESP-IDF docs):**
1. Max DMA buffer size: 4092 bytes
2. Calculate interrupt interval: `dma_frame_num / sample_rate`
3. Determine `dma_desc_num` based on polling cycle
4. Ensure `recv_buffer_size > dma_desc_num * dma_buffer_size`

**Example Calculation:**
```
Given:
- sample_rate = 144000 Hz
- data_bit_width = 32 bits
- slot_num = 2
- polling_cycle = 10 ms

Calculate:
- dma_frame_num <= 511 (to keep buffer <= 4092 bytes)
- interrupt_interval = 511 / 144000 = 3.549 ms
- dma_desc_num > 10 / 3.549 = 3
- recv_buffer_size > 3 * 4092 = 12276 bytes
```

### 4. Clock Configuration

**Clock Sources:**
```c
typedef enum {
    I2S_CLK_SRC_DEFAULT,  // Default PLL clock
    I2S_CLK_SRC_APLL,     // Audio PLL (more precise)
} i2s_clock_src_t;
```

**Clock Terminology:**
- **SCLK**: Source clock frequency
- **MCLK**: Master clock (MCLK = sample_rate * mclk_multiple)
- **BCLK**: Bit clock
- **WS/LRCK**: Word select / Left-right clock (equals sample rate)

**MCLK Multiples:**
```c
typedef enum {
    I2S_MCLK_MULTIPLE_128,   // Standard
    I2S_MCLK_MULTIPLE_192,   // 24-bit compatible
    I2S_MCLK_MULTIPLE_256,   // Most cases
    I2S_MCLK_MULTIPLE_384,   // 24-bit compatible (recommended for 24-bit)
    I2S_MCLK_MULTIPLE_512,
    // ... etc
} i2s_mclk_multiple_t;
```

**Important Note for 24-bit audio:**
Use `I2S_MCLK_MULTIPLE_384` (or multiple of 3) for accurate WS timing with 24-bit data.

### 5. PDM Mode (ESP32-S3)

**PDM-to-PCM Converter (Hardware):**
```c
// Receive PDM and convert to PCM in hardware
i2s_pdm_rx_slot_config_t slot_cfg = {
    .data_fmt = I2S_PDM_DATA_FMT_PCM,  // Enable HW converter
    // ...
};
```

**Down-sampling Modes:**
```c
typedef enum {
    I2S_PDM_DSR_8S,   // PDM clock = sample_rate * 64
    I2S_PDM_DSR_16S,  // PDM clock = sample_rate * 128
} i2s_pdm_dsr_t;
```

**PCM-to-PDM (TX):**
```c
// Write PCM data, hardware converts to PDM
i2s_pdm_tx_slot_config_t slot_cfg = {
    .data_fmt = I2S_PDM_DATA_FMT_PCM,
    // ...
};

// Up-sampling configuration
uint32_t up_sample_fp = 960;
uint32_t up_sample_fs = 480;
// PDM clock = 128 * PCM sample_rate
```

### 6. Callback-Based Async I/O

**Event Callbacks:**
```c
typedef struct {
    i2s_isr_callback_t on_recv;         // RX: data received
    i2s_isr_callback_t on_recv_q_ovf;   // RX: queue overflow
    i2s_isr_callback_t on_sent;         // TX: data sent
    i2s_isr_callback_t on_send_q_ovf;   // TX: queue overflow
} i2s_event_callbacks_t;

// Register callbacks
i2s_channel_register_event_callback(handle, &callbacks, user_ctx);
```

**Callback Signature:**
```c
typedef bool (*i2s_isr_callback_t)(
    i2s_chan_handle_t handle,
    i2s_event_data_t *event,
    void *user_ctx
);
```

**Important Notes:**
- Callbacks run in **ISR context**
- Keep logic minimal (no blocking, no floating point, no non-reentrant functions)
- Use IRAM-safe attributes for real-time applications
- Event data includes DMA buffer address and size

### 7. Data Format Considerations

**Standard Mode Bit Widths:**
- 8-bit: Data in high 8 bits of 16-bit word
- 16-bit: Normal
- 24-bit: Data in high 24 bits of 32-bit word
- 32-bit: Normal

**Mono/Stereophonic:**
- Mono: Single slot data
- Stereo: Two slots (left/right)
- Slot mask can select LEFT, RIGHT, or BOTH

**Byte Ordering (ESP32 Quirk):**
For 8-bit and 16-bit mono modes, data is swapped every two bytes in RX mode. Manual swap may be required.

### 8. Advanced Features

**Rate Tuning:**
```c
// Fine-tune MCLK to match producer/consumer rates
i2s_channel_tune_rate(handle, &tune_cfg, &tune_info);
```

**Data Preloading:**
```c
// Preload data before starting to reduce initial latency
i2s_channel_preload_data(tx_handle, src, size, &bytes_loaded);
```

**Power Management:**
- Driver automatically acquires power lock during I/O
- APLL mode: `ESP_PM_NO_LIGHT_SLEEP`
- APB clock: `ESP_PM_APB_FREQ_MAX`

---

## Common Abstraction Patterns

### 1. Callback-Based Rendering

**Both platforms support callback-based audio rendering:**

| Platform | Mechanism | Context | Constraints |
|----------|-----------|---------|-------------|
| **iOS** | `installTap` | Real-time audio thread | No blocking operations |
| **ESP32** | Event callbacks | ISR context | Minimal logic, no blocking |

**Common Pattern:**
```cpp
// Pseudo-code for cross-platform callback
class AudioCallback {
    virtual void OnAudioBuffer(
        float* buffer,
        size_t frame_count,
        void* user_data
    ) = 0;
};
```

### 2. Buffer Management Strategies

| Aspect | iOS | ESP32 |
|--------|-----|-------|
| **Allocation** | Automatic (AVAudioPCMBuffer) | Manual (DMA descriptors) |
| **Sizing** | Configurable (bufferSize) | Calculated (frame_num * desc_num) |
| **Lifecycle** | Managed by engine | Manual start/stop |
| **Zero-copy** | Generally yes | Yes (DMA) |

### 3. Sample Rate Handling

**Common Rates:**
- 16 kHz: Voice/audio conferencing
- 44.1 kHz: CD quality
- 48 kHz: Professional audio

**Conversion:**
- iOS: Built-in AVAudioConverter
- ESP32: Manual resampling or PDM hardware converter

### 4. Channel Configuration

**Both support:**
- Mono (1 channel)
- Stereo (2 channels)

**iOS:**
- Configurable via AVAudioChannelLayout

**ESP32:**
- Configured via slot mode and slot mask
- Hardware can route to single or both channels

---

## Cross-Platform Abstraction Recommendations

### 1. Unified Audio Interface

```cpp
class IAudioPlatform {
public:
    virtual ~IAudioPlatform() = default;

    // Initialization
    virtual bool Initialize(
        const AudioConfig& config,
        AudioCallback* callback
    ) = 0;
    virtual void Shutdown() = 0;

    // Stream control
    virtual bool Start() = 0;
    virtual bool Stop() = 0;

    // Configuration
    virtual bool SetSampleRate(uint32_t rate) = 0;
    virtual bool SetChannels(uint32_t channels) = 0;
    virtual bool SetBufferSize(size_t frames) = 0;

    // Status
    virtual bool IsRunning() const = 0;
    virtual AudioStats GetStats() const = 0;
};

struct AudioConfig {
    uint32_t sample_rate = 48000;
    uint32_t channels = 1;
    size_t buffer_frames = 1024;
    uint32_t bit_depth = 16;
    bool enable_input = false;
    bool enable_output = true;
};

struct AudioStats {
    uint32_t underrun_count = 0;
    uint32_t overrun_count = 0;
    double latency_ms = 0.0;
};
```

### 2. Callback Interface

```cpp
class AudioCallback {
public:
    virtual ~AudioCallback() = default;

    // Called when audio buffer is needed (output)
    // or when audio buffer is ready (input)
    virtual void ProcessAudio(
        float* output_buffer,  // NULL if input-only
        const float* input_buffer,  // NULL if output-only
        size_t frame_count,
        void* user_data
    ) = 0;
};
```

### 3. Platform Implementations

```cpp
// iOS implementation using AVAudioEngine
class iOSAudioPlatform : public IAudioPlatform {
    // Wraps AVAudioEngine
    // Uses installTap for callbacks
    // Handles AVAudioSession configuration
};

// ESP32 implementation using I2S driver
class ESP32AudioPlatform : public IAudioPlatform {
    // Wraps ESP-IDF I2S driver
    // Uses event callbacks for async I/O
    // Manages DMA buffer configuration
};
```

### 4. Configuration Mapping

| Parameter | iOS | ESP32 | Notes |
|-----------|-----|-------|-------|
| Sample rate | AVAudioFormat | i2s_std_clk_config_t | Use APLL on ESP32 for precision |
| Channels | AVAudioChannelLayout | i2s_slot_mode_t | Mono/stereo |
| Buffer size | bufferSize in installTap | dma_frame_num | Different scaling factors |
| Bit depth | AVAudioCommonFormat | i2s_data_bit_width_t | 16/24/32-bit |

---

## Key Differences Summary

| Aspect | iOS | ESP32 | Impact |
|--------|-----|-------|--------|
| **API Level** | Mid-high (Obj-C/Swift) | Low (C) | iOS easier to use |
| **Buffer Mgmt** | Automatic | Manual | ESP32 requires careful sizing |
| **Callback Context** | Audio thread | ISR | ESP32 more constrained |
| **Clock Precision** | Hardware managed | Configurable | ESP32 offers more control |
| **Power Mgmt** | System managed | Configurable | ESP32 can optimize better |
| **Format Conversion** | Built-in | Manual/hardware | iOS more convenient |

---

## Sources

### iOS Audio:
- [Apple Developer - AVAudioEngine](https://developer.apple.com/documentation/avfaudio/avaudioengine)
- [Tips about AVAudioEngine](https://snakamura.github.io/log/2024/11/audio_engine.html)
- [Apple Developer - Audio](https://developers.apple.com/audio/)

### ESP32 Audio:
- [ESP-IDF I2S Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html)
- [ESP32 I2S Speaker Library](https://github.com/jahrulnr/esp32-speaker)
- [I2S Audio Interface of ESP32](https://circuitlabs.net/i2s-audio-interface-of-esp32/)

---

## Next Steps

1. **Design modular audio architecture** based on findings
2. **Create platform abstraction layer** with unified interface
3. **Implement platform-specific adapters** for iOS and ESP32
4. **Develop cross-platform audio test suite**
5. **Document performance characteristics** for each platform

---

*End of Research Document*
