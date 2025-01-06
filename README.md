# SF2LV2 - SoundFont to LV2 Plugin Generator

A tool for converting SoundFont (.sf2) files into LV2 plugins, specifically designed for use with the Isla Instruments S2400/Caladan but compatible with any LV2 host.

## Overview

SF2LV2 converts SoundFont (.sf2) files into fully functional LV2 plugins with:
- MIDI input support
- Stereo audio output
- Real-time time control over Filters and Envelopes.
- Full preset name display.
- Bank and program change support

## Features

- **Preset Management**: All SoundFont presets are available through the Program selector
- **Sound Shaping**:
  - Cutoff: Filter frequency control 
  - Resonance: Filter resonance control 
  - ADSR Envelope: Attack, Decay, Sustain, Release controls
- **Level Control**: Master volume control 

## Building

### Prerequisites
- FluidSynth development libraries
- LV2 development headers
- GCC compiler
- Make build system

### Build Commands

Build with specific SoundFont and plugin name:
```bash
make clean && make PLUGIN_NAME=YourPluginName SF2_FILE=YourSoundFont.sf2
```

### Installation

Install to system LV2 directory:
```bash
sudo make install PLUGIN_NAME=YourPluginName SF2_FILE=YourSoundFont.sf2
```

## Usage

1. Place your .sf2 file in the project root directory
2. Build the plugin with appropriate PLUGIN_NAME and SF2_FILE
3. Install the plugin
4. The plugin will appear in your LV2 host as "YourPluginName"

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

## Troubleshooting

Common issues and solutions:
1. **Missing SoundFont**: Ensure .sf2 file is in project root
2. **Build Errors**: Check FluidSynth and LV2 development packages
3. **Installation Issues**: Verify write permissions to /usr/lib/lv2/

## License

MIT License - See LICENSE file for details. This is a permissive license that lets people do anything they want with the code as long as they provide attribution back to you and don't hold you liable.

## Credits

- **FluidSynth** ([github.com/FluidSynth/fluidsynth](https://github.com/FluidSynth/fluidsynth))  
  A real-time software synthesizer based on the SoundFont 2 specifications. FluidSynth provides the core synthesis engine for this plugin, handling SoundFont loading, MIDI processing, and audio generation.

- **LV2** ([lv2plug.in](https://lv2plug.in/))  
  A portable plugin standard for audio systems. LV2 provides the plugin architecture and host interface, enabling integration with digital audio workstations and other audio software.

- Created by Brad Holland/Isla Instruments ([www.islainstruments.com](https://www.islainstruments.com)) with significant assistance from Claude AI & ChatGPT