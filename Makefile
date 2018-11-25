tex.out: tex.c
	$(CC) tex.c -o tex -Wall -Wextra -pedantic -std=c99
