/* eSpeak NG's config.h for the LIED core (what CMake writes for: no MBROLA, sonic, pcaudio, async, speech player). */
#if !defined(_WIN32)
#define HAVE_MKSTEMP 1
#endif
#define USE_ASYNC 0
#define USE_KLATT 1
#define USE_LIBPCAUDIO 0
#define USE_LIBSONIC 0
#define USE_MBROLA 0
#define USE_SPEECHPLAYER 0
#define PACKAGE_VERSION "1.52.0"
