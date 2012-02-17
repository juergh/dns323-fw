KERNEL := linux-2.6.12.6-arm1
OUTPUT := output

all:

$(OUTPUT):
	mkdir $(OUTPUT)

menuconfig: $(OUTPUT)
	cp -f kernel.config $(KERNEL)/.config
	make -C $(KERNEL) ARCH=arm menuconfig
	cp -f $(KERNEL)/.config kernel.config

kernel: $(OUTPUT)
	cp -f kernel.config $(OUTPUT)/.config
	make -C $(KERNEL) O=../$(OUTPUT) ARCH=arm \
		CROSS_COMPILE=arm-linux-uclibc- uImage modules

reloaded: $(OUTPUT)

clean:
	make -C $(KERNEL) mrproper
	rm -rf $(OUTPUT)
