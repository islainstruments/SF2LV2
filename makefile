# SF2LV2 - SoundFont to LV2 Plugin Generator
# Makefile for converting SoundFont (.sf2) files into LV2 plugins

# Compiler settings
CC = gcc
CFLAGS = -Wall -fPIC `pkg-config --cflags fluidsynth`
LDFLAGS = `pkg-config --libs fluidsynth`

# Project configuration
# These can be overridden from command line:
# make PLUGIN_NAME=YourPlugin SF2_FILE=YourSound.sf2
PLUGIN_NAME ?= SF2LV2-Default
SF2_FILE ?= soundfont.sf2

# Directory structure
BUILD_DIR = build
PLUGIN_DIR = $(BUILD_DIR)/$(PLUGIN_NAME).lv2
INSTALL_DIR = /usr/lib/lv2

# Source files
METADATA_GEN = src/ttl_generator.c
PLUGIN_SRC = src/synth_plugin.c

# Phony targets (not files)
.PHONY: all clean install

# Default target
all: intro $(PLUGIN_DIR)/$(PLUGIN_NAME).so $(PLUGIN_DIR)/metadata
	@echo "Build complete: $(PLUGIN_DIR)"


# Display intro message
intro:
	@echo "\033[1;31m _  ________        ___    _           _                                  _"
	@echo "| ||  ____  |      /   |  | |         | |                                | |"
	@echo "| || |___ | |     / /| |  | |_ __  ___| |_ _ __ _   _ _ __ __  ___  _ __ | |_ ___"
	@echo "| ||___  || |    / / | |  | | '_ \/ __|  _| '__| | | | '_ '_ \/ _ \| '_ \|  _/ __|"
	@echo "| |____| || |___/ /  | |  | | | | \__ | |_| |  | |_/ | | || | | __/| | | | |_\__ \\"
	@echo "|________||______/   |_|  |_|_| |_|___\\___|_|  \\___,_|_| || |_\\___||_| |_\\___|___/\033[0m"
	@echo "\033[1;37m                     SF2LV2 Soundfont Plugin Creator v1.0 2025-01-06\033[0m"
	@echo 

# Create build directory
$(BUILD_DIR):
	@echo "Creating build directory..."
	@mkdir -p $(BUILD_DIR)

# Create plugin directory
$(PLUGIN_DIR): $(BUILD_DIR)
	@echo "Creating plugin directory..."
	@mkdir -p $(PLUGIN_DIR)

# Build plugin binary
$(PLUGIN_DIR)/$(PLUGIN_NAME).so: $(PLUGIN_SRC) | $(PLUGIN_DIR)
	@echo "Building plugin binary..."
	@$(CC) $(CFLAGS) -shared -DPLUGIN_NAME=\"$(PLUGIN_NAME)\" -DSF2_FILE=\"$(SF2_FILE)\" $< -o $@ $(LDFLAGS)

# Generate metadata
$(PLUGIN_DIR)/metadata: $(METADATA_GEN) $(SF2_FILE) | $(PLUGIN_DIR)
	@echo "Building metadata generator..."
	@$(CC) $(CFLAGS) -DPLUGIN_NAME=\"$(PLUGIN_NAME)\" $< -o $(BUILD_DIR)/ttl_generator $(LDFLAGS)
	@echo "Copying SoundFont and generating metadata..."
	@cp $(SF2_FILE) $(PLUGIN_DIR)/
	@$(BUILD_DIR)/ttl_generator $(SF2_FILE)
	@echo "Cleaning up ttl_generator..."
	@rm -f $(BUILD_DIR)/ttl_generator
	@touch $@

# Install to system LV2 directory
install: all
	@echo "Installing to $(INSTALL_DIR)/$(PLUGIN_NAME).lv2..."
	@sudo mkdir -p $(INSTALL_DIR)/$(PLUGIN_NAME).lv2
	@sudo cp -r $(PLUGIN_DIR)/* $(INSTALL_DIR)/$(PLUGIN_NAME).lv2/
	@echo "Installation complete"

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@rm -f *.ttl
	@echo "Clean complete"