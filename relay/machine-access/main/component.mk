#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_SUBMODULES=kv
#COMPONENT_SRCDIRS=.
#COMPONENT_SRCDIRS+=kv
#COMPONENT_INCLUDES=kv/include

define FIX =
ifdef $(1)
CFLAGS += -D$(1)=$$($(1))
CPPFLAGS += -D$(1)=$$($(1))
COMPONENT_CFLAGS += -D$(1)=$$($(1))
COMPONENT_CPPFLAGS += -D$(1)=$$($(1))
#$$(info USING $(1)=$$($(1)))
#else
#$$(info NOT SET: $(1))
endif
endef

$(eval $(call FIX,WIFI_SSID))
$(eval $(call FIX,WIFI_PASSWORD))
$(eval $(call FIX,MQTT_HOST))
$(eval $(call FIX,MQTT_PORT))

MAIN_GIT_REV = $(shell git rev-parse --short HEAD)
CFLAGS+=-DMAIN_GIT_REV=\"$(MAIN_GIT_REV)\"
CPPFLAGS+=-DMAIN_GIT_REV=\"$(MAIN_GIT_REV)\"

show-fix:
	@echo CFLAGS=$(CFLAGS)
