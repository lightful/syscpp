ifeq ($(DEBUG), 1)
    BUILD_DIR := debug
    CXXFLAGS  := -O0 -g3 $(CXXFLAGS)
else
    BUILD_DIR := release
    CXXFLAGS  := -O2 $(CXXFLAGS)
endif

PATH_BIN  := $(BUILD_DIR)/application
SUBPRJS   := ../MyLib

SRC_DIR   := src
INCLUDES  := -I../../../include  # for <sys++/ActorThread.hpp>
LDLIBS    := -lpthread

include ../../../posix.mk
