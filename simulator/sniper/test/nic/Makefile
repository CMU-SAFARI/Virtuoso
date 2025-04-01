CC = g++
CFLAGS = -g -Wall  -I../../include
LDFLAGS = 

all: write_to_nic read_from_nic

write_to_nic: write_to_nic.cpp
	$(CC) $(CFLAGS) -o write_to_nic write_to_nic.cpp $(LDFLAGS)

read_from_nic: read_from_nic.cpp
	$(CC) $(CFLAGS) -o read_from_nic read_from_nic.cpp $(LDFLAGS)