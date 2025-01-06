/*
 * SF2LV2 - SoundFont to LV2 Plugin Generator
 * Synthesizer Plugin Runtime (synth_plugin.c)
 *
 * This plugin:
 * 1. Loads a SoundFont file using FluidSynth
 * 2. Handles MIDI input and program changes
 * 3. Processes real-time parameter controls
 * 4. Generates audio output using FluidSynth
 *
 * Control Parameters:
 * - Level: Master volume (0.0 - 2.0)
 * - Program: Preset selection (0 - num_presets)
 * - Cutoff: Filter cutoff frequency (0.0 - 1.0)
 * - Resonance: Filter resonance (0.0 - 1.0)
 * - ADSR: Attack, Decay, Sustain, Release controls (0.0 - 1.0)
 */

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <fluidsynth.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Plugin name and SF2 file can be defined at compile time */
#ifndef PLUGIN_NAME
#define PLUGIN_NAME "undefined"
#endif

#ifndef SF2_FILE
#define SF2_FILE "undefined.sf2"
#endif

/* Display name for logging */
static const char* PLUGIN_DISPLAY_NAME = PLUGIN_NAME;
#define PLUGIN_URI "https://github.com/bradholland/sf2lv2/" PLUGIN_NAME

/* Audio processing buffer size */
#define BUFFER_SIZE 64

/* MIDI CC numbers for sound parameters */
#define CC_CUTOFF    21  // Filter cutoff frequency control
#define CC_RESONANCE 22  // Filter resonance control
#define CC_ATTACK    23  // Envelope attack time control
#define CC_DECAY     24  // Envelope decay time control
#define CC_SUSTAIN   25  // Envelope sustain level control
#define CC_RELEASE   26  // Envelope release time control

/* Structure to store bank/program pairs for SoundFont presets */
typedef struct {
    int bank;   // MIDI bank number
    int prog;   // MIDI program number
} BankProgram;

/* Port indices for the plugin's inputs and outputs */
typedef enum {
    PORT_EVENTS = 0,      // MIDI input port
    PORT_AUDIO_OUT_L = 1, // Left audio output
    PORT_AUDIO_OUT_R = 2, // Right audio output
    PORT_LEVEL = 3,       // Master level control (0.0 to 2.0)
    PORT_PROGRAM = 4,     // Program selection
    PORT_CUTOFF = 5,      // Filter cutoff control
    PORT_RESONANCE = 6,   // Filter resonance control
    PORT_ATTACK = 7,      // Envelope attack control
    PORT_DECAY = 8,       // Envelope decay control
    PORT_SUSTAIN = 9,     // Envelope sustain control
    PORT_RELEASE = 10     // Envelope release control
} PortIndex;

/* Structure for URID mapping */
typedef struct {
    LV2_URID midi_Event;  // URID for MIDI event type
} URIDs;

/* Main plugin instance structure */
typedef struct {
    // LV2 features
    LV2_URID_Map* map;    // URID mapping feature
    URIDs urids;          // Mapped URIDs

    // Port connections
    const LV2_Atom_Sequence* events_in;  // MIDI input buffer
    float* audio_out_l;    // Left audio output buffer
    float* audio_out_r;    // Right audio output buffer
    float* level_port;     // Master level control
    float* program_port;   // Program selection
    float* cutoff_port;    // Filter cutoff
    float* resonance_port; // Filter resonance
    float* attack_port;    // Attack time
    float* decay_port;     // Decay time
    float* sustain_port;   // Sustain level
    float* release_port;   // Release time

    // FluidSynth instances
    fluid_settings_t* settings;  // FluidSynth settings
    fluid_synth_t* synth;       // FluidSynth synthesizer
    int current_program;        // Currently selected program
    BankProgram* programs;      // Array of available programs
    int sfont_id;              // Loaded SoundFont ID
    int program_count;         // Total number of programs

    // Audio processing
    char* bundle_path;     // Path to plugin bundle
    float* buffer_l;       // Left channel processing buffer
    float* buffer_r;       // Right channel processing buffer
    double rate;          // Sample rate

    // Parameter tracking
    float last_cutoff;     // Previous cutoff value
    float last_resonance;  // Previous resonance value
    float prev_cutoff;     // Last sent cutoff value
    float prev_resonance;  // Last sent resonance value
    float prev_attack;     // Last sent attack value
    float prev_decay;      // Last sent decay value
    float prev_sustain;    // Last sent sustain value
    float prev_release;    // Last sent release value
    bool controls_touched[6];  // Flags for modified controls
    bool controls_initialized; // Whether controls are initialized
    bool preset_just_changed; // Flag for preset change
} Plugin;

