UNAME ?= $(shell uname -r)

obj-m += kwasmjit.o

all:
	 make -C /lib/modules/$(UNAME)/build M=$(PWD) modules

clean:
	 make -C /lib/modules/$(UNAME)/build M=$(PWD) clean
