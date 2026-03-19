// IAudioRenderer.h - Renderer interface
// Strategy pattern - abstracts rendering strategy (sync-pull vs cursor-chasing)
// OCP: New modes can be added without modifying existing code
// SRP: Each renderer encapsulates its own rendering logic
// DI: Renderer is injected via constructor

#ifndef IAUDIO_RENDERER_H
#define IAUDIO_RENDERER_H

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;
    
    // Render audio to the buffer list - returns true if rendering succeeded
    virtual bool render(
        void* context,
        AudioBufferList* ioData,
        UInt32 numberFrames
    ) = 0;
    
    // Check if this renderer is active/enabled
    virtual bool isEnabled() const = 0;
    
    // Get the name of this renderer for diagnostics
    virtual const char* getName() const = 0;
    
    // Add frames to the renderer's buffer (for playBuffer compatibility)
    // Returns true if frames were added successfully
    virtual bool AddFrames(void* context, float* buffer, int frameCount) = 0;
};

#endif // IAUDIO_RENDERER_H
