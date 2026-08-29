```makefile
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

TARGET := ScreenColorChanger

BUILD := build
SOURCES := source

DATA :=
INCLUDES :=
GRAPHICS :=

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

LDFLAGS := -specs=3dsx.specs -g $(ARCH) \
           -Wl,-Map,$(notdir $*.map)

LIBS := -lctru -lm

LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
# Build system
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

#---------------------------------------------------------------------------------
# Top-level build
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES := $(CPPFILES:.cpp=.o) \
                 $(CFILES:.c=.o) \
                 $(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(TARGET).smdh
export _3DSXFLAGS := --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

#---------------------------------------------------------------------------------

else

#---------------------------------------------------------------------------------
# Build inside build/
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

#---------------------------------------------------------------------------------

include $(DEVKITARM)/base_rules
```
