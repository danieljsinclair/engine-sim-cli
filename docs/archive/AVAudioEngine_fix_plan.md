# AVAudioEngineHardwareProvider Fix Plan

## Problem
- `AVAudioEngineHardwareProvider` uses AVAudioEngine non-interleaved format
- Render block manually de-interleaves with stack buffer and loop — duplicate logic
- Violates DRY: CoreAudioHardwareProvider does this cleanly via callback wrapper
- Magic numbers, Objective-C buffer manipulation in real-time path

## Root Cause
AVAudioEngine mixer node requires non-interleaved format. CoreAudio RemoteIO (what we use on macOS) supports interleaved natively. Using AVAudioEngine was the wrong platform abstraction choice for iOS.

## Solution
Replace AVAudioEngine with CoreAudio's `kAudioUnitSubType_RemoteIO` AudioUnit. Same API as CoreAudioHardwareProvider — thin C++ wrapper over AudioUnit callback.

## Changes

**File: `engine-sim-bridge/src/hardware/AVAudioEngineHardwareProvider.mm`**

- Remove all AVAudioEngine/AVAudioSourceNode/AVAudioFormat/AVAudioSession types and calls
- Replace with CoreAudio AudioUnit pattern:
  - `setupAudioUnit()`: `AudioComponentFindNext` with `kAudioUnitSubType_RemoteIO`, `AudioComponentInstanceNew`
  - `configureAudioFormat()`: `AudioStreamBasicDescription` with interleaved float32, `kAudioUnitProperty_StreamFormat` on input scope
  - `registerCallbackWithAudioUnit()`: `kAudioUnitProperty_SetRenderCallback` → `remoteIORenderCallbackWrapper`
  - `cleanup()`: `AudioUnitStop`/`AudioUnitUninitialize`/`AudioComponentInstanceDispose` with 50ms drain delay
  - `startPlayback()`/`stopPlayback()`: `AudioOutputUnitStart`/`AudioOutputUnitStop`
  - `setVolume()`: `AudioUnitSetParameter` with `kHALOutputParam_Volume`
- Implement `remoteIORenderCallbackWrapper()` static function:
  - Cast `refCon` to `AVAudioEngineHardwareProvider*`
  - Get interleaved `float*` from `ioData->mBuffers[0].mData`
  - Construct `AudioBufferView` with `(channelData, numberFrames, channels)`
  - Call `self->audioCallback_(view)`, return OSStatus
- Remove `setupAudioSession()`, `configureAudioFormat()` overload, `createSourceNode()` entirely
- Add `logAudioError()` helper for consistent error logging (like CoreAudio version)
- Member `audioUnit_` replaces `audioEngine_` and `sourceNode_`; remove `audioSession_`
- Diagnostics: keep `underrunCount_`/`overrunCount_` (interface requires them); RemoteIO push-mode won't underrun but counters stay

**File: `engine-sim-bridge/include/hardware/AVAudioEngineHardwareProvider.h`**
- Member variable changes: `AVAudioEngine* audioEngine_` → `AudioUnit audioUnit_`; remove `AVAudioSourceNode* sourceNode_`, `AVAudioSession* audioSession_`
- Remove private method declarations: `setupAudioSession()`, `setupAudioEngine()`, `configureAudioFormat()`, `createSourceNode()`
- Add private method declarations: `setupAudioUnit()`, `configureAudioFormat(const AudioStreamFormat&)`, `registerCallbackWithAudioUnit()`, `logAudioError(const char*, OSStatus, const char*)`

**No changes to:**
- Header public interface (still implements `IAudioHardwareProvider`)
- Any simulation/strategy/factory code
- Build system — file stays `.mm` (still needs Objective-C++ for AudioToolbox headers)

## Validation
- Build engine-sim-bridge target (compiles .mm on iOS only)
- Run CLI tests (macOS CoreAudio path unaffected)
- Deploy iOS app — audio should play sine wave without format errors
- No manual buffer manipulation — pipeline's `AudioBufferView` passed directly to callback
