.SILENT: run clean

run: src/FS.c
	gcc src/FS.c -o FS
	./FS

clean:
	rm ./FS