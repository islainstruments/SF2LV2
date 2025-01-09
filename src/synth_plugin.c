/*
 * SF2LV2 - SoundFont to LV2 Plugin Generator
 * Synthesizer Plugin Runtime (synth_plugin.c)
 *
 * This is the main runtime implementation of the plugin that:
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

// Required LV2 headers for plugin functionality
#include <lv2/core/lv2.h>          // Core LV2 functionality
#include <lv2/atom/atom.h>         // For handling MIDI events
#include <lv2/atom/util.h>         // Utility functions for atom handling
#include <lv2/midi/midi.h>         // MIDI event definitions
#include <lv2/urid/urid.h>         // URI mapping functionality

// FluidSynth header for SoundFont synthesis
#include <fluidsynth.h>

// Standard C library headers
#include <stdlib.h>                // For memory allocation
#include <string.h>                // For string operations
#include <stdio.h>                 // For debug output
#include <math.h>                  // For mathematical operations

/* Plugin name and SF2 file are defined at compile time.
   If not defined, use "undefined" as fallback values */
#ifndef PLUGIN_NAME
#define PLUGIN_NAME "undefined"
#endif

#ifndef SF2_FILE
#define SF2_FILE "undefined.sf2"
#endif

// Display name used for logging and debugging
static const char* PLUGIN_DISPLAY_NAME = PLUGIN_NAME;

// Unique URI for the plugin, used by LV2 hosts to identify the plugin
#define PLUGIN_URI "https://github.com/islainstruments/sf2lv2/" PLUGIN_NAME

// Size of audio processing buffer for FluidSynth
#define BUFFER_SIZE 64

/* MIDI CC numbers for sound parameters - these match standard MIDI CC assignments
   for common synthesizer controls */
#define CC_CUTOFF    74  // Filter cutoff/brightness (Sound Controller 5)
#define CC_RESONANCE 71  // Filter resonance/timbre (Sound Controller 2)
#define CC_ATTACK    73  // Attack time
#define CC_DECAY     75  // Decay time (Sound Controller 6)
#define CC_SUSTAIN   70  // Sustain level (Sound Controller 1)
#define CC_RELEASE   72  // Release time

/* Structure to store bank/program pairs for SoundFont presets.
   Each preset in a SoundFont is identified by a bank and program number */
typedef struct {
    int bank;   // MIDI bank number (0-128)
    int prog;   // MIDI program number (0-127)
} BankProgram;

/* Port indices for the plugin's inputs and outputs.
   These must match the TTL file port definitions */
typedef enum {
    PORT_EVENTS = 0,      // MIDI input port for receiving MIDI messages
    PORT_AUDIO_OUT_L = 1, // Left audio output channel
    PORT_AUDIO_OUT_R = 2, // Right audio output channel
    PORT_LEVEL = 3,       // Master level control (0.0 to 2.0)
    PORT_PROGRAM = 4,     // Program selection (0 to program_count-1)
    PORT_CUTOFF = 5,      // Filter cutoff control (0.0 to 1.0)
    PORT_RESONANCE = 6,   // Filter resonance control (0.0 to 1.0)
    PORT_ATTACK = 7,      // Envelope attack control (0.0 to 1.0)
    PORT_DECAY = 8,       // Envelope decay control (0.0 to 1.0)
    PORT_SUSTAIN = 9,     // Envelope sustain control (0.0 to 1.0)
    PORT_RELEASE = 10     // Envelope release control (0.0 to 1.0)
} PortIndex;

/* Structure for URID (URI to integer ID) mapping.
   LV2 uses URIs to identify different types of data.
   These are mapped to integers for efficiency during runtime */
typedef struct {
    LV2_URID midi_Event;  // Integer ID for MIDI event type URI
} URIDs;

/* Main plugin instance structure.
   Contains all state and data needed for plugin operation */
