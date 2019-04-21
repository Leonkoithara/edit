tex.out: tex.c
	$(CC) tex.c -o edit -Wall -Wextra -pedantic -std=c99 -g
