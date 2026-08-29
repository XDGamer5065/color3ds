#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to devkitARM>")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Project settings
#---------------------------------------------------------------------------------

TARGET      := ScreenColorChanger
BUILD       := build
SOURCES     := source

APP_TITLE       := 3DS Screen Color Changer
APP_DESCRIPTION := Solid-color screen utility with a touch menu
APP_AUTHOR      := XDGamer5065

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------

CFLAGS   := -g -Wall -O2 -mword-relocations \
            -ffunction-sections -fdata-sections

CFLAGS  += $(INCLUDE)

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := -g

LDFLAGS  := -specs=3dsx.specs -g \
            -Wl,-Map,$(notdir $*.map)

LIBS     := -lctru -lm
LIBDIRS  := $(CTRULIB)

#---------------------------------------------------------------------------------
# Build system
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD)
	@rm -f $(TARGET).3dsx
	@rm -f $(TARGET).elf

else

DEPENDS := $(CFILES:.c=.d) \
           $(CPPFILES:.cpp=.d) \
           $(SFILES:.s=.d)

CFILES   := $(addprefix $(BUILD)/,$(CFILES:.c=.o))
CPPFILES := $(addprefix $(BUILD)/,$(CPPFILES:.cpp=.o))
SFILES   := $(addprefix $(BUILD)/,$(SFILES:.s=.o))

.PHONY: all clean

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(CFILES) $(CPPFILES) $(SFILES)

-include $(DEPENDS)

endif

#---------------------------------------------------------------------------------
include $(DEVKITARM)/base_rules
#---------------------------------------------------------------------------------
