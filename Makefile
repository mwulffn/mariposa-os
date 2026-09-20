# Amiga Project - Main Makefile

# Platform detection
UNAME_S := $(shell uname -s)

# Configuration
CONFIG = configs/a600.fs-uae
HDD = harddrives/boot.hdf

# Sub-project directories
ROM_DIR = src/rom
KERNEL_DIR = src/kernel

# Build artifacts (for run target)
ROM = $(ROM_DIR)/build/kick.rom
KERNEL = $(KERNEL_DIR)/build/SYSTEM.BIN

# Platform-specific FS-UAE binary path
# Can be overridden with: make run FS_UAE=/path/to/fs-uae
ifeq ($(UNAME_S),Darwin)
    FS_UAE ?= /Applications/FS-UAE.app/Contents/MacOS/fs-uae
else ifeq ($(UNAME_S),Linux)
    FS_UAE ?= fs-uae
else
    FS_UAE ?= fs-uae
endif

.PHONY: all rom kernel deploy run run-open clean test test-clean lint fmt
.PHONY: docker-image docker-image-refresh docker-build docker-make docker-shell docker-versions

all: rom kernel

rom:
	$(MAKE) -C $(ROM_DIR)

kernel:
	$(MAKE) -C $(KERNEL_DIR)

deploy: kernel
	@echo "Deploying kernel to hard drive image..."
	mcopy -i $(HDD) -o $(KERNEL) ::SYSTEM.BIN
	@echo "Kernel deployed successfully"
	@mdir -i $(HDD) ::

run: rom deploy
	$(FS_UAE) "$(PWD)/$(CONFIG)"

# Alternative: use macOS open command (doesn't pass args reliably, macOS only)
run-open: rom deploy
	open -a "FS-UAE" --args "$(PWD)/$(CONFIG)"

# ---------------------------------------------------------------------------
# Headless tests
# ---------------------------------------------------------------------------
# Runs real ROM code under a 68000 CPU simulator: no emulator, no display, no
# serial port, whole suite in well under a second. Output is the ### protocol
# (see docs/testing.md); the exit code is the verdict.
#
#   make test                       run everything
#   make test FILTER=rom.panic      run one group while iterating
# ---------------------------------------------------------------------------
FILTER ?=

test: rom kernel lint
	@$(MAKE) -C tests run FILTER="$(FILTER)"

# ---------------------------------------------------------------------------
# Python helper scripts
# ---------------------------------------------------------------------------
# The repo's Python is six build/debug helpers with no third-party imports.
# uv exists only to pin ruff; it is deliberately NOT on the build path, so
# tests/mksym.py and tests/mkdisk.py stay callable as bare `python3` and the
# ROM still builds in a container that carries python3 and nothing else.
#
# `make test` depends on lint, but a missing uv skips rather than fails -
# otherwise `make docker-make DOCKER_TARGET=test` would break, since
# docker/Dockerfile has no uv.
# ---------------------------------------------------------------------------
UV ?= uv

lint:
	@if command -v $(UV) >/dev/null 2>&1; then \
		$(UV) run --quiet ruff check .; \
	else \
		echo "lint: $(UV) not found, skipping Python lint"; \
	fi

fmt:
	$(UV) run ruff format .

test-clean:
	$(MAKE) -C tests clean

clean:
	$(MAKE) -C $(ROM_DIR) clean
	$(MAKE) -C $(KERNEL_DIR) clean
	$(MAKE) -C tests clean

# ---------------------------------------------------------------------------
# Containerised build
# ---------------------------------------------------------------------------
# Builds vasm/vbcc/vlink from upstream source into an image, then runs these
# same Makefiles inside it against a bind mount of the working tree. Output
# lands in the usual build/ directories, owned by you rather than by root.
#
# No host toolchain required. FS-UAE is deliberately not in the image, so
# `make run` and ./debug.py still need a local install.
#
#   make docker-build            build ROM + kernel in the container
#   make docker-make DOCKER_TARGET=clean   run any target in the container
#   make docker-shell            interactive shell with the toolchain on PATH
#   make docker-versions         show which tool releases the image has
#   make docker-image-refresh    re-fetch the toolchain, ignoring layer cache
# ---------------------------------------------------------------------------
DOCKER         ?= docker
DOCKER_IMAGE   ?= mariposa-os-build
DOCKER_TARGET  ?= all

# Run as the invoking uid/gid so build artifacts are not root-owned on the
# host. The build context is docker/ - the sources come in via the mount, so
# nothing large is ever shipped to the daemon.
DOCKER_RUN = $(DOCKER) run --rm \
	-u $$(id -u):$$(id -g) \
	-v "$(CURDIR)":/work \
	-w /work \
	$(DOCKER_IMAGE)

docker-image:
	$(DOCKER) build -t $(DOCKER_IMAGE) docker

# The toolchain URLs are unversioned "latest" tarballs, so Docker's layer
# cache would otherwise keep handing you the release you first built with.
docker-image-refresh:
	$(DOCKER) build --no-cache -t $(DOCKER_IMAGE) docker

docker-build: docker-image
	$(DOCKER_RUN) make all

docker-make: docker-image
	$(DOCKER_RUN) make $(DOCKER_TARGET)

docker-shell: docker-image
	$(DOCKER) run --rm -it \
		-u $$(id -u):$$(id -g) \
		-v "$(CURDIR)":/work \
		-w /work \
		$(DOCKER_IMAGE) bash

docker-versions: docker-image
	$(DOCKER_RUN) cat /opt/amiga-toolchain/VERSIONS
