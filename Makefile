OS := $(shell uname)
OPTIONS:= 

ifeq ($(OS),Darwin)
	OPTIONS += -framework OpenCL
else
	OPTIONS += -l OpenCL
endif

main: DEFLATE.c
	gcc -Wall -g DEFLATE.c -o DEFLATE $(OPTIONS)

clean:
	rm -rf DEFLATE
