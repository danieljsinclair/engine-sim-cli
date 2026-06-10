# macOS Audio API Research: Playback Position Feedback

**Date**: 2026-01-31
**Purpose**: Research macOS audio APIs that provide real-time playback position feedback to match GUI's zero-delay audio architecture.

## Executive Summary

OpenAL lacks an equivalent to Windows Audio's `GetCurrentWritePosition()`. This document catalogs macOS native audio APIs that provide hardware-level playback timing information.

**Key Finding**: macOS provides multiple APIs for playback position feedback:
1. **AudioQueueGetCurrentTime** - High-level API for audio queue timing
2. **AudioDeviceGetCurrentTime** - Low-level HAL API for direct hardware timing
3. **AudioQueueDeviceGetCurrentTime** - Device-level timing via audio queue
4. **AVAudioEngine/AVAudioPlayerNode** - High-level Swift/Objective-C API with timing support

---

## 1. AudioQueue Services (AudioToolbox Framework)

### 1.1 AudioQueueGetCurrentTime

**API Signature**:
```c
extern OSStatus
AudioQueueGetCurrentTime(
    AudioQueueRef           inAQ,
    AudioQueueTimelineRef   inTimeline,
    AudioTimeStamp         *outTimeStamp,
    Boolean               *outTimelineDiscontinuity
);
```

**Description**: Obtains the current audio queue time. The `mSampleTime` field is in terms of the audio queue's sample rate, relative to when the queue started or will start.

**Requirements**:
- Must create a timeline object using `AudioQueueCreateTimeline()` to detect discontinuities
- Timeline discontinuity flag indicates sample rate changes or buffer underruns

**Setup Code**:
```c
// Create timeline for discontinuity detection
AudioQueueTimelineRef timeline;
OSStatus status = AudioQueueCreateTimeline(inAQ, &timeline);
if (status != noErr) {
    // Handle error
}

// Get current playback position
AudioTimeStamp currentTime;
Boolean discontinuity;
status = AudioQueueGetCurrentTime(inAQ, timeline, &currentTime, &discontinuity);
if (status == noErr && (currentTime.mFlags & kAudioTimeStampSampleTimeValid)) {
    Float64 samplePosition = currentTime.mSampleTime;
    // Use samplePosition for synchronization
}
```

