#include <fluidsynth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "undefined"
#endif

// Function to sanitize names for filenames and URIs
void sanitize_name(char* name) {
    for (int i = 0; name[i]; ++i) {
        if (name[i] == ' ' || name[i] == '-' || name[i] == '.') {
            name[i] = '_';
        }
    }
}

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
    fprintf(stderr, "Starting soundplug generator...\n");
    if (argc < 2) {
        printf("Usage: %s <soundfont.sf2>\n", argv[0]);
        return 1;
    }

    // Get the soundfont name
    char soundfont_name[256];
    strncpy(soundfont_name, argv[1], 255);
    soundfont_name[255] = '\0';

    // Strip path and extension
    char* base_name = strrchr(soundfont_name, '/');
    base_name = base_name ? base_name + 1 : soundfont_name;
    char* ext = strrchr(base_name, '.');
    if (ext) *ext = '\0';

    // Store display name before sanitization
    char display_name[256];
    strncpy(display_name, base_name, 255);
    display_name[255] = '\0';

    // Sanitize the name for URI and filenames
    sanitize_name(base_name);

    // Create the output directory
    char output_dir[2048];
    snprintf(output_dir, sizeof(output_dir), "builds/%s.lv2", PLUGIN_NAME);

    if (mkdir("builds", 0777) != 0 && errno != EEXIST) {
        perror("Failed to create 'builds' directory");
        return 1;
    }

    if (mkdir(output_dir, 0777) != 0 && errno != EEXIST) {
        perror("Failed to create plugin directory");
        return 1;
    }

    // Copy SF2 to bundle directory
    char sf2_dest[2048];
    snprintf(sf2_dest, sizeof(sf2_dest), "%s/%s", output_dir, argv[1]);
    copy_file(argv[1], sf2_dest);

    // Initialize FluidSynth and load soundfont
    fluid_settings_t* settings = new_fluid_settings();
    fluid_synth_t* synth = new_fluid_synth(settings);
    fprintf(stderr, "FluidSynth settings and synth instance created\n"); 
    int sfont_id = fluid_synth_sfload(synth, argv[1], 1);
    if (sfont_id == FLUID_FAILED) {
        printf("Failed to load SoundFont\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    fluid_sfont_t* sfont = fluid_synth_get_sfont(synth, 0);
        if (!sfont) {
        fprintf(stderr, "Failed to get soundfont\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Count total presets
    int total_presets = 0;
    int bank, prog;
    fluid_preset_t* preset;

    fprintf(stderr, "Starting preset enumeration...\n");
    for (bank = 0; bank < 128; bank++) {
        for (prog = 0; prog < 128; prog++) {
            preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                total_presets++;
                fprintf(stderr, "Found preset bank:%d prog:%d\n", bank, prog);
            }
        }
    }

    if (total_presets == 0) {
        fprintf(stderr, "No presets found in soundfont\n");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }
    fprintf(stderr, "Total presets found: %d\n", total_presets);

    // Open files for writing
    
    char ttl_path[2048], manifest_path[2048];
    snprintf(ttl_path, sizeof(ttl_path), "%s/%s.ttl", output_dir, PLUGIN_NAME);
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.ttl", output_dir);

fprintf(stderr, "About to open TTL file at: %s\n", ttl_path);


    FILE* ttl = fopen(ttl_path, "w");
    if (!ttl) {
        perror("Failed to open plugin TTL file");
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

// Write prefix definitions
fprintf(stderr, "TTL file opened successfully\n");
fprintf(stderr, "About to write prefix definitions...\n");

int write_result = fprintf(ttl,
    "@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
);
fprintf(stderr, "First line write result: %d\n", write_result);

write_result = fprintf(ttl,
    "@prefix doap: <http://usefulinc.com/ns/doap#> .\n"
);
fprintf(stderr, "Second line write result: %d\n", write_result);

write_result = fprintf(ttl,
    "@prefix foaf: <http://xmlns.com/foaf/0.1/> .\n"
);
fprintf(stderr, "Third line write result: %d\n", write_result);

write_result = fprintf(ttl,
    "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
);
fprintf(stderr, "Fourth line write result: %d\n", write_result);

write_result = fprintf(ttl,
    "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
);
fprintf(stderr, "Fifth line write result: %d\n", write_result);

write_result = fprintf(ttl,
    "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
);
fprintf(stderr, "Sixth line write result: %d\n", write_result);


// Write plugin definition
fprintf(ttl,
    "<https://github.com/bradholland/soundplug/%s>\n"
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

// Add Program Control Port and Preset List
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

       // Write preset information
fprintf(stderr, "Starting to write TTL preset entries...\n");
    int preset_index = 0;
    for (bank = 0; bank < 128; bank++) {
        for (prog = 0; prog < 128; prog++) {
            preset = fluid_sfont_get_preset(sfont, bank, prog);
            if (preset != NULL) {
                const char* name = fluid_preset_get_name(preset);
                if (name) {
                    fprintf(stderr, "Writing preset %d: bank=%d prog=%d name=%s\n", 
                            preset_index, bank, prog, name);
                    fprintf(ttl,
                        "            rdfs:label \"%s\" ;\n"
                        "            rdf:value %d\n",
                        name, preset_index
                    );

                    preset_index++;
                    if (preset_index < total_presets) {
                        fprintf(ttl, "        ] , [\n");
                    }
                }
            }
        }
    }

    // Properly close the scalePoint list and port
    fprintf(ttl, "        ]\n    ] ;\n");

    // Write metadata
    fprintf(ttl,
        "    doap:name \"%s\" ;\n"
        "    doap:license \"LGPL\" ;\n"
        "    doap:maintainer [\n"
        "        foaf:name \"Brad Holland\" ;\n"
        "        foaf:homepage <https://github.com/bradholland> ;\n"
        "    ] ;\n"
        "    rdfs:comment \"This plugin provides the %s soundset as an LV2 instrument.\\nBuilt using FluidSynth for sample playback.\" ;\n"
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
            "<https://github.com/bradholland/soundplug/%s>\n"
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

    printf("Successfully generated plugin in %s\n", output_dir);
    printf("Total presets: %d\n", total_presets);
    return 0;
}
