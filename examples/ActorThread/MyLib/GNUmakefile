ifeq ($(DEBUG), 1)
    BUILD_DIR := debug
    CXXFLAGS  := -O0 -g3 $(CXXFLAGS)
else
    BUILD_DIR := release
    CXXFLAGS  := -O2 $(CXXFLAGS)
endif

OUT_LIB   := MyLib.a

SRC_DIR   := src
INCLUDES  := -I../../../include  # for <sys++/ActorThread.hpp>

include ../../../posix.mk
