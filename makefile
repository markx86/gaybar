SRCDIR	 = $(abspath src)
BUILDDIR = $(abspath build)

define pkg-config
	$(eval CFLAGS += $(shell pkg-config --cflags $(1)))
	$(eval LDFLAGS += $(shell pkg-config --libs $(1)))
endef

TARGET = build/gaybar

CC = clang

COMFLAGS = -Wall -Wextra
ifeq ($(RELEASE),)
COMFLAGS += -ggdb
else
COMFLAGS += -O3
ifeq ($(NO_SHSTK),)
COMFLAGS += -mshstk
else
COMFLAGS += -fsanitize=safe-stack
endif
ifeq ($(NO_CET),)
COMFLAGS += -fcf-protection=full
endif
endif

LDFLAGS = $(COMFLAGS)
ifneq ($(RELEASE),)
LDFLAGS += \
	-Wl,-z,relro,-z,now,-z,noexecstack \
	-flto \
	-pie
else
LDFLAGS += -lasan
endif

CFLAGS = \
	$(COMFLAGS) \
	-I include/

ifneq ($(RELEASE),)
# FIXME: Maybe use _FORTIFY_SOURCE level 3?
CFLAGS += \
	-fPIE \
	-fstack-protector-all \
	-D_FORTIFY_SOURCE=2
endif

LIBS = wayland-client freetype2

$(foreach lib,$(LIBS),$(call pkg-config, $(lib)))

SRCS = $(shell find $(SRCDIR)/ -name '*.c' -type f)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: run
run: $(TARGET)
	$(TARGET)

.PHONY: run-debug
run-debug: $(TARGET)
	$(TARGET) -L3

.PHONY: run-trace
run-trace: $(TARGET)
	$(TARGET) -L4

.PHONY: clean
clean:
	@rm -rf $(BUILDDIR)