typedef struct {
    // LV2 host features
    LV2_URID_Map* map;    // Host-provided URID mapping feature
    URIDs urids;          // Our mapped URIDs for event handling

    // Port connections - pointers to host-provided buffers
    const LV2_Atom_Sequence* events_in;  // Buffer for incoming MIDI events
    float* audio_out_l;    // Buffer for left channel audio output
    float* audio_out_r;    // Buffer for right channel audio output
    float* level_port;     // Control value for master level
    float* program_port;   // Control value for program selection
    float* cutoff_port;    // Control value for filter cutoff
    float* resonance_port; // Control value for filter resonance
    float* attack_port;    // Control value for envelope attack
    float* decay_port;     // Control value for envelope decay
    float* sustain_port;   // Control value for envelope sustain
    float* release_port;   // Control value for envelope release

    // Debug flag for logging
    bool debug;           // When true, outputs debug information to stderr

    // FluidSynth engine instances and state
    fluid_settings_t* settings;  // FluidSynth configuration settings
    fluid_synth_t* synth;       // FluidSynth synthesizer instance
    int current_program;        // Currently selected program number
    BankProgram* programs;      // Array of available program bank/number pairs
    int sfont_id;              // ID of loaded SoundFont
    int program_count;         // Total number of available programs

    // Audio processing buffers
    char* bundle_path;     // Path to plugin's resource directory
    float* buffer_l;       // Temporary buffer for left channel processing
    float* buffer_r;       // Temporary buffer for right channel processing
    double rate;          // Audio sample rate in Hz

    // Parameter change tracking
    float prev_cutoff;     // Previous value of cutoff control
    float prev_resonance;  // Previous value of resonance control
    float prev_attack;     // Previous value of attack control
    float prev_decay;      // Previous value of decay control
    float prev_sustain;    // Previous value of sustain control
    float prev_release;    // Previous value of release control
} Plugin;

/*
 * Load and initialize the SoundFont file.
 * This function:
 * 1. Loads the specified SoundFont file
 * 2. Scans it for available presets
 * 3. Stores preset information for program changes
 * Returns: The SoundFont ID if successful, -1 on failure
 */
static int load_soundfont(Plugin* plugin) {
    // Construct the full path to the SoundFont file by combining bundle path and filename
    char sf_path[4096];
    snprintf(sf_path, sizeof(sf_path), "%s/%s", plugin->bundle_path, SF2_FILE);
    if (plugin->debug) {
        fprintf(stderr, "Loading soundfont from: %s\n", sf_path);
    }
    
    // Load the SoundFont into FluidSynth (1 = reset presets)
    plugin->sfont_id = fluid_synth_sfload(plugin->synth, sf_path, 1);
    if (plugin->sfont_id == FLUID_FAILED) {
        fprintf(stderr, "Failed to load SoundFont: %s\n", sf_path);
        return -1;
    }

    // Get a handle to the loaded SoundFont for preset scanning
    fluid_sfont_t* sfont = fluid_synth_get_sfont(plugin->synth, 0);
    if (!sfont) {
        fprintf(stderr, "Failed to get soundfont instance\n");
        return -1;
    }

    // First pass: Count total available presets across all banks
    size_t preset_count = 0;
    for (int bank = 0; bank <= 128; bank++) {          // Bank 128 is percussion
        for (int prog = 0; prog < 128; prog++) {       // Programs 0-127 in each bank
            if (fluid_sfont_get_preset(sfont, bank, prog) != NULL) {
                preset_count++;                         // Count each valid preset
            }
        }
    }

    if (plugin->debug) {
        fprintf(stderr, "Found %zu total presets in soundfont\n", preset_count);
    }
    plugin->program_count = preset_count;

    // Allocate memory to store information about each preset
    plugin->programs = (BankProgram*)calloc(preset_count, sizeof(BankProgram));
    if (!plugin->programs) {
        fprintf(stderr, "Failed to allocate program array\n");
        return -1;
    }

    // Second pass: Store bank/program numbers for each preset
    int idx = 0;
    for (int bank = 0; bank <= 128; bank++) {
        for (int prog = 0; prog < 128; prog++) {
            fluid_preset_t* preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                // Store the bank and program numbers for this preset
                plugin->programs[idx].bank = bank;
                plugin->programs[idx].prog = prog;
                if (plugin->debug) {
                    fprintf(stderr, "Stored program %d: bank=%d prog=%d name=%s\n",
                            idx, bank, prog, fluid_preset_get_name(preset));
                }
                idx++;
            }
        }
    }
    
    return plugin->sfont_id;  // Return the SoundFont ID for success
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
        if (plugin->debug) {
            fprintf(stderr, "Invalid program number: %d (max: %d)\n", 
                    program, plugin->program_count - 1);
        }
        return;
    }

    // Reset all notes and sounds
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);

    int bank = plugin->programs[program].bank;
    int prog = plugin->programs[program].prog;

    if (plugin->debug) {
        fprintf(stderr, "Changing to program %d (bank:%d prog:%d)\n", 
                program, bank, prog);
    }

    // Reset CCs (cutoff to max, others to 0)
    fluid_synth_cc(plugin->synth, 0, CC_CUTOFF, 127);    // Cutoff fully open
    fluid_synth_cc(plugin->synth, 0, CC_RESONANCE, 0);
    fluid_synth_cc(plugin->synth, 0, CC_ATTACK, 0);
    fluid_synth_cc(plugin->synth, 0, CC_DECAY, 0);
    fluid_synth_cc(plugin->synth, 0, CC_SUSTAIN, 0);
    fluid_synth_cc(plugin->synth, 0, CC_RELEASE, 0);

    // Send bank select first
    fluid_synth_bank_select(plugin->synth, 0, bank);
    
    // Then send program change
    int result = fluid_synth_program_change(plugin->synth, 0, prog);
    
    if (result != FLUID_OK) {
        if (plugin->debug) {
            fprintf(stderr, "Failed to change program: bank=%d prog=%d\n", bank, prog);
        }
    }

    if (plugin->debug) {
        // Debug output showing FluidSynth CC values
        int cc_value;
        fprintf(stderr, "CC values after program change:\n");
        
        fluid_synth_get_cc(plugin->synth, 0, CC_CUTOFF, &cc_value);
        fprintf(stderr, "  Cutoff (CC%d): %d\n", CC_CUTOFF, cc_value);
        
        fluid_synth_get_cc(plugin->synth, 0, CC_RESONANCE, &cc_value);
        fprintf(stderr, "  Resonance (CC%d): %d\n", CC_RESONANCE, cc_value);
        
        fluid_synth_get_cc(plugin->synth, 0, CC_ATTACK, &cc_value);
        fprintf(stderr, "  Attack (CC%d): %d\n", CC_ATTACK, cc_value);
        
        fluid_synth_get_cc(plugin->synth, 0, CC_DECAY, &cc_value);
        fprintf(stderr, "  Decay (CC%d): %d\n", CC_DECAY, cc_value);
        
        fluid_synth_get_cc(plugin->synth, 0, CC_SUSTAIN, &cc_value);
        fprintf(stderr, "  Sustain (CC%d): %d\n", CC_SUSTAIN, cc_value);
        
        fluid_synth_get_cc(plugin->synth, 0, CC_RELEASE, &cc_value);
        fprintf(stderr, "  Release (CC%d): %d\n", CC_RELEASE, cc_value);
    }
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

    // Initialize debug flag (set to false by default)
    plugin->debug = false;
    
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
    
    // Initialize prev values
    plugin->prev_cutoff = 1.0f;     // Start with cutoff open
    plugin->prev_resonance = 0.0f;
    plugin->prev_attack = 0.0f;
    plugin->prev_decay = 0.0f;
    plugin->prev_sustain = 0.0f;
    plugin->prev_release = 0.0f;
    
    fprintf(stderr, "Plugin instantiated successfully\n");
    return (LV2_Handle)plugin;
}

