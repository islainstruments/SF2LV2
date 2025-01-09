# SF2LV2 - SoundFont to LV2 Plugin Generator
# Makefile for converting SoundFont (.sf2) files into LV2 plugins

# Compiler settings
CC = gcc
CFLAGS = -Wall -fPIC `pkg-config --cflags fluidsynth`
LDFLAGS = `pkg-config --libs fluidsynth`

# Project configuration
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
.PHONY: all clean install interactive build_plugin clean_plugin

# Default target is now interactive
.DEFAULT_GOAL := interactive

# Interactive build process
interactive:
	@clear
	@$(MAKE) --no-print-directory intro
	@echo "\033[1;34m=== SF2LV2 Interactive Build ===\033[0m"
	@echo -n "\033[1;32mWould you like to batch process all available .sf2 files? (y/n): \033[0m"; \
	read batch_choice; \
	if [ "$$batch_choice" = "y" ] || [ "$$batch_choice" = "Y" ]; then \
		echo -n "\033[1;32mInstall all plugins to system after build? (y/n): \033[0m"; \
		read install_choice; \
		for sf2_file in *.sf2; do \
			if [ -f "$$sf2_file" ]; then \
				plugin_name=$${sf2_file%.sf2}; \
				echo "\033[1;34mProcessing: $$sf2_file -> $$plugin_name\033[0m"; \
				$(MAKE) --no-print-directory clean_plugin PLUGIN_NAME="$$plugin_name" > /dev/null; \
				if [ "$$install_choice" = "y" ] || [ "$$install_choice" = "Y" ]; then \
					$(MAKE) --no-print-directory build_plugin PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file" && \
					$(MAKE) --no-print-directory install PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file" || exit 1; \
				else \
					$(MAKE) --no-print-directory build_plugin PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file" || exit 1; \
				fi; \
			fi; \
		done; \
		echo "\033[1;32mBatch processing complete!\033[0m"; \
	else \
		echo -n "\033[1;32mEnter SoundFont filename (.sf2): \033[0m"; \
		read sf2_file; \
		echo -n "\033[1;32mEnter plugin name: \033[0m"; \
		read plugin_name; \
		echo -n "\033[1;32mInstall to system after build? (y/n): \033[0m"; \
		read install_choice; \
		if [ ! -f "$$sf2_file" ]; then \
			echo "\033[1;31mError: SoundFont file '$$sf2_file' not found\033[0m"; \
			exit 1; \
		fi; \
		$(MAKE) --no-print-directory clean_plugin PLUGIN_NAME="$$plugin_name" > /dev/null; \
		if [ "$$install_choice" = "y" ] || [ "$$install_choice" = "Y" ]; then \
			$(MAKE) --no-print-directory build_plugin PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file" && \
			$(MAKE) --no-print-directory install PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file"; \
		else \
			$(MAKE) --no-print-directory build_plugin PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file"; \
		fi \
	fi

# Batch process target
batch_process:
	@echo "\033[1;34m=== Processing all .sf2 files ===\033[0m"
	@for sf2_file in *.sf2; do \
		if [ -f "$$sf2_file" ]; then \
			plugin_name=$${sf2_file%.sf2}; \
			echo "\033[1;32mProcessing: $$sf2_file -> $$plugin_name\033[0m"; \
			$(MAKE) --no-print-directory clean_plugin PLUGIN_NAME="$$plugin_name" > /dev/null; \
			$(MAKE) --no-print-directory build_plugin PLUGIN_NAME="$$plugin_name" SF2_FILE="$$sf2_file" || exit 1; \
		fi \
	done
	@echo "\033[1;32mBatch processing complete!\033[0m"

# Clean only specific plugin directory
clean_plugin:
	@if [ -d "$(PLUGIN_DIR)" ]; then \
		echo "Cleaning plugin directory: $(PLUGIN_DIR)"; \
		rm -rf "$(PLUGIN_DIR)"; \
	fi

# Clean everything (only when explicitly called)
clean:
	@echo "Cleaning all build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f *.ttl
	@echo "Clean complete"

# Silent build target for interactive mode
build_plugin: $(PLUGIN_DIR)/$(PLUGIN_NAME).so $(PLUGIN_DIR)/metadata
	@echo "Build complete: $(PLUGIN_DIR)"

# Normal build target with logo
all: intro $(PLUGIN_DIR)/$(PLUGIN_NAME).so $(PLUGIN_DIR)/metadata
	@echo "Build complete: $(PLUGIN_DIR)"

# Display intro message
intro:
	@echo "\033[1;31m _  ________        \033[38;5;203m___    _           _                                  _\033[0m"
	@echo "\033[1;31m| ||  ____  |      \033[38;5;203m/   |  | |         | |                                | |\033[0m"
	@echo "\033[1;31m| || |___ | |     \033[38;5;203m/ /| |  | |_ __  ___| |_ _ __ _   _ _ __ __  ___  _ __ | |_ ___\033[0m"
	@echo "\033[1;31m| ||___  || |    \033[38;5;203m/ / | |  | | '_ \/ __|  _| '__| | | | '_ '_ \/ _ \| '_ \|  _/ __|\033[0m"
	@echo "\033[1;31m| |____| || |___\033[38;5;203m/ /  | |  | | | | \__ | |_| |  | |_/ | | || | | __/| | | | |_\__ \\\033[0m"
	@echo "\033[1;31m|________||______\033[38;5;203m/   |_|  |_|_| |_|___\\___|_|  \\___,_|_| || |_\\___||_| |_\\___|___/\033[0m"
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