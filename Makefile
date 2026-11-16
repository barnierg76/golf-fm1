# Golf voor de FM-1: make (build) / make flash.
#
# FM1_BASE  the fm1-base checkout (cloned into .fm1-base if missing, git pulled otherwise)
# JIELI_TOOLCHAIN / AC79_SDK  the JieLi toolchain and SDK packaging files
# DEVICE  MIDI port to flash (substring of its name). Empty by default: the
#         installer then finds any FM-1 by its handshake, whatever the port is
#         called (it survives firmware renames). Set DEVICE only when more
#         than one FM-1 is attached and it picks the wrong one.

FM1_BASE ?= $(abspath .fm1-base)
JIELI_TOOLCHAIN ?= $(abspath ../jieli/toolchain)
AC79_SDK ?= $(abspath ../jieli/sdk-files)
DEVICE ?=

PY := $(abspath .venv)/bin/python
FM1_BASE_URL := https://github.com/charlesvestal/fm1-base

export JIELI_TOOLCHAIN AC79_SDK

.PHONY: all setup build flash

all: build

# the venv with the build's and the installer's Python dependencies; redone
# when Makefile's VENV_DEPS change or the venv is gone
VENV_DEPS := mido python-rtmidi pillow

setup: .venv/.stamp

.venv/.stamp: Makefile
	test -x .venv/bin/python || uv venv .venv
	uv pip install --python .venv/bin/python $(VENV_DEPS)
	touch $@

$(FM1_BASE)/.git:
	git clone $(FM1_BASE_URL) $(FM1_BASE)

build: setup $(FM1_BASE)/.git
	git -C $(FM1_BASE) pull --ff-only || true
	git -C $(FM1_BASE) submodule update --init
	rsync -a --delete --exclude build --exclude '.git*' ./eigen/ $(FM1_BASE)/golf/
	mkdir -p $(FM1_BASE)/golf/build build
	cd $(FM1_BASE) && $(PY) tools/build.py golf --no-ble
	cp $(FM1_BASE)/golf/build/golf.fwsc build/golf.fwsc
	ls -l build/golf.fwsc

flash: build
	$(PY) $(FM1_BASE)/tools/fm1_install.py build/golf.fwsc \
		$(if $(DEVICE),--port "$(DEVICE)") --yes --classic
