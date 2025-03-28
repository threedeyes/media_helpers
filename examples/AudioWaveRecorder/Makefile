NAME = AudioWaveRecorder
TYPE = APP
SRCS = AudioWaveRecorder.cpp
LIBS = be media mediahelpers $(STDCPPLIBS)
LIBPATHS = ../../lib
LOCAL_INCLUDE_PATHS = ../../src
OPTIMIZE := FULL
SYMBOLS :=
DEBUGGER :=
COMPILER_FLAGS =
LINKER_FLAGS =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
