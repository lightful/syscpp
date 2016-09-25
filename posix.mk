# "Universal C++ POSIX Makefile" v2.0 for GNU make

#       Copyright Ciriaco Garcia de Celis 2011-2016.
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)

# With automatic dependencies generation (gcc/clang) and recursive source directory tree support
#
# In your project(s) Makefile it is only required to define a few settings and include this file AT THE END:
#
#     Settings which can be defined in any project (all optional unless otherwise stated):
#         BUILD_DIR := debug            # mandatory setting
#         SRC_DIR   := src              # mandatory setting
#         INCLUDES  :=
#         CXX       := g++              # a default value is provided in your system
#         CXXFLAGS  := -O0 -g3 -DDEBUG  # see also ARCHFLAGS and WARNFLAGS bellow
#
#     Settings to be defined only for library subprojects
#         OUT_LIB   := LibName.a  # mandatory setting (file will always be placed in BUILD_DIR)
#
#     Settings to be defined only for executable projects:
#         PATH_BIN  := $(BUILD_DIR)/executable    # mandatory setting
#         SUBPRJS   := ../Lib1 ../Lib2            # each require its own Makefile
#         LIBS      := ../../ExternLib/SomeLib.a  # Note: omit subprojects (implicitly linked and included)
#         LDLIBS    := -lboost_system -lpthread
#         LDFLAGS   :=
# NOTES:
#  * The includes exported by library subprojects must be placed in a directory called 'include'
#  * When used on a top project, its library subprojects makefiles are automatically invoked in the proper order:
#    - Any target will be passed to them (not only 'all' or 'clean')
#    - To make only the top project run "DONTLOOP=1 make"
#    - If ARCHFLAGS/WARNFLAGS are setup they need to be exported when aiming to override implicit defaults in subprojects
#  * To hook 'all' or 'clean' targets in your Makefile use '::' (instead of ':')
#  * Other targets than 'all' or 'clean' should be placed after including this file (to keep 'all' as the default target)

ARCHFLAGS ?= -std=c++11 -march=native  # set these variables in your Makefile to override these implicit defaults

WARNFLAGS ?= -Wall -Wextra -pedantic -Wconversion -Wsign-conversion -Wsign-promo -Wcast-qual -Wfloat-equal \
             -Wpointer-arith -Wnon-virtual-dtor -Woverloaded-virtual -Wshadow -Wundef -Wmissing-include-dirs

CXXFLAGS += $(ARCHFLAGS) $(WARNFLAGS)
INCLUDES += $(foreach dir,$(SUBPRJS),-I$(dir)/include)
LIBS     += $(foreach dir,$(SUBPRJS),$(wildcard $(dir)/$(BUILD_DIR)/*.a))

HL_OK    := \e[30;42m
HL_ERROR := \e[30;101m
HL_CMD   := \e[35m
HL_OFF   := \e[0m

ifndef SUBPRJS
MAKEACTIVE := 1
else
ifdef DONTLOOP
MAKEACTIVE := 1
else
export DONTLOOP := 1

reverse_list = $(if $(1),$(call reverse_list,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

ALL_MK_PROJECTS := $(call reverse_list,$(SUBPRJS)) .

all:: whichever

whichever::
	@for prj in $(ALL_MK_PROJECTS); do if ! \
		$(MAKE) -C $$prj --no-print-directory $(MAKECMDGOALS) \
	; then echo -e "$(HL_ERROR)ERROR $$prj$(HL_OFF)" && exit 1; fi; done

.PHONY: whichever all $(MAKECMDGOALS)

$(foreach target, $(MAKECMDGOALS), $(eval $(target): whichever))

endif # DONTLOOP
endif # SUBPRJS

ifdef MAKEACTIVE

SRC_SUBDIRS   := $(shell [ -d $(SRC_DIR) ] && find -L $(SRC_DIR) -type d \
                   -not \( -path $(SRC_DIR)/$(BUILD_DIR) -prune \) -not \( -name .git -prune \))
BUILD_SUBDIRS := $(patsubst $(SRC_DIR)%,$(BUILD_DIR)%,$(SRC_SUBDIRS))
MKDIR_SUBDIRS := $(sort $(patsubst %/,,$(BUILD_SUBDIRS) $(dir $(PATH_BIN)) $(dir $(PATH_LIB))))
PATH_LIB      ?= $(if $(OUT_LIB),$(BUILD_DIR)/$(OUT_LIB))
SRC_FILES     := $(sort $(foreach sdir,$(SRC_SUBDIRS),$(wildcard $(sdir)/*.cpp)) $(AUTOGEN))
OBJ_FILES     := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_FILES))
DEPENDENCIES  := $(OBJ_FILES:.o=.d)

vpath %.cpp $(SRC_SUBDIRS) $(BUILD_SUBDIRS)

.PHONY: all clean checkdirs

all:: checkdirs $(PATH_BIN) $(PATH_LIB)
	@echo -e "$(HL_OK)DONE$(HL_OFF) $(lastword $(subst /, ,$(CURDIR)))"

clean::
	@rm -rf $(BUILD_SUBDIRS)

checkdirs: $(MKDIR_SUBDIRS)

$(MKDIR_SUBDIRS):
	@mkdir -p $@

-include $(DEPENDENCIES)

$(PATH_LIB): $(OBJ_FILES)
	@echo -e "$(HL_CMD)$(AR)$(HL_OFF)" -r "$(HL_CMD)$@$(HL_OFF)" $(OBJ_FILES)
	@$(AR) -r $@ $(OBJ_FILES)

$(PATH_BIN): $(OBJ_FILES) $(LIBS)
	@echo -e "$(HL_CMD)$(CXX)$(HL_OFF)" $(LDFLAGS) $(OBJ_FILES) $(CXXFLAGS) -MMD "$(HL_CMD)-o $@$(HL_OFF)" $(LIBS) $(LDLIBS)
	@$(CXX) $(LDFLAGS) $(OBJ_FILES) $(CXXFLAGS) -MMD -o $@ $(LIBS) $(LDLIBS)

define build-subdir-goal
$1/%.o: %.cpp
	@echo -e "$(HL_CMD)$(CXX)$(HL_OFF)" $(CXXFLAGS) $(INCLUDES) -MMD "$(HL_CMD)-c $$<$(HL_OFF)" -o $$@
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -c $$< -o $$@
endef

$(foreach bdir,$(BUILD_SUBDIRS),$(eval $(call build-subdir-goal,$(bdir))))

endif # MAKEACTIVE
