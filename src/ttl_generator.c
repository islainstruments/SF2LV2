/*
 * SF2LV2 - SoundFont to LV2 Plugin Generator
 * TTL Generator (ttl_generator.c)
 *
 * This program:
 * 1. Loads a SoundFont file using FluidSynth
 * 2. Scans all available presets (including bank 128 for drum kits)
 * 3. Generates LV2 TTL files describing the plugin interface
 * 4. Creates a manifest file for LV2 plugin discovery
 */

#include <fluidsynth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Plugin name should be defined at compile time using the make command, defaults to "undefined" */
#ifndef PLUGIN_NAME
#define PLUGIN_NAME "undefined"
#endif

/* Structure to store bank/program mapping information */
struct PresetMapping {
    int bank;           // MIDI bank number
    int prog;           // MIDI program number
    const char* name;   // Preset name from SoundFont
};

/* Sanitize names for use in filenames and URIs */
void sanitize_name(char* name) {
    for (int i = 0; name[i]; ++i) {
        if (name[i] == ' ' || name[i] == '-' || name[i] == '.') {
            name[i] = '_';
        }
    }
}

/* Utility function to copy files with error checking */
void copy_file(const char* src_path, const char* dst_path) {
    FILE* src = fopen(src_path, "rb");
    if (!src) {
        perror("Failed to open source file");
        exit(1);
    }

    FILE* dst = fopen(dst_path, "wb");
    if (!dst) {
        perror("Failed to open destination file");
        fclose(src);
        exit(1);
    }

    char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dst) != bytes) {
            perror("Failed to write to destination file");
            fclose(src);
            fclose(dst);
            exit(1);
        }
    }

    fclose(src);
    fclose(dst);
}

