NAME = libmediahelpers.so
TYPE = SHARED
APP_MIME_SIG =
SRCS = AudioCapture.cpp VideoConsumer.cpp
LIBS = be media $(STDCPPLIBS)
OPTIMIZE := FULL
WARNINGS = NONE
COMPILER_FLAGS =
LINKER_FLAGS =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
