# GENRA Test: new instruments of the GENRA family to try out (MATH, KURBEL). Build:
#   make RACK_DIR=/path/to/Rack-SDK dist
RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/math/*.cpp)
SOURCES += $(wildcard src/kurbel/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += licenses

include $(RACK_DIR)/arch.mk

# ---------------------------------------------------------------- the MATH core
# The JIANT FM firmware (jiant.tar.gz, a snapshot: jiant/SNAPSHOT.txt) with mathcore/mathcore.c on top, built as its
# own small shared library. Plain C with no Rack dependency; every module instance loads a private copy of the
# library, since the firmware keeps its state in static variables (as GENRA Instruments' VOICE and ANIMAL).
CORE_CC ?= $(CC)
CORE_FLAGS := -std=gnu11 -O2 -w -ffp-contract=off -DFELUCCA_VERSION='"VCV"' -Ijiant/build/gen -Ijiant/firmware/src
ifdef ARCH_LIN
	CORE_EXT := so
	CORE_FLAGS += -fPIC -fvisibility=hidden -shared
	CORE_LIBS := -lm
endif
ifdef ARCH_MAC
	CORE_EXT := dylib
	CORE_FLAGS += -fPIC -fvisibility=hidden -dynamiclib
	CORE_LIBS := -lm
endif
ifdef ARCH_WIN
	CORE_CC := gcc
	CORE_EXT := dll
	# the firmware's host build includes <sys/wait.h> (its test harness, unused here): mathcore/compat stands in
	CORE_FLAGS += -shared -Imathcore/compat -static
	CORE_LIBS := -lm -lwinpthread
endif
CORES := res/mathcore.$(CORE_EXT) res/kurbelcore.$(CORE_EXT)

JIANT_STAMP := jiant/.unpacked
$(JIANT_STAMP): jiant.tar.gz
	tar xzf jiant.tar.gz
	touch $@

res/mathcore.$(CORE_EXT): mathcore/mathcore.c mathcore/mathcore.h $(JIANT_STAMP)
	@mkdir -p res
	$(CORE_CC) $(CORE_FLAGS) $(filter --target% -arch% -march% -mmacosx-version-min%,$(FLAGS)) -o $@ $< $(CORE_LIBS)

# ---------------------------------------------------------------- the KURBEL core
# ESTAMPIE's firmware (estampie.tar.gz, a snapshot: estampie/SNAPSHOT.txt) with kurbelcore/kurbelcore.c on top, built
# the same way (its own includes instead of JIANT's).
KURBEL_FLAGS := $(subst -Ijiant/build/gen -Ijiant/firmware/src,-Iestampie/build/gen -Iestampie/firmware/src,$(CORE_FLAGS))
ESTAMPIE_STAMP := estampie/.unpacked
$(ESTAMPIE_STAMP): estampie.tar.gz
	tar xzf estampie.tar.gz
	touch $@

res/kurbelcore.$(CORE_EXT): kurbelcore/kurbelcore.c kurbelcore/kurbelcore.h $(ESTAMPIE_STAMP)
	@mkdir -p res
	$(CORE_CC) $(KURBEL_FLAGS) $(filter --target% -arch% -march% -mmacosx-version-min%,$(FLAGS)) -o $@ $< $(CORE_LIBS)

all: $(CORES)
dist: $(CORES)

include $(RACK_DIR)/plugin.mk

clean: clean-cores
clean-cores:
	rm -f res/*core.so res/*core.dylib res/*core.dll
