# Project Name
TARGET = Kairos

# Sources
CPP_SOURCES = Kairos.cpp \
              globals.cpp \
              clock.cpp \
              pattern.cpp \
              display.cpp \
              storage.cpp

# Include paths
C_INCLUDES += -I.

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
