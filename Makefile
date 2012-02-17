ORIG := dns323_fw_109
NEW  := dns323_fw_109-naspkg0

FW_TOOLS := ../dns323-fw-tools

all: unpack patch pack

unpack:
	$(FW_TOOLS)/fw-unpack $(ORIG) $(ORIG).d

patch:
	tar -C $(NEW).d -v -c -f - . | tar -C $(ORIG).d -x -f -

pack:
	$(FW_TOOLS)/fw-pack $(ORIG).d $(NEW)

clean:
	$(FW_TOOLS)/fw-clean $(ORIG).d
	find $(NEW).d \( -name '*~' \) -type f -print | xargs rm -f