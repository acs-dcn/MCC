CC=gcc
CFLAGS=-c -Wall
LDFLAGS=
#SOURCES=aes.c cipher.c cipher_wrap.c gcm.c aes_utils.c main.c
SOURCES=aes.c cipher.c cipher_wrap.c gcm.c
OBJECTS=$(SOURCES:.c=.o)
#EXECUTABLE=out
AES_LIB=libaes.a

#all: $(SOURCES) $(EXECUTABLE) $(AES_LIB)
all: $(SOURCES) $(AES_LIB)

#$(EXECUTABLE): $(OBJECTS) 
#	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

$(AES_LIB): $(OBJECTS)
	ar rvs ../../lib/$@ $(OBJECTS)
	rm -f $(OBJECTS)

clean:
	rm -rf *.o ../../lib/libaes.a

