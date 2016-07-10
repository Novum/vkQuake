INPUT := gfx/conback.lmp \
	maps/e1m1.ent \
	maps/e1m2.ent \
	maps/e1m4.ent \
	maps/e2m2.ent \
	maps/e2m3.ent \
	maps/e2m7.ent \
	default.cfg

OUTPUT := quakespasm.pak

$(OUTPUT): $(INPUT)
	./mkpak.sh $(INPUT) > $(OUTPUT)

.PHONY: clean
clean:
	rm -f $(OUTPUT)

