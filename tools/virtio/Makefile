# SPDX-License-Identifier: GPL-2.0
all: test mod virtio-ivshmem-block virtio-ivshmem-console
test: virtio_test vringh_test vhost_net_test
virtio_test: virtio_ring.o virtio_test.o
vringh_test: vringh_test.o vringh.o virtio_ring.o
vhost_net_test: virtio_ring.o vhost_net_test.o
virtio-ivshmem-block: virtio-ivshmem-block.o
virtio-ivshmem-console: virtio-ivshmem-console.o

try-run = $(shell set -e;		\
	if ($(1)) >/dev/null 2>&1;	\
	then echo "$(2)";		\
	else echo "$(3)";		\
	fi)

__cc-option = $(call try-run,\
	$(1) -Werror $(2) -c -x c /dev/null -o /dev/null,$(2),)
cc-option = $(call __cc-option, $(CC),$(1))

CFLAGS += -g -O2 -Werror -Wno-maybe-uninitialized -Wall -I. -I../include/ -I ../../usr/include/ -Wno-pointer-sign -fno-strict-overflow -fno-strict-aliasing -fno-common -MMD -U_FORTIFY_SOURCE -include ../../include/linux/kconfig.h $(call cc-option,-mfunction-return=thunk) $(call cc-option,-fcf-protection=none) $(call cc-option,-mindirect-branch-register)

CFLAGS += -pthread
LDFLAGS += -pthread
vpath %.c ../../drivers/virtio ../../drivers/vhost
mod:
	${MAKE} -C `pwd`/../.. M=`pwd`/vhost_test V=${V}

#oot: build vhost as an out of tree module for a distro kernel
#no effort is taken to make it actually build or work, but tends to mostly work
#if the distro kernel is very close to upstream
#unsupported! this is a development tool only, don't use the
#resulting modules in production!
OOT_KSRC=/lib/modules/$$(uname -r)/build
OOT_VHOST=`pwd`/../../drivers/vhost
#Everyone depends on vhost
#Tweak the below to enable more modules
OOT_CONFIGS=\
	CONFIG_VHOST=m \
	CONFIG_VHOST_NET=n \
	CONFIG_VHOST_SCSI=n \
	CONFIG_VHOST_VSOCK=n \
	CONFIG_VHOST_RING=n
OOT_BUILD=KCFLAGS="-I "${OOT_VHOST} ${MAKE} -C ${OOT_KSRC} V=${V}
oot-build:
	echo "UNSUPPORTED! Don't use the resulting modules in production!"
	${OOT_BUILD} M=`pwd`/vhost_test
	${OOT_BUILD} M=${OOT_VHOST} ${OOT_CONFIGS}

oot-clean: oot-build
oot: oot-build
oot-clean: OOT_BUILD+=clean

.PHONY: all test mod clean vhost oot oot-clean oot-build
clean:
	${RM} *.o vringh_test virtio_test vhost_net_test vhost_test/*.o \
              vhost_test/.*.cmd vhost_test/Module.symvers \
              vhost_test/modules.order *.d \
              virtio-ivshmem-block virtio-ivshmem-console
-include *.d
