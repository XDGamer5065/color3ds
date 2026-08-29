#---------------------------------------------------------------------------------
# 3DS Screen Color Changer - Makefile
#---------------------------------------------------------------------------------

.SUFFIXES:

#---------------------------------------------------------------------------------
# Environment
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to devkitARM>")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Project settings
#---------------------------------------------------------------------------------

TARGET := ScreenColorChanger
BUILD := build
SOURCES := source

APP_TITLE := 3DS Screen Color Changer
APP_DESCRIPTION := Solid-color screen utility with a touch menu
APP_AUTHOR := XDGamer5065

#---------------------------------------------------------------------------------
# Compiler settings
#---------------------------------------------------------------------------------

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -O2 -mword-relocations \
          -ffunction-sections -fdata-sections \
          $(ARCH)

CFLAGS += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)

LIBS := -lctru -lm

LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
# Build configuration
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

#---------------------------------------------------------------------------------
# Object files
#---------------------------------------------------------------------------------

export OFILES_SOURCES := \
    $(CPPFILES:.cpp=.o) \
    $(CFILES:.c=.o) \
    $(SFILES:.s=.o)

export OFILES := $(OFILES_SOURCES)

#---------------------------------------------------------------------------------
# Include and library paths
#---------------------------------------------------------------------------------

export INCLUDE := \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)

export LIBPATHS := \
    $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------
# SMDH / 3DSX settings
#---------------------------------------------------------------------------------

export _3DSXDEPS := $(OUTPUT).smdh

export _3DSXFLAGS += --smdh=$(OUTPUT).smdh

#---------------------------------------------------------------------------------
# Main build
#---------------------------------------------------------------------------------

.PHONY: all clean

all: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
# Create build directories
#---------------------------------------------------------------------------------

$(BUILD):
	@mkdir -p $@

$(DEPSDIR):
	@mkdir -p $@

#---------------------------------------------------------------------------------
# Clean
#---------------------------------------------------------------------------------

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD)
	@rm -f $(TARGET).3dsx
	@rm -f $(TARGET).elf
	@rm -f $(TARGET).smdh

else

#---------------------------------------------------------------------------------
# Build inside build/
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES_SOURCES:.o=.d)

.PHONY: all

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf: $(OFILES)

-include $(DEPSDIR)/*.d

endif

#---------------------------------------------------------------------------------
# Standard devkitPro rules
#---------------------------------------------------------------------------------

include $(DEVKITARM)/base_rules
