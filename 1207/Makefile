CC = gcc
CFLAGS = -Wall -I.
LDLIBS =

SERVER_DIR = server_folder
CLIENT_DIR = client_folder
CODE_DIR = code

all: $(SERVER_DIR)/server $(CLIENT_DIR)/client

$(SERVER_DIR)/server: $(CODE_DIR)/server.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	mkdir -p $(SERVER_DIR)
	cp users.csv $(SERVER_DIR)

$(CLIENT_DIR)/client: $(CODE_DIR)/client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	mkdir -p $(CLIENT_DIR)

$(CODE_DIR)/%.o: $(CODE_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CODE_DIR)/server.o $(CODE_DIR)/client.o
	rm -rf $(SERVER_DIR) $(CLIENT_DIR)
