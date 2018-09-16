#Generated by VisualGDB (http://visualgdb.com)
#DO NOT EDIT THIS FILE MANUALLY UNLESS YOU ABSOLUTELY NEED TO
#USE VISUALGDB PROJECT PROPERTIES DIALOG INSTEAD

BINARYDIR := Debug_walls

#Toolchain
CC := gcc
CXX := g++
LD := $(CXX)
AR := ar
OBJCOPY := objcopy

#Additional flags
PREPROCESSOR_MACROS := DEBUG
INCLUDE_DIRS := /usr/include/ /usr/include/glib-2.0/ /usr/lib/x86_64-linux-gnu/glib-2.0/include
LIBRARY_DIRS := /usr/local/lib/sloong
LIBRARY_NAMES := m dl lua5.3 univ pthread glib-2.0 crypto uuid ssl crypto
ADDITIONAL_LINKER_INPUTS := 
MACOS_FRAMEWORKS := 
LINUX_PACKAGES := 

CFLAGS := -ggdb -ffunction-sections -O0
CXXFLAGS := -ggdb -ffunction-sections -O0 -std=c++17 -DLUA_USE_READLINE
ASFLAGS := 
LDFLAGS := -Wl,-gc-sections -Wl,-rpath=/usr/local/lib/sloong -Wl,-rpath=/usr/lib64/mysql
COMMONFLAGS := 
LINKER_SCRIPT := 

START_GROUP := -Wl,--start-group
END_GROUP := -Wl,--end-group

#Additional options detected from testing the toolchain
IS_LINUX_PROJECT := 1