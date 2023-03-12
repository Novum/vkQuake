INPUT := gfx/conback.lmp \
	gfx/mainmenu2.lmp \
	gfx/p_mods.lmp \
	maps/e1m1@c49d.ent \
	maps/e1m2@0caa.ent \
	maps/e1m4@958e.ent \
	maps/e2m2@fbfe.ent \
	maps/e2m3@237a.ent \
	maps/e2m7@10a8.ent \
	maps/r2m3@63f4.ent \
	maps/r2m3@76ec.ent \
	default.cfg

OUTPUT_NAME := vkquake.pak
OUTPUT := ../../Quake/$(OUTPUT_NAME)
OUTPUT_EMBEDDED := ../../Quake/embedded_pak.c

$(OUTPUT_EMBEDDED): bintoc $(OUTPUT) $(OUTPUT).deflate
	./bintoc $(OUTPUT) $(OUTPUT_NAME) $(OUTPUT_EMBEDDED)

$(OUTPUT).deflate: $(OUTPUT)
	zopfli --deflate $(OUTPUT)

$(OUTPUT): mkpak $(INPUT)
	./mkpak $(OUTPUT) $(INPUT)

mkpak: mkpak.c
	cc mkpak.c -o mkpak --std=gnu11 -g

bintoc: ../../Shaders/bintoc.c
	cc ../../Shaders/bintoc.c -o bintoc --std=gnu11 -g

.PHONY: clean
clean:
	rm -f mkpak
	rm -f bintoc
