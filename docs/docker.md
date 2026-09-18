# Containerised build

Builds the ROM and kernel without installing anything on the host. The image
compiles vasm, vlink and vbcc from upstream source, then this repo's normal
Makefiles run inside it against a bind mount of the working tree.

## Usage

```bash
make docker-build     # build ROM + kernel in the container
make docker-versions  # show which toolchain releases the image has
make docker-shell     # interactive shell, toolchain on PATH
```

Artifacts land in the usual places — `src/rom/build/kick.rom` and
`src/kernel/build/SYSTEM.BIN` — and are owned by you, not by root: the
container runs as your uid/gid.

Any target can be run inside the container:

```bash
make docker-make DOCKER_TARGET=rom
make docker-make DOCKER_TARGET=clean
```

## What is and isn't in the image

| Tool | In image | Why |
|------|----------|-----|
| vasm (`vasmm68k_mot`) | yes | assembles the ROM and the kernel's `.s` files |
| vlink | yes | links `SYSTEM.BIN` (`-b rawbin1`) |
| vbcc (`vbccm68k`) | yes | compiles the kernel's C |
| mtools | yes | `make deploy` copies `SYSTEM.BIN` into `boot.hdf` |
| FS-UAE | **no** | needs a display and a TCP port |

So `make run` and `./debug.py` still require FS-UAE installed locally. The
container is for producing binaries, not for running them.

Only the compiler proper (`vbccm68k`) is installed, not the full vbcc
target/config package. The kernel is freestanding — it supplies its own
headers and never links a libc — and `src/kernel/Makefile` invokes
`vbccm68k` directly rather than through the `vc` frontend, so neither the
target package nor the `$VBCC` environment variable is needed.

## "Latest" and the layer cache

vasm, vlink and vbcc publish no versioned release URLs. The tarballs the
Dockerfile fetches always serve the current release, so a fresh build picks
up whatever is latest — and, by the same token, the image is not
reproducible across time.

Docker's layer cache will otherwise keep handing you the release you first
built with. To move forward deliberately:

```bash
make docker-image-refresh   # re-fetch everything, ignoring the cache
```

`make docker-versions` prints the version banners recorded when the image was
built, so you can tell what a given image actually contains.

To pin or redirect a source, override the build args:

```bash
docker build -t mariposa-os-build \
  --build-arg VASM_URL=http://example/vasm-1.9.tar.gz docker
```

## Notes

- The base is `debian:bookworm-slim` on purpose. It ships GCC 12; GCC 14
  (trixie) turns implicit function declarations and other C99 removals into
  hard errors, which breaks these older C codebases.
- The build context is `docker/`, which holds only the Dockerfile. Sources
  arrive via the bind mount, so nothing large is shipped to the daemon and no
  `.dockerignore` is needed.
- `MTOOLS_SKIP_CHECK=1` is set in the image. `harddrives/boot.hdf` does not
  have floppy geometry, and without it mtools refuses with "Total number of
  sectors not a multiple of sectors per track".
- Current vasm is also what allows `src/kernel/crt0.s` to use `section .text`;
  releases before roughly 1.7 only knew `CODE`/`DATA`/`BSS`.