/*
 * Connect plugin ports to host buffers.
 * Called by the host to connect each port to the appropriate data location.
 * The plugin must store each data location for use in the run() function.
 */
void connect_port(LV2_Handle instance,
            uint32_t port,
            void* data)
{
    Plugin* plugin = (Plugin*)instance;

    // Map each port index to its corresponding plugin member
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
 * Activate plugin for audio processing.
 * Called when the plugin is activated (enabled) by the host.
 * Ensures a clean state by stopping all notes and sounds.
 */
void activate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

/*
 * Process audio and handle events for one cycle.
 * This is the main processing function called by the host for each audio buffer.
 * Handles:
 * 1. Program changes
 * 2. Control parameter updates (only when values change)
 * 3. MIDI event processing
 * 4. Audio generation
 */
void run(LV2_Handle instance, uint32_t sample_count)
{
    Plugin* plugin = (Plugin*)instance;

    // Handle program changes first - if program changes, skip control updates
    if (plugin->program_port) {
        int new_program = (int)(*plugin->program_port + 0.5);
        if (new_program != plugin->current_program && new_program >= 0) {
            handle_program_change(plugin, new_program);
            plugin->current_program = new_program;
            goto process_audio;  // Skip control updates after program change
        }
    }

    // Process control changes - only send CC if control actually moved
    if (plugin->cutoff_port && *plugin->cutoff_port != plugin->prev_cutoff) {
        int cc_value = (int)(*plugin->cutoff_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_CUTOFF, cc_value);
        plugin->prev_cutoff = *plugin->cutoff_port;
    }

    if (plugin->resonance_port && *plugin->resonance_port != plugin->prev_resonance) {
        int cc_value = (int)(*plugin->resonance_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_RESONANCE, cc_value);
        plugin->prev_resonance = *plugin->resonance_port;
    }

    if (plugin->attack_port && *plugin->attack_port != plugin->prev_attack) {
        int cc_value = (int)(*plugin->attack_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_ATTACK, cc_value);
        plugin->prev_attack = *plugin->attack_port;
    }

    if (plugin->decay_port && *plugin->decay_port != plugin->prev_decay) {
        int cc_value = (int)(*plugin->decay_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_DECAY, cc_value);
        plugin->prev_decay = *plugin->decay_port;
    }

    if (plugin->sustain_port && *plugin->sustain_port != plugin->prev_sustain) {
        int cc_value = (int)(*plugin->sustain_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_SUSTAIN, cc_value);
        plugin->prev_sustain = *plugin->sustain_port;
    }

    if (plugin->release_port && *plugin->release_port != plugin->prev_release) {
        int cc_value = (int)(*plugin->release_port * 127.0f);
        fluid_synth_cc(plugin->synth, 0, CC_RELEASE, cc_value);
        plugin->prev_release = *plugin->release_port;
    }

process_audio:
    // Update master level if changed
    if (plugin->level_port) {
        float level = *plugin->level_port;
        fluid_synth_set_gain(plugin->synth, level);
    }

    // Process incoming MIDI events
    LV2_ATOM_SEQUENCE_FOREACH(plugin->events_in, ev) {
        if (ev->body.type == plugin->urids.midi_Event) {
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            switch (msg[0] & 0xF0) {
                case 0x90:  // Note On (velocity > 0) or Note Off (velocity = 0)
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
                case 0xE0:  // Pitch Bend (14-bit value from two 7-bit values)
                    fluid_synth_pitch_bend(plugin->synth, 0,
                        (msg[2] << 7) | msg[1]);
                    break;
            }
        }
    }

    // Generate audio in chunks of BUFFER_SIZE
    uint32_t remaining = sample_count;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint32_t chunk_size = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;

        // Generate audio for current chunk
        fluid_synth_write_float(plugin->synth, chunk_size,
                              plugin->buffer_l, 0, 1,
                              plugin->buffer_r, 0, 1);

        // Copy generated audio to output ports
        memcpy(plugin->audio_out_l + offset, plugin->buffer_l, chunk_size * sizeof(float));
        memcpy(plugin->audio_out_r + offset, plugin->buffer_r, chunk_size * sizeof(float));

        remaining -= chunk_size;
        offset += chunk_size;
    }
}

