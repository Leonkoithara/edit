tex.out: tex.c
	$(CC) tex.c -o tex.out -Wall -Wextra -pedantic -std=c99