/*
 * Load and initialize the SoundFont file
 * Returns the SoundFont ID if successful, -1 on failure
 */
static int load_soundfont(Plugin* plugin) {
    // Construct path to SoundFont file
    char sf_path[4096];
    snprintf(sf_path, sizeof(sf_path), "%s/%s", plugin->bundle_path, SF2_FILE);
    fprintf(stderr, "Loading soundfont from: %s\n", sf_path);
    
    // Load the SoundFont
    plugin->sfont_id = fluid_synth_sfload(plugin->synth, sf_path, 1);
    if (plugin->sfont_id == FLUID_FAILED) {
        fprintf(stderr, "Failed to load SoundFont: %s\n", sf_path);
        return -1;
    }

    // Get the SoundFont instance
    fluid_sfont_t* sfont = fluid_synth_get_sfont(plugin->synth, 0);
    if (!sfont) {
        fprintf(stderr, "Failed to get soundfont instance\n");
        return -1;
    }

    // Count total available presets (including bank 128)
    size_t preset_count = 0;
    for (int bank = 0; bank <= 128; bank++) {
        for (int prog = 0; prog < 128; prog++) {
            if (fluid_sfont_get_preset(sfont, bank, prog) != NULL) {
                preset_count++;
            }
        }
    }

    fprintf(stderr, "Found %zu total presets in soundfont\n", preset_count);
    plugin->program_count = preset_count;

    // Allocate memory for preset storage
    plugin->programs = (BankProgram*)calloc(preset_count, sizeof(BankProgram));
    if (!plugin->programs) {
        fprintf(stderr, "Failed to allocate program array\n");
        return -1;
    }

    // Store all bank/program pairs in the same order as main.c
    int idx = 0;
    for (int bank = 0; bank <= 128; bank++) {
        for (int prog = 0; prog < 128; prog++) {
            fluid_preset_t* preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                plugin->programs[idx].bank = bank;
                plugin->programs[idx].prog = prog;
                fprintf(stderr, "Stored program %d: bank=%d prog=%d name=%s\n",
                        idx, bank, prog, fluid_preset_get_name(preset));
                idx++;
            }
        }
    }
    
    return plugin->sfont_id;
}

/*
 * Map URIs for MIDI event handling
 */
static void map_uris(LV2_URID_Map* map, URIDs* uris) {
    uris->midi_Event = map->map(map->handle, LV2_MIDI__MidiEvent);
}

/*
 * Handle program changes with proper bank selection
 */
static void handle_program_change(Plugin* plugin, int program) {
    if (program < 0 || program >= plugin->program_count) {
        fprintf(stderr, "Invalid program number: %d (max: %d)\n", 
                program, plugin->program_count - 1);
        return;
    }

    // Reset all notes and sounds
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);

    int bank = plugin->programs[program].bank;
    int prog = plugin->programs[program].prog;

    fprintf(stderr, "Changing to program %d (bank:%d prog:%d)\n", 
            program, bank, prog);

    // Send bank select first
    fluid_synth_bank_select(plugin->synth, 0, bank);
    
    // Ensure bank change is processed
    fluid_synth_sfont_select(plugin->synth, 0, plugin->sfont_id);
    
    // Then send program change
    int result = fluid_synth_program_change(plugin->synth, 0, prog);
    
    if (result != FLUID_OK) {
        fprintf(stderr, "Failed to change program: bank=%d prog=%d\n", bank, prog);
    }

    // Reset all CC values to 0
    fluid_synth_cc(plugin->synth, 0, CC_CUTOFF, 0);
    fluid_synth_cc(plugin->synth, 0, CC_RESONANCE, 0);
    fluid_synth_cc(plugin->synth, 0, CC_ATTACK, 0);
    fluid_synth_cc(plugin->synth, 0, CC_DECAY, 0);
    fluid_synth_cc(plugin->synth, 0, CC_SUSTAIN, 0);
    fluid_synth_cc(plugin->synth, 0, CC_RELEASE, 0);

    // Reset control states
    memset(plugin->controls_touched, 0, sizeof(plugin->controls_touched));
    plugin->preset_just_changed = true;
}

