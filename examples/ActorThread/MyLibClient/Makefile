BUILD_DIR = build
PATH_BIN  = $(BUILD_DIR)/application.exe
SUBPRJS   = ../MyLib

SRC_DIR   = src
INCLUDES  = -I$(SUBPRJS)/include \
            -I../../../include  # for <sys++/ActorThread.hpp>

LIBS      = $(SUBPRJS)/$(BUILD_DIR)/*.a

include ../../../nmake.mk
