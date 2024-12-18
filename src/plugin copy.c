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

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "undefined"
#endif

#ifndef SF2_FILE
#define SF2_FILE "undefined.sf2"
#endif

// Let's store these as actual const strings for debugging
static const char* PLUGIN_DISPLAY_NAME = "E-MU Orbit";
#define PLUGIN_URI "https://github.com/bradholland/soundplug/" PLUGIN_NAME
#define BUFFER_SIZE 2048

typedef enum {
    PORT_EVENTS = 0,      // MIDI input
    PORT_AUDIO_OUT_L = 1, // Audio output left
    PORT_AUDIO_OUT_R = 2, // Audio output right
    PORT_LEVEL = 3,       // Level control (0.0 to 2.0)
    PORT_PROGRAM = 4      // Program control (preset changes)
} PortIndex;

typedef struct {
    LV2_URID midi_Event;
} URIDs;

typedef struct {
    // Feature handlers
    LV2_URID_Map* map;
    URIDs urids;

    // Port buffers
    const LV2_Atom_Sequence* events_in;
    float* audio_out_l;
    float* audio_out_r;
    float* level_port;
    float* program_port;

    // FluidSynth
    fluid_settings_t* settings;
    fluid_synth_t* synth;
    int current_program;

    // Instance data
    char* bundle_path;
    float* buffer_l;
    float* buffer_r;
    double rate;
} Plugin;

static int load_soundfont(Plugin* plugin) {
    char sf_path[2048];
    snprintf(sf_path, sizeof(sf_path), "%s/%s", plugin->bundle_path, SF2_FILE);
    fprintf(stderr, "Loading soundfont from: %s\n", sf_path);
    
    int sfont_id = fluid_synth_sfload(plugin->synth, sf_path, 1);
    if (sfont_id == FLUID_FAILED) {
        fprintf(stderr, "Failed to load SoundFont: %s\n", sf_path);
        return -1;
    }
    
    // Verify we can get the soundfont back
    fluid_sfont_t* sfont = fluid_synth_get_sfont(plugin->synth, 0);
    if (!sfont) {
        fprintf(stderr, "Failed to get loaded soundfont\n");
        return -1;
    }
    
    fluid_synth_set_bank_offset(plugin->synth, 0, 0);
    return sfont_id;
}

static void
map_uris(LV2_URID_Map* map, URIDs* uris)
{
    uris->midi_Event = map->map(map->handle, LV2_MIDI__MidiEvent);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    fprintf(stderr, "Instantiating plugin: %s\n", PLUGIN_DISPLAY_NAME);
    fprintf(stderr, "Bundle path: %s\n", bundle_path);
    fprintf(stderr, "Plugin URI: %s\n", PLUGIN_URI);
    fprintf(stderr, "SF2 file: %s\n", SF2_FILE);

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

    // Map URIs
    map_uris(plugin->map, &plugin->urids);
    
    plugin->bundle_path = strdup(bundle_path);
    plugin->rate = rate;
    
    plugin->settings = new_fluid_settings();
    if (!plugin->settings) {
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    fluid_settings_setnum(plugin->settings, "synth.sample-rate", rate);
    fluid_settings_setint(plugin->settings, "synth.audio-channels", 2);
    fluid_settings_setint(plugin->settings, "synth.audio-groups", 2);
    fluid_settings_setint(plugin->settings, "synth.reverb.active", 1);
    fluid_settings_setint(plugin->settings, "synth.chorus.active", 1);
    
    plugin->synth = new_fluid_synth(plugin->settings);
    if (!plugin->synth) {
        delete_fluid_settings(plugin->settings);
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
    if (load_soundfont(plugin) < 0) {
        delete_fluid_synth(plugin->synth);
        delete_fluid_settings(plugin->settings);
        free(plugin->bundle_path);
        free(plugin);
        return NULL;
    }
    
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
    
    plugin->current_program = -1;
    fprintf(stderr, "Plugin instantiated successfully\n");
    return (LV2_Handle)plugin;
}

static void
connect_port(LV2_Handle instance,
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
    }
}

static void
activate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

static void
run(LV2_Handle instance,
    uint32_t sample_count)
{
    Plugin* plugin = (Plugin*)instance;

// Handle program changes (PORT_PROGRAM)
if (plugin->program_port) { 
    int new_program = (int)floor(*plugin->program_port);
    if (new_program != plugin->current_program && new_program >= 0) {
        // Get the total number of presets first
        fluid_sfont_t* sfont = fluid_synth_get_sfont(plugin->synth, 0);
        if (sfont) {
            // Calculate actual bank and program numbers
            int bank = 0;  // Start with bank 0
            int prog = new_program;  // Use program number directly
            if (prog >= 128) {  // If we exceed bank 0's range
                bank = 1;       // Move to bank 1
                prog -= 128;    // Adjust program number
            }
            fluid_preset_t* preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset) {
                fluid_synth_bank_select(plugin->synth, 0, bank);
                fluid_synth_program_change(plugin->synth, 0, prog);
                plugin->current_program = new_program;
                fprintf(stderr, "Program changed to %d (bank:%d prog=%d)\n", 
                        new_program, bank, prog);
            } else {
                fprintf(stderr, "Invalid program number: %d\n", new_program);
            }
        }
    }
}

    // Handle level control (PORT_LEVEL)
    if (plugin->level_port) {
        float level = *plugin->level_port;
        fluid_synth_set_gain(plugin->synth, level);
    }

    // Process incoming MIDI events
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

static void
deactivate(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    fluid_synth_all_notes_off(plugin->synth, -1);
    fluid_synth_all_sounds_off(plugin->synth, -1);
}

static void
cleanup(LV2_Handle instance)
{
    Plugin* plugin = (Plugin*)instance;
    
    if (plugin) {
        if (plugin->buffer_l) free(plugin->buffer_l);
        if (plugin->buffer_r) free(plugin->buffer_r);
        if (plugin->synth) delete_fluid_synth(plugin->synth);
        if (plugin->settings) delete_fluid_settings(plugin->settings);
        if (plugin->bundle_path) free(plugin->bundle_path);
        free(plugin);
    }
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

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

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    fprintf(stderr, "lv2_descriptor called with index: %d\n", index);
    return (index == 0) ? &descriptor : NULL;
}