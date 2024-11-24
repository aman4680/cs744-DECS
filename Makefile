# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LIBS = -lcurl -lpthread -lcjson

# Output files
DATA = getdata
BROKER = broker
PUBLISHER = publisher
SUBSCRIBER = subscriber

# Source files
DATA_SRC = getdata.c
BROKER_SRC = broker.c
PUBLISHER_SRC = publisher.c
SUBSCRIBER_SRC = subscriber.c

# Default target: build everything
all: $(DATA) $(BROKER) $(PUBLISHER) $(SUBSCRIBER)

# Build data
$(DATA): $(DATA_SRC)
	$(CC) $(CFLAGS) -o $(DATA) $(DATA_SRC) $(LIBS)

# Build broker
$(BROKER): $(BROKER_SRC)
	$(CC) $(CFLAGS) -o $(BROKER) $(BROKER_SRC) $(LIBS)

# Build publisher
$(PUBLISHER): $(PUBLISHER_SRC)
	$(CC) $(CFLAGS) -o $(PUBLISHER) $(PUBLISHER_SRC) $(LIBS)

# Build subscriber
$(SUBSCRIBER): $(SUBSCRIBER_SRC)
	$(CC) $(CFLAGS) -o $(SUBSCRIBER) $(SUBSCRIBER_SRC) $(LIBS)

# Clean up executables
clean:
	rm -f $(DATA) $(BROKER) $(PUBLISHER) $(SUBSCRIBER)