/*
 * Initialize a new instance of the plugin
 */
LV2_Handle instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    fprintf(stderr, "Instantiating plugin: %s\n", PLUGIN_DISPLAY_NAME);
    
    // Allocate and initialize the plugin structure
    Plugin* plugin = (Plugin*)calloc(1, sizeof(Plugin));
    if (!plugin) {
        return NULL;
    }
    
    // Get host features
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            plugin->map = (LV2_URID_Map*)features[i]->data;
        }
    }

    if (!plugin->map) {
        fprintf(stderr, "Missing required feature urid:map\n");
        free(plugin);
        return NULL;
    }

    // Initialize URIs and basic plugin data
    map_uris(plugin->map, &plugin->urids);
    plugin->bundle_path = strdup(bundle_path);
    plugin->rate = rate;
    
    // Initialize FluidSynth settings
    plugin->settings = new_fluid_settings();
    if (!plugin->settings) {
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    // Configure FluidSynth settings for optimal performance
    fluid_settings_setint(plugin->settings, "synth.threadsafe-api", 1);
    fluid_settings_setint(plugin->settings, "audio.period-size", 256);
    fluid_settings_setint(plugin->settings, "audio.periods", 2);
    fluid_settings_setnum(plugin->settings, "synth.sample-rate", rate);
    fluid_settings_setint(plugin->settings, "synth.cpu-cores", 4);
    fluid_settings_setint(plugin->settings, "synth.polyphony", 16);
    fluid_settings_setint(plugin->settings, "synth.reverb.active", 0);
    fluid_settings_setint(plugin->settings, "synth.chorus.active", 0);
    
    // Create FluidSynth instance
    plugin->synth = new_fluid_synth(plugin->settings);
    if (!plugin->synth) {
        delete_fluid_settings(plugin->settings);
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    // Load and initialize the SoundFont
    if (load_soundfont(plugin) < 0) {
        delete_fluid_synth(plugin->synth);
        delete_fluid_settings(plugin->settings);
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    // Initialize all CC values to 0
    fluid_synth_cc(plugin->synth, 0, CC_CUTOFF, 0);
    fluid_synth_cc(plugin->synth, 0, CC_RESONANCE, 0);
    fluid_synth_cc(plugin->synth, 0, CC_ATTACK, 0);
    fluid_synth_cc(plugin->synth, 0, CC_DECAY, 0);
    fluid_synth_cc(plugin->synth, 0, CC_SUSTAIN, 0);
    fluid_synth_cc(plugin->synth, 0, CC_RELEASE, 0);
    
    // Allocate audio processing buffers
    plugin->buffer_l = (float*)calloc(BUFFER_SIZE, sizeof(float));
    plugin->buffer_r = (float*)calloc(BUFFER_SIZE, sizeof(float));
    if (!plugin->buffer_l || !plugin->buffer_r) {
        if (plugin->buffer_l) free(plugin->buffer_l);
        if (plugin->buffer_r) free(plugin->buffer_r);
        delete_fluid_synth(plugin->synth);
        delete_fluid_settings(plugin->settings);
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    // Initialize plugin state
    plugin->current_program = -1;
    plugin->last_cutoff = -1;
    plugin->last_resonance = -1;
    plugin->controls_initialized = false;
    plugin->preset_just_changed = false;
    memset(plugin->controls_touched, 0, sizeof(plugin->controls_touched));
    
    fprintf(stderr, "Plugin instantiated successfully\n");
    return (LV2_Handle)plugin;
}

/*
 * Connect plugin ports to host buffers
 */
void connect_port(LV2_Handle instance,
            uint32_t port,
            void* data)
{
    Plugin* plugin = (Plugin*)instance;

    switch (port) {
        case PORT_EVENTS:
            plugin->events_in = (const LV2_Atom_Sequence*)data;
            break;
        case PORT_AUDIO_OUT_L:
            plugin->audio_out_l = (float*)data;
            break;
        case PORT_AUDIO_OUT_R:
            plugin->audio_out_r = (float*)data;
            break;
        case PORT_LEVEL:
            plugin->level_port = (float*)data;
            break;
        case PORT_PROGRAM:
            plugin->program_port = (float*)data;
            break;
        case PORT_CUTOFF:
            plugin->cutoff_port = (float*)data;
            break;
        case PORT_RESONANCE:
            plugin->resonance_port = (float*)data;
            break;
        case PORT_ATTACK:
            plugin->attack_port = (float*)data;
            break;
        case PORT_DECAY:
            plugin->decay_port = (float*)data;
            break;
        case PORT_SUSTAIN:
            plugin->sustain_port = (float*)data;
            break;
        case PORT_RELEASE:
            plugin->release_port = (float*)data;
            break;
    }
}

/*
 * Activate plugin (prepare for audio processing)
 */
void activate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

/*
 * Process audio and handle events for one cycle
 */
void run(LV2_Handle instance,
    uint32_t sample_count)
{
    Plugin* plugin = (Plugin*)instance;

    // Initialize controls if needed
    if (!plugin->controls_initialized && !plugin->preset_just_changed) {
        plugin->prev_cutoff = *plugin->cutoff_port;
        plugin->prev_resonance = *plugin->resonance_port;
        plugin->prev_attack = *plugin->attack_port;
        plugin->prev_decay = *plugin->decay_port;
        plugin->prev_sustain = *plugin->sustain_port;
        plugin->prev_release = *plugin->release_port;
        plugin->controls_initialized = true;
    }
    
    // Handle control changes
    if (plugin->controls_initialized) {
        // Cutoff (inverted control)
        if (*plugin->cutoff_port != plugin->prev_cutoff) {
            plugin->controls_touched[0] = true;
            plugin->prev_cutoff = *plugin->cutoff_port;
            if (plugin->controls_touched[0]) {
                float inverted_cutoff = 1.0f - *plugin->cutoff_port;
                fluid_synth_cc(plugin->synth, 0, CC_CUTOFF, (int)(inverted_cutoff * 127.0f));
            }
        }

        // Resonance
        if (*plugin->resonance_port != plugin->prev_resonance) {
            plugin->controls_touched[1] = true;
            plugin->prev_resonance = *plugin->resonance_port;
            if (plugin->controls_touched[1]) {
                fluid_synth_cc(plugin->synth, 0, CC_RESONANCE, (int)(*plugin->resonance_port * 127.0f));
            }
        }

        // Attack
        if (*plugin->attack_port != plugin->prev_attack) {
            plugin->controls_touched[2] = true;
            plugin->prev_attack = *plugin->attack_port;
            if (plugin->controls_touched[2]) {
                fluid_synth_cc(plugin->synth, 0, CC_ATTACK, (int)(*plugin->attack_port * 127.0f));
            }
        }

        // Decay
        if (*plugin->decay_port != plugin->prev_decay) {
            plugin->controls_touched[3] = true;
            plugin->prev_decay = *plugin->decay_port;
            if (plugin->controls_touched[3]) {
                fluid_synth_cc(plugin->synth, 0, CC_DECAY, (int)(*plugin->decay_port * 127.0f));
            }
        }

        // Sustain
        if (*plugin->sustain_port != plugin->prev_sustain) {
            plugin->controls_touched[4] = true;
            plugin->prev_sustain = *plugin->sustain_port;
            if (plugin->controls_touched[4]) {
                fluid_synth_cc(plugin->synth, 0, CC_SUSTAIN, (int)(*plugin->sustain_port * 127.0f));
            }
        }

        // Release
        if (*plugin->release_port != plugin->prev_release) {
            plugin->controls_touched[5] = true;
            plugin->prev_release = *plugin->release_port;
            if (plugin->controls_touched[5]) {
                fluid_synth_cc(plugin->synth, 0, CC_RELEASE, (int)(*plugin->release_port * 127.0f));
            }
        }
    }
    
    plugin->preset_just_changed = false;
    
    // Handle program changes
    if (plugin->program_port) {
        int new_program = (int)(*plugin->program_port + 0.5);
        if (new_program != plugin->current_program && new_program >= 0) {
            handle_program_change(plugin, new_program);
            plugin->current_program = new_program;
            
            // Update previous values to match current port values
            plugin->prev_cutoff = *plugin->cutoff_port;
            plugin->prev_resonance = *plugin->resonance_port;
            plugin->prev_attack = *plugin->attack_port;
            plugin->prev_decay = *plugin->decay_port;
            plugin->prev_sustain = *plugin->sustain_port;
            plugin->prev_release = *plugin->release_port;
        }
    }

    // Handle level control
    if (plugin->level_port) {
        float level = *plugin->level_port;
        fluid_synth_set_gain(plugin->synth, level);
    }

    // Process MIDI events
    LV2_ATOM_SEQUENCE_FOREACH(plugin->events_in, ev) {
        if (ev->body.type == plugin->urids.midi_Event) {
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            switch (msg[0] & 0xF0) {
                case 0x90:  // Note On
                    if (msg[2] > 0) {
                        fluid_synth_noteon(plugin->synth, 0, msg[1], msg[2]);
                    } else {
                        fluid_synth_noteoff(plugin->synth, 0, msg[1]);
                    }
                    break;
                case 0x80:  // Note Off
                    fluid_synth_noteoff(plugin->synth, 0, msg[1]);
                    break;
                case 0xB0:  // Control Change
                    fluid_synth_cc(plugin->synth, 0, msg[1], msg[2]);
                    break;
                case 0xE0:  // Pitch Bend
                    fluid_synth_pitch_bend(plugin->synth, 0,
                        ((msg[2] << 7) | msg[1]) - 8192);
                    break;
            }
        }
    }

    // Process audio
    uint32_t remaining = sample_count;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint32_t chunk_size = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;

        fluid_synth_write_float(plugin->synth, chunk_size,
                              plugin->buffer_l, 0, 1,
                              plugin->buffer_r, 0, 1);

        memcpy(plugin->audio_out_l + offset, plugin->buffer_l, chunk_size * sizeof(float));
        memcpy(plugin->audio_out_r + offset, plugin->buffer_r, chunk_size * sizeof(float));

        remaining -= chunk_size;
        offset += chunk_size;
    }
}

/*
 * Deactivate plugin (stop audio processing)
 */
void deactivate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

/*
 * Cleanup plugin instance
 */
void cleanup(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    
    if (plugin) {
        if (plugin->buffer_l) free(plugin->buffer_l);
        if (plugin->buffer_r) free(plugin->buffer_r);
        if (plugin->programs) free(plugin->programs);
        if (plugin->synth) delete_fluid_synth(plugin->synth);
        if (plugin->settings) delete_fluid_settings(plugin->settings);
        if (plugin->bundle_path) free(plugin->bundle_path);
        free(plugin);
    }
}

/*
 * Extension data (none implemented)
 */
const void* extension_data(const char* uri)
{
    return NULL;
}

/*
 * Plugin descriptor
 */
static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

/*
 * Return plugin descriptor
 */
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : NULL;
}

// ... rest of the code ...
