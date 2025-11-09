HOST_CC = gcc
INPUT := vq_pak_contents.txt
OUTPUT_NAME := vkquake.pak
OUTPUT := ../../Quake/$(OUTPUT_NAME)
OUTPUT_EMBEDDED := ../../Quake/embedded_pak.c

$(OUTPUT_EMBEDDED): bintoc $(OUTPUT)
	./../../Shaders/bintoc -c $(OUTPUT) $(OUTPUT_NAME) $(OUTPUT_EMBEDDED)

$(OUTPUT): mkpak $(INPUT)
	./mkpak $(OUTPUT) . $(INPUT)

mkpak: mkpak.c
	$(HOST_CC) mkpak.c -o mkpak --std=gnu11 -g

bintoc: ../../Shaders/bintoc.c 
	$(HOST_CC) -I../../Quake/ ../../Shaders/bintoc.c -o ../../Shaders/bintoc --std=gnu11 -g 

.PHONY: clean
clean:
	rm -f mkpak
	rm -f ../../Shaders/bintoc
