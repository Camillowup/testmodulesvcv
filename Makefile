# GENRA Test: new instruments of the GENRA family to try out before they join GENRA Instruments (empty since 2.8.0:
# MATH … KESSEL FX moved to GENRA Instruments). Build:
#   make RACK_DIR=/path/to/Rack-SDK dist
RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += licenses
SOURCES += $(wildcard src/*/*.cpp)

include $(RACK_DIR)/arch.mk

# ---------------------------------------------------------------- GENRA's shared parts
# The knobs, Follow GENRA, the link hub and the JIANT UI are not copied into this repo: they come fresh from the GENRA
# repo (github.com/Camillowup/Genra-for-Vcv, branch main) before every build, so GENRA Test always speaks the same link
# version as the GENRA plugins. `make genra-src` fetches them (GitHub Actions does it before building):
#   genra/     <- GENRA's src/        res/knob/  <- GENRA's res/knob/
GENRA_REPO ?= https://github.com/Camillowup/Genra-for-Vcv
GENRA_BRANCH ?= main
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

# ---------------------------------------------------------------- cores of new test modules
# None at the moment. A new module's engine (a firmware as its own small shared library, as GENRA Instruments builds
# them: see instruments/Makefile in the GENRA repo) goes here, with its file in CORES.
CORES :=
all: $(CORES)
dist: $(CORES)

include $(RACK_DIR)/plugin.mk

clean: clean-cores
clean-cores:
	rm -f res/*core.so res/*core.dylib res/*core.dll
