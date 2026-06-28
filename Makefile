CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
PREFIX ?= /usr/local
SYSCONFDIR ?= /etc
SYSTEMD_DIR ?= $(SYSCONFDIR)/systemd/system
UDEV_DIR ?= $(SYSCONFDIR)/udev/rules.d
DEFAULT_DIR ?= $(SYSCONFDIR)/default
MODULES_LOAD_DIR ?= $(SYSCONFDIR)/modules-load.d

BUILD_DIR := build
SRC_DIR := src

TOOLS := $(BUILD_DIR)/scanner-dump $(BUILD_DIR)/scanner-bridge

.PHONY: all clean install uninstall

all: $(TOOLS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/scanner-dump: $(SRC_DIR)/scanner_dump.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BUILD_DIR)/scanner-bridge: $(SRC_DIR)/scanner_bridge.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(BUILD_DIR)/scanner-bridge "$(DESTDIR)$(PREFIX)/bin/scanner-bridge"
	install -Dm755 $(BUILD_DIR)/scanner-dump "$(DESTDIR)$(PREFIX)/bin/scanner-dump"
	install -Dm644 packaging/escanner.service "$(DESTDIR)$(SYSTEMD_DIR)/escanner.service"
	install -Dm644 packaging/99-escanner.rules "$(DESTDIR)$(UDEV_DIR)/99-escanner.rules"
	install -Dm644 packaging/escanner.default "$(DESTDIR)$(DEFAULT_DIR)/escanner"
	install -Dm644 packaging/escanner-modules.conf "$(DESTDIR)$(MODULES_LOAD_DIR)/escanner.conf"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/scanner-bridge"
	rm -f "$(DESTDIR)$(PREFIX)/bin/scanner-dump"
	rm -f "$(DESTDIR)$(SYSTEMD_DIR)/escanner.service"
	rm -f "$(DESTDIR)$(UDEV_DIR)/99-escanner.rules"
	rm -f "$(DESTDIR)$(DEFAULT_DIR)/escanner"
	rm -f "$(DESTDIR)$(MODULES_LOAD_DIR)/escanner.conf"
