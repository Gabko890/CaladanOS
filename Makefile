DOCKER_IMAGE ?= caladanos-build
DOCKER_TAG   ?= latest
DOCKER       ?= docker

ARCH         ?= x86_64
OPT          ?= Debug

# Common docker run options: mount repo, set workdir
define DOCKER_RUN
$(DOCKER) run --rm -u $(shell id -u):$(shell id -g) \
  -v $(PWD):/work \
  -w /work \
  $(DOCKER_IMAGE):$(DOCKER_TAG)
endef

.PHONY: docker-image shell kernel iso clean

docker-image:
	$(DOCKER) build -t $(DOCKER_IMAGE):$(DOCKER_TAG) .

shell: docker-image
	$(DOCKER_RUN) bash -i

# Build kernel ELF via Zig->obj + NASM->obj + x86_64-elf-ld
kernel: docker-image
	zig build kernel -Doptimize=$(OPT) -Duse_docker_tools=true

# Build hybrid BIOS+UEFI ISO with GRUB tools
iso: docker-image
	zig build iso -Doptimize=$(OPT) -Duse_docker_tools=true

clean:
	rm -rf zig-out .zig-cache
