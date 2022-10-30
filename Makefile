piep: piep.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -lasound -lm -opiep piep.c
