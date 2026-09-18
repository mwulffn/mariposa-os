# syntax=docker/dockerfile:1

# ===========================================================================
# Stage 1 - build the m68k cross toolchain from upstream source
# ===========================================================================
# vasm, vbcc and vlink publish no versioned release URLs: the tarballs below
# always serve the current release. A fresh build of this stage therefore
# picks up whatever is latest, and by the same token this stage is NOT
# reproducible across time. Docker's layer cache will happily pin you to the
# release you first built with, so use `make docker-image-refresh` (or
# --no-cache) when you actually want to move forward. `make docker-versions`
# prints what a given image ended up with.
# ===========================================================================
FROM debian:bookworm-slim AS toolchain

# bookworm ships GCC 12. That matters: these are older C codebases, and
# GCC 14 (trixie) promotes implicit function declarations and similar C99
# removals to hard errors, which breaks them.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Bump this (any changed value) to bust the cache on the fetch layers below
# without invalidating the apt layer above.
ARG TOOLCHAIN_REFRESH=0

ARG VASM_URL=http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
ARG VLINK_URL=http://sun.hasenbraten.de/vlink/release/vlink.tar.gz
ARG VBCC_URL=http://www.ibaug.de/vbcc/vbcc.tar.gz

ENV PREFIX=/opt/amiga-toolchain
RUN mkdir -p "$PREFIX/bin"

WORKDIR /src

# --- vasm ------------------------------------------------------------------
# The ROM is assembled with the m68k backend in Motorola syntax. Current
# vasm is also what lets src/kernel/crt0.s say `section .text`; releases
# before ~1.7 only knew CODE/DATA/BSS.
RUN echo "refresh=$TOOLCHAIN_REFRESH" > /dev/null \
    && curl -fsSL "$VASM_URL" -o vasm.tar.gz \
    && tar xzf vasm.tar.gz \
    && cd vasm \
    && make CPU=m68k SYNTAX=mot \
    && install -m 0755 vasmm68k_mot "$PREFIX/bin/" \
    && { install -m 0755 vobjdump "$PREFIX/bin/" \
         || echo "note: vobjdump not built by this release, skipping"; } \
    && cd /src && rm -rf vasm vasm.tar.gz

# --- vlink -----------------------------------------------------------------
# Linker. src/kernel/Makefile drives it with -b rawbin1 and kernel.ld.
RUN curl -fsSL "$VLINK_URL" -o vlink.tar.gz \
    && tar xzf vlink.tar.gz \
    && cd vlink \
    && mkdir -p objects \
    && make \
    && install -m 0755 vlink "$PREFIX/bin/" \
    && cd /src && rm -rf vlink vlink.tar.gz

# --- vbcc ------------------------------------------------------------------
# Only the compiler proper (vbccm68k) is needed. The kernel is freestanding:
# it supplies its own headers, never links a libc, and src/kernel/Makefile
# invokes vbccm68k directly rather than through the `vc` frontend - so the
# separate vbcc target/config package (and the $VBCC env var) is not needed.
#
# stdin is closed so that if a future release's dtgen turns interactive the
# build fails fast instead of hanging forever on a prompt no one can see.
RUN curl -fsSL "$VBCC_URL" -o vbcc.tar.gz \
    && tar xzf vbcc.tar.gz \
    && cd vbcc \
    && mkdir -p bin \
    && make TARGET=m68k < /dev/null \
    && install -m 0755 bin/vbccm68k "$PREFIX/bin/" \
    && cd /src && rm -rf vbcc vbcc.tar.gz

# --- record what we actually got -------------------------------------------
# Each tool prints its version banner before complaining about missing input,
# so run them bare and keep the first few lines.
RUN { \
      echo "toolchain built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
      echo; \
      "$PREFIX/bin/vasmm68k_mot" < /dev/null 2>&1 | head -3; \
      echo; \
      "$PREFIX/bin/vlink" < /dev/null 2>&1 | head -2; \
      echo; \
      "$PREFIX/bin/vbccm68k" < /dev/null 2>&1 | head -2; \
    } > "$PREFIX/VERSIONS"; \
    cat "$PREFIX/VERSIONS"

# ===========================================================================
# Stage 2 - the build image
# ===========================================================================
# Carries only what the project's Makefiles invoke. No compiler sources, no
# FS-UAE: `make run` and ./debug.py need a display and a TCP port, so they
# stay on the host.
# ===========================================================================
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        make \
        mtools \
    && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /opt/amiga-toolchain/bin/ /usr/local/bin/
COPY --from=toolchain /opt/amiga-toolchain/VERSIONS /opt/amiga-toolchain/VERSIONS

# `make deploy` runs mcopy against harddrives/boot.hdf, whose geometry is not
# a standard floppy's - without this mtools refuses with "Total number of
# sectors not a multiple of sectors per track". The container also runs as
# the caller's uid, which has no home directory here and so no ~/.mtoolsrc.
ENV MTOOLS_SKIP_CHECK=1

WORKDIR /work
CMD ["make", "all"]