int main(int argc, char** argv) {
    fprintf(stderr, "Starting SF2LV2 generator...\n");
    
    // Check command line arguments
    if (argc < 2) {
        printf("Usage: %s <soundfont.sf2>\n", argv[0]);
        return 1;
    }

    // Process SoundFont filename
    char soundfont_name[256];
    strncpy(soundfont_name, argv[1], 255);
    soundfont_name[255] = '\0';

    // Extract base name without path and extension
    char* base_name = strrchr(soundfont_name, '/');
    base_name = base_name ? base_name + 1 : soundfont_name;
    char* ext = strrchr(base_name, '.');
    if (ext) *ext = '\0';

    // Store original name before sanitization
    char display_name[256];
    strncpy(display_name, base_name, 255);
    display_name[255] = '\0';

    // Create sanitized name for URIs and filenames
    sanitize_name(base_name);

    // Set up output directory structure
    char output_dir[4096];
    snprintf(output_dir, sizeof(output_dir), "build/%s.lv2", PLUGIN_NAME);

    // Create build directory if it doesn't exist
    if (mkdir("build", 0777) != 0 && errno != EEXIST) {
        perror("Failed to create 'build' directory");
        return 1;
    }

    // Create plugin directory if it doesn't exist
    if (mkdir(output_dir, 0777) != 0 && errno != EEXIST) {
        perror("Failed to create plugin directory");
        return 1;
    }

    // Initialize FluidSynth
    fluid_settings_t* settings = new_fluid_settings();
    fluid_synth_t* synth = new_fluid_synth(settings);
    
    // Load the SoundFont file
    int sfont_id = fluid_synth_sfload(synth, argv[1], 1);
    if (sfont_id == FLUID_FAILED) {
        fprintf(stderr, "Failed to load SoundFont: %s\n", argv[1]);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Get SoundFont instance
    fluid_sfont_t* sfont = fluid_synth_get_sfont(synth, 0);
    if (!sfont) {
        fprintf(stderr, "Failed to get soundfont instance\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Count total available presets
    int total_presets = 0;
    int bank, prog;
    fluid_preset_t* preset;

    // Scan all banks (including bank 128 for drum kits)
    for (bank = 0; bank <= 128; bank++) {
        for (prog = 0; prog < 128; prog++) {
            preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                total_presets++;
            }
        }
    }

    if (total_presets == 0) {
        fprintf(stderr, "No presets found in soundfont\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }
    fprintf(stderr, "Found %d total presets\n", total_presets);

    // Prepare output files
    char ttl_path[4096], manifest_path[4096];
    if (snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", output_dir, PLUGIN_NAME) >= sizeof(ttl_path)) {
        fprintf(stderr, "Path too long for TTL file\n");
        return 1;
    }
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.ttl", output_dir) >= sizeof(manifest_path)) {
        fprintf(stderr, "Path too long for manifest file\n");
        return 1;
    }

    // Open main TTL file
    FILE* ttl = fopen(ttl_path, "w");
    if (!ttl) {
        perror("Failed to open plugin TTL file");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Write TTL prefix definitions
    fprintf(ttl,
        "@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
        "@prefix doap: <http://usefulinc.com/ns/doap#> .\n"
        "@prefix foaf: <http://xmlns.com/foaf/0.1/> .\n"
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
    );

    // Write plugin definition
    fprintf(ttl,
        "<https://github.com/bradholland/sf2lv2/%s>\n"
        "    a lv2:InstrumentPlugin, lv2:Plugin ;\n"
        "    lv2:requiredFeature <http://lv2plug.in/ns/ext/urid#map> ;\n"
        "    lv2:port [\n"
        "        a lv2:InputPort, atom:AtomPort ;\n"
        "        atom:bufferType atom:Sequence ;\n"
        "        atom:supports <http://lv2plug.in/ns/ext/midi#MidiEvent> ;\n"
        "        lv2:designation lv2:control ;\n"
        "        lv2:index 0 ;\n"
        "        lv2:symbol \"events\" ;\n"
        "        lv2:name \"Events\" ;\n"
        "    ] , [\n"
        "        a lv2:OutputPort, lv2:AudioPort ;\n"
        "        lv2:index 1 ;\n"
        "        lv2:symbol \"audio_out_l\" ;\n"
        "        lv2:name \"Audio Output Left\" ;\n"
        "    ] , [\n"
        "        a lv2:OutputPort, lv2:AudioPort ;\n"
        "        lv2:index 2 ;\n"
        "        lv2:symbol \"audio_out_r\" ;\n"
        "        lv2:name \"Audio Output Right\" ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 3 ;\n"
        "        lv2:symbol \"level\" ;\n"
        "        lv2:name \"Level\" ;\n"
        "        lv2:default 1.0 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 2.0 ;\n"
        "    ] , [\n",
        PLUGIN_NAME
    );

    // Add Program Control Port definition
    fprintf(ttl,
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 4 ;\n"
        "        lv2:symbol \"program\" ;\n"
        "        lv2:name \"Program\" ;\n"
        "        lv2:portProperty lv2:enumeration, lv2:integer ;\n"
        "        lv2:default 0 ;\n"
        "        lv2:minimum 0 ;\n"
        "        lv2:maximum %d ;\n"
        "        lv2:scalePoint [\n",
        total_presets - 1
    );

    // Allocate memory for preset mappings
    struct PresetMapping* preset_mappings = malloc(total_presets * sizeof(struct PresetMapping));
    if (!preset_mappings) {
        fprintf(stderr, "Failed to allocate preset mapping memory\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Store all preset mappings
    int mapping_index = 0;
    fprintf(stderr, "\nAvailable presets:\n");
    for (bank = 0; bank <= 128; bank++) {
        for (prog = 0; prog < 128; prog++) {
            preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                preset_mappings[mapping_index].bank = bank;
                preset_mappings[mapping_index].prog = prog;
                preset_mappings[mapping_index].name = fluid_preset_get_name(preset);
                // Print first column with Isla Instruments colors
                fprintf(stderr, "  \033[1;37m%3d\033[0m: [\033[1;31m%3d,%3d\033[0m] \033[0;37m%-24s\033[0m", 
                        mapping_index, bank, prog, preset_mappings[mapping_index].name);
                // If this is an even-numbered preset and not the last one, print a separator
                if (mapping_index % 2 == 0) {
                    fprintf(stderr, "\033[1;30m|\033[0m ");
                } else {
                    fprintf(stderr, "\n");
                }
                mapping_index++;
            }
        }
    }
    // Add a newline if we ended on an even-numbered preset
    if ((mapping_index - 1) % 2 == 0) {
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    // Write preset information to TTL
    for (int i = 0; i < total_presets; i++) {
        fprintf(ttl,
            "            rdfs:label \"%s\" ;\n"
            "            rdf:value %d\n",
            preset_mappings[i].name, i
        );

        if (i < total_presets - 1) {
            fprintf(ttl, "        ] , [\n");
        }
    }

    // Add control ports
    fprintf(ttl,
        "        ]\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 5 ;\n"
        "        lv2:symbol \"cutoff\" ;\n"
        "        lv2:name \"Cutoff\" ;\n"
        "        lv2:default 0.5 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 6 ;\n"
        "        lv2:symbol \"resonance\" ;\n"
        "        lv2:name \"Resonance\" ;\n"
        "        lv2:default 0.5 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 7 ;\n"
        "        lv2:symbol \"attack\" ;\n"
        "        lv2:name \"Attack\" ;\n"
        "        lv2:default 0.0 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 8 ;\n"
        "        lv2:symbol \"decay\" ;\n"
        "        lv2:name \"Decay\" ;\n"
        "        lv2:default 0.0 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 9 ;\n"
        "        lv2:symbol \"sustain\" ;\n"
        "        lv2:name \"Sustain\" ;\n"
        "        lv2:default 0.0 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] , [\n"
        "        a lv2:InputPort, lv2:ControlPort ;\n"
        "        lv2:index 10 ;\n"
        "        lv2:symbol \"release\" ;\n"
        "        lv2:name \"Release\" ;\n"
        "        lv2:default 0.0 ;\n"
        "        lv2:minimum 0.0 ;\n"
        "        lv2:maximum 1.0 ;\n"
        "    ] ;\n"
    );

    // Write plugin metadata
    fprintf(ttl,
        "    doap:name \"%s\" ;\n"
        "    doap:license \"MIT\" ;\n"
        "    doap:maintainer [\n"
        "        foaf:name \"Isla Instruments\" ;\n"
        "        foaf:homepage <https://www.islainstruments.com> ;\n"
        "    ] ;\n"
        "    rdfs:comment \"This plugin wraps the %s soundfont as an LV2 instrument.\\nBuilt using FluidSynth as the synthesizer engine.\" ;\n"
        "    lv2:minorVersion 2 ;\n"
        "    lv2:microVersion 0 .\n",
        display_name, display_name
    );

    fclose(ttl);

    // Write manifest.ttl
    FILE* manifest = fopen(manifest_path, "w");
    if (manifest) {
        fprintf(manifest,
            "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
            "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
            "<https://github.com/islainstruments/sf2lv2/%s>\n"
            "    a lv2:Plugin ;\n"
            "    lv2:binary <%s.so> ;\n"
            "    rdfs:seeAlso <%s.ttl> .\n",
            PLUGIN_NAME, PLUGIN_NAME, PLUGIN_NAME
        );
        fclose(manifest);
    }

    // Cleanup
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
    free(preset_mappings);

    fprintf(stderr, "Successfully generated plugin in %s\n", output_dir);
    return 0;
}
