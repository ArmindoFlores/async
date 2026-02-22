# Compiler and flags
CC := gcc
CFLAGS := -std=c2x -Wall -Wextra -ggdb -Iinclude -DDEBUGGING

# Directories
SRC_DIR := src
MAIN_DIR := main
BUILD_DIR := build
LIB_DIR := lib

# Library
LIB_NAME := async
LIB_FILE := $(LIB_DIR)/lib$(LIB_NAME).a

# Find all .c, .s, and .S files in src/ and main/
SRC_FILES := $(wildcard $(SRC_DIR)/*.[cSs])
MAIN_FILES := $(wildcard $(MAIN_DIR)/*.c)

# Convert src/*.[cSs] to build/*.o
OBJ_FILES := \
	$(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC_FILES)) \
	$(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(SRC_FILES)) \
	$(patsubst $(SRC_DIR)/%.s, $(BUILD_DIR)/%.o, $(SRC_FILES))

# Executables (one per main/*.c)
EXECUTABLES := $(patsubst $(MAIN_DIR)/%.c, %, $(MAIN_FILES))

# Default target
all: $(EXECUTABLES)

# Build each executable by linking main.o with the library
%: $(MAIN_DIR)/%.c $(LIB_FILE)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -l$(LIB_NAME) -o $@

# Build the library
$(LIB_FILE): $(OBJ_FILES) | $(LIB_DIR)
	ar rcs $@ $^

# Compile src/*.c -> build/*.o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.[cSs] | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure directories exist
$(BUILD_DIR):
	mkdir -p $@

$(LIB_DIR):
	mkdir -p $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR) $(EXECUTABLES)

.PHONY: all clean
