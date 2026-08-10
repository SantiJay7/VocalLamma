RACK_DIR ?= ../Rack-SDK

SLUG := $(shell jq -r .slug plugin.json)
VERSION := $(shell jq -r .version plugin.json)

SOURCES += src/plugin.cpp
SOURCES += src/VocalLamma.cpp

DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
