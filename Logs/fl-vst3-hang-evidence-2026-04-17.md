# FL Studio VST3 Hang Evidence — 2026-04-17

## Artifacts

- Original user-supplied dump reference: `C:\Users\ham\AppData\Local\Temp\FL64 (2).DMP`
- Current local dump present during cleanup: `C:\Users\ham\AppData\Local\Temp\FL64.DMP`
  - Size: `746262435` bytes
  - Timestamp: `2026-04-17 08:33:06`
- Saved log excerpt: [fl-vst3-hang-trace-tail-2026-04-17.txt](/C:/Users/ham/Documents/DRUMENGINE/Logs/fl-vst3-hang-trace-tail-2026-04-17.txt)

The dump was intentionally not copied into the workspace because it is a large binary artifact. The paths above are the preserved references.

## What The Log Proved

The VST3 remove-from-slot path completed all plugin-side teardown steps and returned:

- `VST3 setProcessing state=false` -> `Processor reset` -> `EXIT`
- `VST3 setActive state=false` -> `Processor releaseResources` -> `EXIT`
- `VST3 editor removed` -> `EXIT`
- `VST3 editor destructor` -> `EXIT`
- `VST3 EditController terminate` -> `EXIT`
- `VST3 EditController destructor` -> `EXIT`
- `VST3 terminate` -> `EXIT`
- `VST3 component destructor` -> `EXIT`
- `Processor destructor` -> `EXIT`

That means the hang did **not** remain in:

- `processBlock()`
- `projectMutex`
- `reset()`
- `releaseResources()`
- `getState()/setState()`
- editor/controller removal
- processor/controller/component destructors

## What The Dump / Thread View Showed

After plugin teardown had already completed, the remaining stuck activity was on the host/system side, not in `HPDG`:

- `23968` -> `FLEngine_x64.dll`
- `21556` -> `QuickFontCache_x64.dll`, current frame in `dwmapi.dll`
- `16408` -> `universalaudio...asio_x64.dll`
- Additional waiting/top frames observed during dump pass: `FLEngine_x64.dll`, `QuickFontCache_x64.dll`, `dwmapi.dll`, `KERNELBASE.dll`, `ntdll.dll`

The DebugView line `MSAFD: Pending APCs in cleanup! Waiting...` was noted, but the dump evidence did **not** positively tie the stuck point to `ws2_32/MSAFD`.

## Technical Boundary

`HPDG` / JUCE VST3 teardown completed fully.

The remaining freeze boundary is therefore:

- FL Studio host cleanup
- FL UI / font cache / composition path
- audio-driver / host-side teardown path

It is **not** supported by the current evidence to keep making blind fixes in:

- `Source/Plugin/PluginProcessor.cpp`
- `Source/Plugin/PluginEditor.cpp`
- `JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp`

without a new host-side stack or a different reproducer.
