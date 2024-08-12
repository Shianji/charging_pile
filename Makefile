all : server client

objects=cJSON.o gbt27930-2015.o common.o

server : $(objects) server.o
	gcc $^ -o $@ -ljson-c

client : $(objects) client.o
	gcc $^ -o $@ -ljson-c

%.o:%.c
	gcc -c $< -o $@

clean:
	-rm -rf *.o server client *.csv