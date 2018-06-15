#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_SUBMODULES=kv

define ADD_OVERRIDE =
ifdef $(1)
CFLAGS += -D$(1)=$$($(1))
CPPFLAGS += -D$(1)=$$($(1))
COMPONENT_CFLAGS += -D$(1)=$$($(1))
COMPONENT_CPPFLAGS += -D$(1)=$$($(1))
$$(info Overriding config: $(1)=$$($(1)))
#else
#$$(info NOT SET: $(1))
endif
endef

$(eval $(call ADD_OVERRIDE,WIFI_SSID))
$(eval $(call ADD_OVERRIDE,WIFI_PASSWORD))
$(eval $(call ADD_OVERRIDE,MQTT_HOST))
$(eval $(call ADD_OVERRIDE,MQTT_PORT))

MAIN_GIT_REV = $(shell git rev-parse --short HEAD)
CFLAGS+=-DMAIN_GIT_REV=\"$(MAIN_GIT_REV)\"
CPPFLAGS+=-DMAIN_GIT_REV=\"$(MAIN_GIT_REV)\"

show-flags:
	@echo CFLAGS=$(CFLAGS)
	@echo CPPFLAGS=$(CPPFLAGS)