**Documentation**: [AudioQueueGetCurrentTime - Apple Developer](https://developer.apple.com/documentation/audiotoolbox/audioqueuegetcurrenttime(_:_:_:_:))

---

### 1.2 AudioQueueDeviceGetCurrentTime

**API Signature**:
```c
extern OSStatus
AudioQueueDeviceGetCurrentTime(
    AudioQueueRef    inAQ,
    AudioTimeStamp  *outTimeStamp
);
```

**Description**: Obtains the current time of the audio device associated with an audio queue. Unlike `AudioDeviceGetCurrentTime`, this returns valid mHostTime even when device is not running.

**Key Difference**: Returns `mHostTime` even when device is stopped (unlike `AudioDeviceGetCurrentTime` which errors).

**Use Case**: When you need device-level timing but want graceful handling of stopped state.

**Documentation**: Referenced in [AudioQueue.h header](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/AudioQueue.h)

---

### 1.3 AudioQueueCreateTimeline

**API Signature**:
```c
extern OSStatus
AudioQueueCreateTimeline(
    AudioQueueRef           inAQ,
    AudioQueueTimelineRef  *outTimeline
);
```

**Description**: Creates a timeline object for detecting discontinuities in sample timeline (e.g., device sample rate changes, buffer underruns).

**Documentation**: [AudioQueueCreateTimeline - Apple Developer](https://developer.apple.com/documentation/audiotoolbox/audioquecreatetimeline(_:_:))

---

## 2. CoreAudio HAL (AudioHardware Framework)

### 2.1 AudioDeviceGetCurrentTime

**API Signature**:
```c
extern OSStatus
AudioDeviceGetCurrentTime(
    AudioDeviceID   inDevice,
    AudioTimeStamp *outTime
);
```

**Description**: Retrieves the current time from an AudioDevice. **Device must be running** or returns `kAudioHardwareNotRunningError`.

**Use Case**: Direct hardware timing when managing audio devices at HAL level.

**Example**:
```c
AudioDeviceID outputDevice;
// Get default output device
status = AudioHardwareGetProperty(
    kAudioHardwarePropertyDefaultOutputDevice,
    &size, &outputDevice
);

AudioTimeStamp deviceTime;
status = AudioDeviceGetCurrentTime(outputDevice, &deviceTime);
if (status == noErr) {
    Float64 sampleTime = deviceTime.mSampleTime;
}
```

**Documentation**: [AudioHardware.h header](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreAudio.framework/Versions/A/Headers/AudioHardware.h)

---

### 2.2 AudioTimeStamp Structure

**Definition**:
```c
struct AudioTimeStamp {
    Float64         mSampleTime;       // Sample frame time
    UInt64          mHostTime;         // Host CPU time
    Float64         mRateScalar;       // Rate scalar
    UInt64          mWordClockTime;    // Word clock time
    SMPTETime       mSMPTETime;       // SMPTE time
    UInt32          mFlags;            // Valid field flags
    UInt32          mReserved;
};
```

**Flags**:
```c
kAudioTimeStampSampleTimeValid      = (1 << 0),  // Sample time valid
kAudioTimeStampHostTimeValid        = (1 << 1),  // Host time valid
kAudioTimeStampRateScalarValid      = (1 << 2),  // Rate scalar valid
kAudioTimeStampWordClockTimeValid   = (1 << 3),  // Word clock valid
kAudioTimeStampSMPTETimeValid       = (1 << 4),  // SMPTE valid
```

**Documentation**: [AudioTimeStamp - Apple Developer](https://developer.apple.com/documentation/CoreAudioTypes/AudioTimeStamp)

---

### 2.3 Device Properties for Latency Calculation

**kAudioDevicePropertyLatency** (`'ltnc'`):
- `UInt32` containing frames of latency in the AudioDevice
- Input and output latency may differ
- Streams may have additional latency

**kAudioDevicePropertyBufferFrameSize** (`'fsiz'`):
- `UInt32` indicating number of frames in the IO buffers

**kAudioDevicePropertySafetyOffset** (`'saft'`):
- `UInt32` indicating frames ahead (output) or behind (input) current hardware position that is safe to do IO

**Use Case**: Calculate write cursor position by combining current time with buffer size, latency, and safety offset.

**Documentation**: [kAudioDevicePropertyBufferFrameSize - Apple Developer](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertybufferframesize)

---

## 3. AudioUnit Properties

### 3.1 kAudioUnitProperty_CurrentPlayTime

**Property ID**: `kAudioUnitProperty_CurrentPlayTime = 3302`

**Description**: Query to determine audio unit's current time offset from its start time. Useful for monitoring playback progress.

**Value Type**: `AudioTimeStamp`
- Relative to start time
- Sample time of -1 if not yet started

**Scope**: Output scope

**Documentation**: [AudioUnitProperties.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/AudioUnit.framework/Versions/A/Headers/AudioUnitProperties.h)

---

## 4. AVAudioEngine / AVAudioPlayerNode (AVFoundation)

### 4.1 AVAudioPlayerNode Timing

**Key Resources**:
- [Making Sense of Time in AVAudioPlayerNode - Medium](https://medium.com/@mehsamadi/making-sense-of-time-in-avaudioplayernode-475853f84eb6)
- [Stack Overflow: Display song currentTime](https://stackoverflow.com/questions/26574459/how-to-display-song-currenttime-and-duration-by-using-avaudioplayernode)
- [Calculating Song Position in Music Apps - Medium](https://gmcerveny.medium.com/calculating-song-position-in-music-apps-with-swift-and-avaudioengine-75d05a3922d8)

**Concepts**:
- Each player node has its own timeline
- `sampleTime` shows exact position in node's timeline
- `play(at:)` method for scheduled playback
- Calculate position by taking current time and subtracting reference time

**Use Case**: High-level Swift/Objective-C applications requiring timing.

**Documentation**: [AVAudioEngine - Apple Developer](https://developer.apple.com/documentation/avfaudio/avaudioengine)

---

## 5. Calculating Write Cursor Position

### 5.1 Basic Calculation

To calculate the current write cursor position (equivalent to `GetCurrentWritePosition`):

```c
// Get current device time
AudioTimeStamp deviceTime;
OSStatus status = AudioDeviceGetCurrentTime(deviceID, &deviceTime);
if (status != noErr) {
    // Handle error
}

// Query device properties
UInt32 bufferFrameSize;
UInt32 size = sizeof(bufferFrameSize);
status = AudioDeviceGetProperty(
    deviceID,
    0,  // Master channel
    false,  // Output
    kAudioDevicePropertyBufferFrameSize,
    &size,
    &bufferFrameSize
);

UInt32 latency;
size = sizeof(latency);
status = AudioDeviceGetProperty(
    deviceID,
    0,
    false,
    kAudioDevicePropertyLatency,
    &size,
    &latency
);

// Calculate write cursor position
Float64 currentSampleTime = deviceTime.mSampleTime;
Float64 writeCursorPosition = currentSampleTime + bufferFrameSize + latency;
```

**Formula**: `WriteCursor = CurrentSampleTime + BufferFrameSize + Latency + SafetyOffset`

### 5.2 Stack Overflow Reference

- [Core Audio - Buffer Calculation](https://stackoverflow.com/questions/34254248/core-audio-buffer-calculuation)

---

## 6. Comparison with OpenAL

### OpenAL Limitations
- No equivalent to `GetCurrentWritePosition()`
- No direct hardware timing feedback
- Abstracted buffer management

### macOS Native Advantages
- Direct hardware timing via HAL
- Sample-accurate position tracking
- Latency and safety offset information
- Discontinuity detection

---

## 7. Implementation Recommendations

### For CLI Application (C++)

**Option A: AudioQueue Services (Recommended)**
- Use `AudioQueueGetCurrentTime` for playback timing
- Create timeline with `AudioQueueCreateTimeline` for discontinuity detection
- Higher-level API, easier integration than HAL

**Option B: CoreAudio HAL**
- Use `AudioDeviceGetCurrentTime` for direct hardware timing
- Query `kAudioDevicePropertyBufferFrameSize` and `kAudioDevicePropertyLatency`
- Calculate write cursor: `CurrentSampleTime + BufferSize + Latency`
- Lower-level, more control

**Option C: Hybrid**
- Use AudioQueue for playback management
- Use `AudioQueueDeviceGetCurrentTime` for device-level timing
- Query device properties for buffer/latency information

### Code Example: Write Cursor Feedback

```cpp
#include <AudioToolbox/AudioQueue.h>

class MacAudioPlayer {
    AudioQueueRef queue;
    AudioQueueTimelineRef timeline;
    AudioDeviceID deviceID;

public:
    Float64 getCurrentWritePosition() {
        // Get current queue time
        AudioTimeStamp currentTime;
        Boolean discontinuity = false;
        OSStatus status = AudioQueueGetCurrentTime(
            queue, timeline, &currentTime, &discontinuity
        );

        if (status != noErr) {
            return 0.0;
        }

        // Get device latency
        UInt32 latency = 0;
        UInt32 size = sizeof(latency);
        AudioDeviceGetProperty(
            deviceID, 0, false,
            kAudioDevicePropertyLatency,
            &size, &latency
        );

        // Get buffer size
        UInt32 bufferSize = 0;
        size = sizeof(bufferSize);
        AudioDeviceGetProperty(
            deviceID, 0, false,
            kAudioDevicePropertyBufferFrameSize,
            &size, &bufferSize
        );

        // Calculate write cursor
        Float64 writeCursor = currentTime.mSampleTime + bufferSize + latency;
        return writeCursor;
    }
};
```

---

## 8. Migration Path from OpenAL

### Current OpenAL Implementation
- Uses `AL_BUFFERS_PROCESSED` and `AL_BUFFERS_QUEUED`
- No direct position feedback
- Buffer management via `alSourceUnqueueBuffers` / `alSourceQueueBuffers`

### Proposed Migration
1. Replace `ALCcontext*` with `AudioQueueRef`
2. Replace buffer queue management with AudioQueue buffer callbacks
3. Add timing queries using `AudioQueueGetCurrentTime`
4. Calculate write cursor for zero-delay feedback

---

## 9. Additional Resources

### Documentation
- [Core Audio Essentials - Apple Developer](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/CoreAudioEssentials/CoreAudioEssentials.html)
- [Audio Queue Services Programming Guide - Apple](https://developer.apple.com/documentation/audiotoolbox/audio_queue_services)
- [Technical Note TN2321: Saving Power During Audio I/O](https://developer.apple.com/library/archive/technotes/tn2321/_index.html)

### Code Examples
- [How to use AudioQueue to play a sound for Mac OSX in C++](https://stackoverflow.com/questions/4863811/how-to-use-audioqueue-to-play-a-sound-for-mac-osx-in-c)
- [Mac OS X CoreAudio reference setup code - Handmade Hero](https://hero.handmade.network/forums/code-discussion/t/56-mac_os_x_coreaudio_reference_setup_code)
- [coreaudio-examples on GitHub](https://github.com/rweichler/coreaudio-examples)

### Header Files
- [AudioQueue.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/AudioQueue.h)
- [AudioHardware.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreAudio.framework/Versions/A/Headers/AudioHardware.h)
- [AudioUnitProperties.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/AudioUnit.framework/Versions/A/Headers/AudioUnitProperties.h)
- [CoreAudioTypes.h](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreAudio.framework/Versions/A/Headers/CoreAudioTypes.h)

### Related Discussions
- [Simple low-latency audio playback in iOS Swift](https://stackoverflow.com/questions/34680007/simple-low-latency-audio-playback-in-ios-swift)
- [How to check current time and duration in AudioQueue](https://stackoverflow.com/questions/3395244/how-to-check-current-time-and-duration-in-audioqueue)

---

## 10. Next Steps

1. **Evaluate API choice** based on:
   - Current architecture (pure C++ vs Objective-C/Swift)
   - Latency requirements
   - Integration complexity

2. **Prototype timing query**:
   - Implement `AudioQueueGetCurrentTime` query
   - Test timing accuracy vs Windows GUI
   - Verify zero-delay behavior

3. **Benchmark performance**:
   - Measure query overhead
   - Compare with OpenAL buffer management
   - Validate synchronization accuracy

4. **Migration strategy**:
   - Can we extend OpenAL with HAL queries?
   - Full AudioQueue migration?
   - Hybrid approach?

---

**Sources**:
- [AudioQueueGetCurrentTime - Apple Developer](https://developer.apple.com/documentation/audiotoolbox/audioqueuegetcurrenttime(_:_:_:_:))
- [AudioQueueCreateTimeline - Apple Developer](https://developer.apple.com/documentation/audiotoolbox/audioquecreatetimeline(_:_:))
- [AudioQueue.h - GitHub (phracker/MacOSX-SDKs)](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/AudioQueue.h)
- [AudioHardware.h - GitHub (phracker/MacOSX-SDKs)](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreAudio.framework/Versions/A/Headers/AudioHardware.h)
- [kAudioDevicePropertyBufferFrameSize - Apple Developer](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertybufferframesize)
- [How to check current time and duration in AudioQueue - Stack Overflow](https://stackoverflow.com/questions/3395244/how-to-check-current-time-and-duration-in-audioqueue)
- [Making Sense of Time in AVAudioPlayerNode - Medium](https://medium.com/@mehsamadi/making-sense-of-time-in-avaudioplayernode-475853f84eb6)
- [AVAudioEngine - Apple Developer](https://developer.apple.com/documentation/avfaudio/avaudioengine)
- [Core Audio Essentials - Apple Developer](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/CoreAudioEssentials/CoreAudioEssentials.html)
- [AudioTimeStamp - Apple Developer](https://developer.apple.com/documentation/CoreAudioTypes/AudioTimeStamp)
- [How to use AudioQueue to play a sound for Mac OSX in C++ - Stack Overflow](https://stackoverflow.com/questions/4863811/how-to-use-audioqueue-to-play-a-sound-for-mac-osx-in-c)
