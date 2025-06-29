SRCDIR	 = $(abspath src)
BUILDDIR = $(abspath build)

TARGET = build/gaybar

CC = clang

COMFLAGS = -Wall -Wextra
ifeq ($(RELEASE),)
COMFLAGS += -ggdb
else
COMFLAGS += -O3
endif

LDFLAGS = $(COMFLAGS)
ifneq ($(RELEASE),)
LDFLAGS += -Wl,-z,relro,-z,now -pie
endif

CFLAGS = \
	$(COMFLAGS) \
	-I include/ \
	-fPIE \
	-fstack-protector-all

LIBS = -lwayland-client

SRCS = $(shell find $(SRCDIR)/ -name '*.c' -type f)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf $(BUILDDIR)