/*
 * Deactivate plugin (stop audio processing).
 * Called when the plugin is deactivated (disabled) by the host.
 * Ensures all notes and sounds are stopped.
 */
void deactivate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

/*
 * Cleanup plugin instance.
 * Called when the plugin instance is being destroyed.
 * Frees all allocated resources in reverse order of allocation.
 */
void cleanup(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    
    if (plugin) {
        // Free audio buffers
        if (plugin->buffer_l) free(plugin->buffer_l);
        if (plugin->buffer_r) free(plugin->buffer_r);
        
        // Free program data
        if (plugin->programs) free(plugin->programs);
        
        // Delete FluidSynth instances
        if (plugin->synth) delete_fluid_synth(plugin->synth);
        if (plugin->settings) delete_fluid_settings(plugin->settings);
        
        // Free bundle path
        if (plugin->bundle_path) free(plugin->bundle_path);
        
        // Free plugin struct itself
        free(plugin);
    }
}

/*
 * Extension data interface.
 * Returns NULL as this plugin doesn't implement any extensions.
 */
const void* extension_data(const char* uri)
{
    return NULL;
}

/*
 * Plugin descriptor.
 * Contains all function pointers required by the LV2 specification.
 */
static const LV2_Descriptor descriptor = {
    PLUGIN_URI,            // Unique URI identifying the plugin
    instantiate,           // Create new instance of the plugin
    connect_port,          // Connect plugin ports to host buffers
    activate,             // Prepare plugin for audio processing
    run,                  // Process audio and MIDI events
    deactivate,           // Stop audio processing
    cleanup,              // Free plugin resources
    extension_data        // Plugin extensions (none implemented)
};

/*
 * Return plugin descriptor.
 * This is the entry point required by the LV2 specification.
 * Returns the plugin descriptor for index 0, NULL for all other indices.
 */
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : NULL;
}


