##===- tools/llvmc2/Makefile -------------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open
# Source License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../..

BUILTIN_PLUGINS = Base
DRIVER_NAME = llvmc2
DIRS = $(patsubst %,plugins/%,$(BUILTIN_PLUGINS)) src

export BUILTIN_PLUGINS
export DRIVER_NAME
export BUILTIN_LLVMC_PLUGIN=1

include $(LEVEL)/Makefile.common
