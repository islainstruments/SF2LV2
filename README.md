# SF2LV2 - SoundFont to LV2 Plugin Generator

A tool for converting SoundFont (.sf2) files into LV2 plugins, specifically designed for use with the Isla Instruments S2400/Caladan but compatible with any LV2 host.

<img width="1207" alt="image" src="https://github.com/user-attachments/assets/233b3548-0817-4aa9-9692-eb29e586a789" />




## Overview

SF2LV2 converts SoundFont (.sf2) files into fully functional LV2 plugins with:
- MIDI input support
- Stereo audio output
- Real-time control over Filters and Envelopes (when properly configured in the SoundFont)
- Full preset name display
- Bank and program change support

## Features

- **Preset Management**: All SoundFont presets are available through the Program selector
- **Sound Shaping** (requires proper modulator setup in the SoundFont):
  - Cutoff: Filter frequency control 
  - Resonance: Filter resonance control 
  - ADSR Envelope: Attack, Decay, Sustain, Release controls
- **Level Control**: Master volume control 

**Important Note**: The filter and envelope controls will only affect the sound if the appropriate modulators are set up in the SoundFont file. You can easily configure these modulators using [Polyphone](https://www.polyphone-soundfonts.com/), a free SoundFont editor.

## Building

### Prerequisites
- FluidSynth development libraries
- LV2 development headers
- GCC compiler
- Make build system

### Build Commands

Just run 'make' - the build process is interactive allowing you to:
1. Choose between batch processing all .sf2 files in the directory or processing a single file
2. If processing a single file:
   - Enter your source .sf2 file and chosen plugin name
   - Choose whether to install to the system LV2 folder
3. If batch processing:
   - All .sf2 files in the current directory will be processed
   - Each plugin will be named after its source file (without the .sf2 extension)
   - Choose whether to install all plugins to the system LV2 folder

### Control Parameters

The plugin provides several real-time control parameters that can be automated or controlled via MIDI CC messages:

- **Level**: Master volume control (0.0 to 2.0)
- **Program**: Preset selection from the SoundFont
- **Filter Controls**:
  - Cutoff (CC 74): Controls the filter cutoff frequency
  - Resonance (CC 71): Controls the filter resonance
- **ADSR Envelope**:
  - Attack (CC 73): Controls the attack time
  - Decay (CC 75): Controls the decay time
  - Sustain (CC 70): Controls the sustain level
  - Release (CC 72): Controls the release time

All MIDI CC controls range from 0-127 and can be automated through your DAW or controlled via external MIDI controllers.

### Debug Output

The plugin includes a debug mode that can be enabled by setting `plugin->debug = true` in the code. When enabled, it outputs:
- SoundFont loading information
- Available presets and their bank/program numbers
- Program changes with bank/program details
- CC value comparisons between the plugin's state and FluidSynth's internal state

## Technical Details

### Build Process
1. Compiles the metadata generator (ttl_generator.c)
2. Scans the SoundFont file for all presets
3. Generates LV2 TTL files describing the plugin
4. Compiles the plugin runtime (synth_plugin.c)
5. Packages everything into an LV2 bundle

### Plugin Structure
- **Metadata Generator** (ttl_generator.c):
  - Scans SoundFont presets
  - Generates LV2 metadata
  - Creates plugin description files

- **Plugin Runtime** (synth_plugin.c):
  - Handles MIDI input
  - Manages preset selection
  - Controls sound parameters
  - Processes audio output

### File Structure
```
build/
  └── [PLUGIN_NAME].lv2/
      ├── [PLUGIN_NAME].so  (Plugin binary)
      ├── [PLUGIN_NAME].ttl (Plugin description)
      ├── manifest.ttl      (LV2 manifest)
      └── [SF2_FILE]        (Copied SoundFont)
```

## License

MIT License - See LICENSE file for details.

## Credits

- **FluidSynth** ([github.com/FluidSynth/fluidsynth](https://github.com/FluidSynth/fluidsynth))  
  A real-time software synthesizer based on the SoundFont 2 specifications. FluidSynth provides the core synthesis engine for this plugin, handling SoundFont loading, MIDI processing, and audio generation.

- **LV2** ([lv2plug.in](https://lv2plug.in/))  
  A portable plugin standard for audio systems. LV2 provides the plugin architecture and host interface, enabling integration with digital audio workstations and other audio software.

- Created by Brad Holland/Isla Instruments ([www.islainstruments.com](https://www.islainstruments.com)) with assistance from Claude AI & ChatGPT
