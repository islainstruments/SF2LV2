# Compiler and flags
CC = gcc
CFLAGS = -Wall -fPIC -Iinclude -DPLUGIN_NAME=\"$(PLUGIN_NAME)\" -DSF2_FILE=\"$(SF2)\"
LDFLAGS = -lfluidsynth

# Targets and directories
PLUGIN_NAME ?= E-MU-Orbit
SF2 ?= E-Mu-Orbit.sf2
BUILD_DIR = builds/$(PLUGIN_NAME).lv2
PLUGIN_BINARY = $(BUILD_DIR)/$(PLUGIN_NAME).so

# Target binaries
SOUNDPLUG_BINARY = soundplug

# Default target
all: $(PLUGIN_BINARY)

# Build soundplug (for metadata generation)
$(SOUNDPLUG_BINARY): src/main.c
	$(CC) $(CFLAGS) -Wall -Iinclude src/main.c -o $(SOUNDPLUG_BINARY) -lfluidsynth

# Build the plugin binary
$(PLUGIN_BINARY): src/plugin.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared src/plugin.c -o $@ $(LDFLAGS)

# Generate metadata and copy SF2
metadata: $(SOUNDPLUG_BINARY)
	./$(SOUNDPLUG_BINARY) $(SF2)
	cp $(SF2) $(BUILD_DIR)/

# Full build
build: all metadata

# Clean
clean:
	rm -rf builds/ *.ttl *.so $(SOUNDPLUG_BINARY)

.PHONY: all build metadata clean