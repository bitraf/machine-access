all: kv
.PHONY: all

kv: main.c ../kv.c ../include/kv.h
	gcc -I $(dir $(filter %.h,$^)) $(filter %.c, $^) -o $@

clean:
	rm -f kv
.PHONY: clean
