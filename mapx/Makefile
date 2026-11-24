# GENRA MAP-X: MIDI mapper for GENRA (its own plugin, in a folder of the GENRA Test repo). Build:
#   make genra-src && make RACK_DIR=/path/to/Rack-SDK dist
RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/arch.mk

# GENRA's shared parts (knobs, Follow GENRA, the link hub, the look) come fresh from the GENRA repo before every build
GENRA_REPO ?= https://github.com/Camillowup/Genra-for-Vcv
GENRA_BRANCH ?= dev
genra-src:
	rm -rf .genra-repo genra res/knob
	git clone -q --depth 1 -b $(GENRA_BRANCH) $(GENRA_REPO) .genra-repo
	cp -r .genra-repo/src genra
	mkdir -p res && cp -r .genra-repo/res/knob res/knob
	rm -rf .genra-repo
ifeq ($(filter genra-src clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard genra/Follow.hpp),)
$(error GENRA's shared parts are missing: run "make genra-src" first)
endif
endif

include $(RACK_DIR)/plugin.mk
