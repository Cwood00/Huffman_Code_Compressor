all:
	gcc main.c -O3 -Wall -o huffman.out

debug:
	gcc main.c -g -Wall -o huffman.out

clean:
	rm -f huffman.out