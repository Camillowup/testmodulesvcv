# GENRA Test: new instruments of the GENRA family to try out (MATH, KURBEL, SWARM, LIED). Build:
#   make RACK_DIR=/path/to/Rack-SDK dist
RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/math/*.cpp)
SOURCES += $(wildcard src/kurbel/*.cpp)
SOURCES += $(wildcard src/swarm/*.cpp)
SOURCES += $(wildcard src/lied/*.cpp)

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
CORES := res/mathcore.$(CORE_EXT) res/kurbelcore.$(CORE_EXT) res/swarmcore.$(CORE_EXT) res/liedcore.$(CORE_EXT)

JIANT_STAMP := jiant/.unpacked
$(JIANT_STAMP): jiant.tar.gz
	tar xzf jiant.tar.gz
	touch $@

res/mathcore.$(CORE_EXT): mathcore/mathcore.c mathcore/mathcore.h $(JIANT_STAMP)
	@mkdir -p res
	$(CORE_CC) $(CORE_FLAGS) $(filter --target% -arch% -march% -mmacosx-version-min%,$(FLAGS)) -o $@ $< $(CORE_LIBS)

# ---------------------------------------------------------------- the SWARM core
# The same JIANT snapshot with swarmcore/swarmcore.c on top (SWARM: eng_input.c, with virtual devices instead of a radio).
res/swarmcore.$(CORE_EXT): swarmcore/swarmcore.c swarmcore/swarmcore.h $(JIANT_STAMP)
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

# ---------------------------------------------------------------- the LIED core
# eSpeak NG 1.52.0 (espeak.tar.gz, a snapshot of its library sources: espeak/SNAPSHOT.txt) with liedcore/liedcore.c on
# top, as one small shared library. Loaded once per process (eSpeak's state is static; LIED calls it under a lock).
# Its language data (compiled from the same release) is in res/espeak-ng-data.
ESPEAK_STAMP := espeak/.unpacked
$(ESPEAK_STAMP): espeak.tar.gz
	tar xzf espeak.tar.gz
	touch $@

ESPEAK_LIB := espeak/src/libespeak-ng
ESPEAK_C := common mnemonics error ieee80 compiledata compiledict dictionary encoding intonation langopts numbers \
	phoneme phonemelist readclause setlengths soundicon spect ssml synthdata synthesize tr_languages translate \
	translateword voices wavegen speech espeak_api klatt
UCD_C := case categories ctype proplist scripts tostring
LIED_SRC := liedcore/liedcore.c $(addprefix $(ESPEAK_LIB)/,$(addsuffix .c,$(ESPEAK_C))) \
	$(addprefix espeak/src/ucd-tools/src/,$(addsuffix .c,$(UCD_C)))
LIED_FLAGS := -std=gnu11 -O2 -w -DLIBESPEAK_NG_EXPORT=1 -Iliedcore -Iespeak/src/include -Iespeak/src/include/compat \
	-Iespeak/src/ucd-tools/src/include \
	-I$(ESPEAK_LIB)
ifdef ARCH_LIN
	LIED_FLAGS += -fPIC -fvisibility=hidden -shared
	LIED_LIBS := -lm
endif
ifdef ARCH_MAC
	LIED_FLAGS += -fPIC -fvisibility=hidden -dynamiclib
	LIED_LIBS := -lm
endif
ifdef ARCH_WIN
	LIED_FLAGS += -shared -static
	LIED_LIBS := -lm -ladvapi32
endif

res/liedcore.$(CORE_EXT): liedcore/liedcore.c liedcore/liedcore.h liedcore/config.h $(ESPEAK_STAMP)
	@mkdir -p res
	$(CORE_CC) $(LIED_FLAGS) $(filter --target% -arch% -march% -mmacosx-version-min%,$(FLAGS)) -o $@ $(LIED_SRC) $(LIED_LIBS)

all: $(CORES)
dist: $(CORES)

include $(RACK_DIR)/plugin.mk

clean: clean-cores
clean-cores:
	rm -f res/*core.so res/*core.dylib res/*core.dll
