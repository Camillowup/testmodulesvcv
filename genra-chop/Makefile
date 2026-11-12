# GENRA CHOP: its own plugin (slug GenraChop) in GENRA Test's repo. Build:
#   make RACK_DIR=/path/to/Rack-SDK dist
# Self-contained: no parts fetched from the GENRA repo (its own knobs, its own look).
RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/hh/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
