#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_PATH_LENGTH 512

typedef struct map_node_t {
    int key;
    int loggedIn;
    char workingDirectory[512];
    char username[100];
    char password[100];
    struct map_node_t* next;
} map_node_t;

typedef struct map_t{
    map_node_t* head;
    size_t size;
} map_t;

// =======================================================================================
// map function declaration
// add key-value pair to map
int add_map(map_t* map, int key, char username[], char password[], char cwd[]){
    map_node_t* new_node = malloc(sizeof(map_node_t));
    if (!new_node){
        return 0;
    }
    new_node->key = key;
    new_node->loggedIn = 0;
    strncpy(new_node->username, username, 100); 
    strncpy(new_node->password, password, 100);

    // change the current directory to {current directory}/{username}
    snprintf(new_node->workingDirectory, MAX_PATH_LENGTH, "%s/%s", cwd, username);
    
    new_node->next = NULL;
    if (!map->head){
        map->head = new_node;
    } 
    else{
        map_node_t* current = map->head;
        while (current->next != NULL){
            current = current->next;
        }
        current->next = new_node;
    }
    map->size++;
    return 1;
}

// gets the node for the specific fd
map_node_t* extract_map(map_t* map, int key){
    map_node_t* current = map->head;
    while (current != NULL){
        if (current->key == key){
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// remove the node with the specific fd
int delete_map(map_t* map, int key){
    map_node_t* current = map->head;
    map_node_t* previous = NULL;
    while (current != NULL){
        if (current->key == key){
            if (previous == NULL){
                map->head = current->next;
            } 
            else{
                previous->next = current->next;
            }
            free(current);
            map->size--;
            return 1;
        }
        previous = current;
        current = current->next;
    }
    return 0;
}

// =======================================================================================
// command function declarations
int userCommand(char* username, map_t* map, int sd, char* initial_directory);
int passwordCommand(char* password, map_t* map, int sd);
void portCommand(int control_socket, char* argument);

// Command 1: USER username
// Command 2: PASS password
// Command 3: QUIT
// Command 4: CWD
// Command 5: PWD
// Command 6: PORT (LIST, RETR, and STOR)
// =======================================================================================
// variables declarations
int PORTNO = 21;
int BUFFSIZE = 1024;
int MAX_CONNECTIONS = 10;
int PORTNODATA = 20;
// =======================================================================================
// error messages
char BAD_SEQ_COMMAND[] = "503 Bad sequence of commands.";
char NOT_LOGGED_IN[] = "530 Not logged in.";
char INVALID_FILE_DIR[] = "550 No such file or directory.";
char INVALID_COMMAND[] = "202 Command not implemented.";
char COMMAND_ERROR[] = "202 Command not implemented.";
char DT_RESPONSE[] = "150 File status okay; about to open data connection.";
// =======================================================================================

int main(int argc, char** argv){

	// set sockets to create the connection channel
	int server_sd, client_sd, max_fd;
	struct sockaddr_in server_addr, client_addr;
	server_sd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_sd < 0){
		perror("server socket creation failed");
		exit(-1);
	}

    // make the socket reusable 
    int optval = 1;
    setsockopt(server_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORTNO);

	// bind the server socket
	if (bind(server_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("bind failed");
        exit(-1);
    }

    // set socket persistent to wait for connection
    if (listen(server_sd, MAX_CONNECTIONS) < 0){
    	perror("listen failed");
    	close(server_sd);
    	exit(-1);
    }

    char buffer[BUFFSIZE];
    printf("FTP server listening on port %d\n", PORTNO);

    // select() with multiple connections
    fd_set full_fdset;
    fd_set read_fdset;
    FD_ZERO(&full_fdset);
    FD_SET(server_sd, &full_fdset);
    max_fd = server_sd;

    // create maps to handle multiple connections with multiple clients
    map_t* map = malloc(sizeof(map_t));
    map->head = NULL;
    map->size = 0;

    // get base directory
	char base_directory[1000];
	memset(base_directory, 0, sizeof(base_directory));
	if (getcwd(base_directory, sizeof(base_directory)) == NULL){
		perror("getcwd failed");
		exit(-1);
	}

	while (1){
		// clear the client address for every connection
		memset(&client_addr, 0, sizeof(client_addr));
		memset(&buffer, 0, BUFFSIZE);
		read_fdset = full_fdset;

		if (select(max_fd+1, &read_fdset, NULL, NULL, NULL) < 0){
			perror("select failed");
			exit(-1);
		}

		// iterate through fd set to handle commands accordingly
		for (int fd = 3; fd <= max_fd; fd++){

			if (FD_ISSET(fd, &read_fdset)){

				// if server_sd: accept connections and add new client_sd
				if (fd == server_sd){
					socklen_t client_addr_len = sizeof(client_addr);
					client_sd = accept(server_sd, (struct sockaddr *)&client_addr, &client_addr_len);

					if (client_sd < 0){
						perror("accept failed");
						continue; 
					}
					printf("Established client connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

					// update the full_fdset
					FD_SET(client_sd, &full_fdset);

					char NEW_USER_RESPONSE[] = "220 Service ready for new user.";
					send(client_sd, NEW_USER_RESPONSE, strlen(NEW_USER_RESPONSE), 0);

					if (max_fd < client_sd){
						max_fd = client_sd;
					}
				}

				// if client_sd: handle commands accordingly
				else{
                    memset(buffer, 0, BUFFSIZE);
                    int space = recv(fd, buffer, sizeof(buffer), 0);

                    if (space == 0){
                        printf("connection closed from client side \n");
                        close(fd);

                        FD_CLR(fd, &full_fdset);
                        if (fd == max_fd){
                            for (int i = max_fd; i>=3; i--){
                                if (FD_ISSET(i, &full_fdset)){
                                    max_fd = i;
                                    break;
                                }
                            }
                        }
                        if (extract_map(map, fd)){
                            delete_map(map, fd);
                        }
                    }

                    else {
    					// Command 1: USER username
                            // continue if user entered valid username
    					if (!strncmp(buffer, "USER", 4)){
    						if (extract_map(map, fd)){
    							send(fd, BAD_SEQ_COMMAND, strlen(BAD_SEQ_COMMAND), 0);
    							continue;
    						}

                            // continue if user logged in
    						else if(extract_map(map, fd) && extract_map(map, fd)->loggedIn){
    							send(fd, BAD_SEQ_COMMAND, strlen(BAD_SEQ_COMMAND), 0);
    							continue;
    						}

    						// retrieve the username
    						char username[128];
    						memset(&username, 0, sizeof(username));
    						strncpy(username, buffer+5, strlen(buffer)-4);

                            // call internal userCommand function to login + update directory
    						int response = userCommand(username, map, fd, base_directory);
    						if (response){
    							char USERNAME_RESPONSE[] = "331 Username OK, need password.";
    							send(fd, USERNAME_RESPONSE, strlen(USERNAME_RESPONSE), 0);
    						}
    						else{
    							send(fd, NOT_LOGGED_IN, strlen(NOT_LOGGED_IN), 0);
    						}
    						memset(&buffer, 0, sizeof(buffer));
    					}

    					// Command 2: PASS password
                            // continue if use entered valid username
    					else if(!strncmp(buffer, "PASS", 4)){
    						if (!extract_map(map, fd)){
    							send(fd, BAD_SEQ_COMMAND, strlen(BAD_SEQ_COMMAND), 0);
    							continue;
    						}

    						// continue if user logged in 
    						else if(extract_map(map, fd) && extract_map(map, fd)->loggedIn){
    							send(fd, BAD_SEQ_COMMAND, strlen(BAD_SEQ_COMMAND), 0);
    							continue;
    						}

    						// retrieve the password
    						char password[128];
    						memset(&password, 0, sizeof(password));
    						strncpy(password, buffer+5, strlen(buffer)-4);

                            // call internal userCommand function to login when password matches
    						int response = passwordCommand(password, map, fd);
    						if (response){
    							char PASS_RESPONSE[] = "230 User logged in, proceed.";
    							send(fd, PASS_RESPONSE, strlen(PASS_RESPONSE), 0);
    						}
    						else{
    							send(fd, NOT_LOGGED_IN, strlen(NOT_LOGGED_IN), 0);
    						}
    						memset(&buffer, 0, sizeof(buffer));
    					}

    					// Command 3: QUIT
                        else if(!strncmp(buffer, "QUIT", 4)) {
                            // remove client from map
                            if(extract_map(map, fd)) {
                                delete_map(map, fd);
                            }

                            // send message to client
                            char QUIT_RESPONSE[] = "221 Service closing control connection.";
                            send(fd, QUIT_RESPONSE,strlen(QUIT_RESPONSE),0);
                            printf("connection closed from client side \n");

                            // close connection
                            close(fd);
                            FD_CLR(fd, &full_fdset);
                            memset(&buffer, 0, sizeof(buffer));
                        }

    					// check if logged in successfully before doing CWD, PWD, PORT, LIST, RETR and STOR
                        else if(!extract_map(map,fd) || !extract_map(map,fd)->loggedIn) {
                            send(fd, NOT_LOGGED_IN,strlen(NOT_LOGGED_IN),0);
                            memset(&buffer, 0, sizeof(buffer));
                            continue;
                        }

                        // Command 4: CWD
                        else if(!strncmp(buffer, "CWD", 3)){
                        	// retrieve directory from user input
                        	char path[256];
                        	memset(&path, 0, sizeof(path));
                        	strncpy(path, buffer+4, strlen(buffer)-3);

                        	// change directory
                        	int response = chdir(path);
                        	if (response != 0){
                        		send(fd, INVALID_FILE_DIR, strlen(INVALID_FILE_DIR), 0);
                        		memset(&path, 0, sizeof(path));
                        	}

                        	// if directory is valid:
                        	else{
                        		memset(&path, 0, sizeof(path));
                        		if (getcwd(path, sizeof(path)) == NULL){
                        			perror("getcwd failed");
                        			exit(-1);
                        		}
                                // retreive the new and udpate current directory
                        		memset(extract_map(map, fd)->workingDirectory, 0, sizeof(extract_map(map, fd)->workingDirectory));
                        		strcpy(extract_map(map, fd)->workingDirectory, path);
                        		char res[500];

                        		char CWD_RESPONSE[] = "200 directory changed to ";
                        		strcpy(res, CWD_RESPONSE);
                        		strcat(res, path);
                        		send(fd, res, strlen(res), 0);
                        		memset(&path, 0, sizeof(path));
                        	}
                        	memset(&buffer, 0, sizeof(buffer));
                        }

                        // Command 5: PWD
                        else if(!strncmp(buffer, "PWD", 3)){
                                // retrive current directory
                                char resp[500];
                                memset(&resp, 0, sizeof(resp));

                                char PWD_RESPONSE[] = "257 ";
                                strcpy(resp, PWD_RESPONSE);
                                strcat(resp, extract_map(map,fd)->workingDirectory);
                                send(fd,resp,strlen(resp),0);
                                memset(&buffer, 0, sizeof(buffer));
                            }

                        // Command 6: PORT (LIST, RETR, and STOR)
                        else if (!strncmp(buffer, "PORT", 4)) {
                            
                            // change directory to current directory
                            if (chdir(extract_map(map,fd)->workingDirectory) != 0){
                                printf("chdir failed\n");
                                return -1;
                            }

                            // retrieve client input
                            char args[128];
                            memset(&args, 0, sizeof(args));
                            strncpy(args, buffer+5, strlen(buffer)-4);

                            // handle LIST, STOR, RETR
                            portCommand(fd, buffer);
                            memset(buffer,0,sizeof(buffer));
                        }  

                        // invalid commands
                        else{
                        	send(fd, INVALID_COMMAND, strlen(INVALID_COMMAND), 0);
                        }
                    }
				}	
			}
		}
	}

    // free memory of map and its nodes
    map_node_t* current = map->head;
    while (current) {
        map_node_t* next = current->next;
        free(current);
        current = next;
    }
    free(map);
    
	return 0;
}

// internal function for Command 1: USER
int userCommand(char* username, map_t* map, int sd, char* base_directory){
	// change to base directory
	if (chdir(base_directory) != 0){
		printf("chdir failed");
	}

	// check current users
	FILE* fp = fopen("users.csv", "r");
    if (!fp){
        perror("opening users.csv failed");
        exit(-1);
    }
    char line[100];
    char* token;

    // if the username exists:
    while (fgets(line, sizeof(line), fp)){
        // split line into username and password
        char cwd[256];
        token = strtok(line, " ");
        //print out the key-value pair
        if (!strcmp(token, username)){
            token = strtok(NULL, " ");
            token[strcspn(token, "\n")] = 0;
            fclose(fp);

            if (getcwd(cwd, sizeof(cwd)) == NULL){
                perror("getcwd failed");
                exit(-1);
            }

            // insert socket descripter into the map to retrieve details
            add_map(map, sd, username, token, cwd);
            return 1;
        }
        memset(line,0,sizeof(line));
    }
    
    return 0;
}

// internal function for Command 2: PASS
int passwordCommand(char* password, map_t* map, int sd){
	// retrieve map using socket descripter
    map_node_t* node = extract_map(map, sd); 

    // if the password matches, log in 
    if(!strcmp(node->password, password)){
        node->loggedIn = 1;
        return 1;
    }
    return 0;
}

// internal function for Command 6: PORT
void portCommand(int control_socket, char* argument){
	// port number
	char port1[5];
	memset(port1, 0, sizeof(port1));
	char port2[5];
	memset(port2, 0, sizeof(port2));

	// IP address
	char ip[16];
	memset(ip, 0, sizeof(ip));

	char *ptr;
	ptr = strtok(argument+5, ",");
	int i = 0;
	int count = 0;

	// process the client request
	while (ptr != NULL) {
        if(i<=2){
            count += sprintf(&ip[count], "%s.", ptr);
        } 
        else if(i==3){
            count += sprintf(&ip[count], "%s", ptr);
        } 
        else if(i==4){
            memcpy(port1, ptr, strlen(ptr));
        } 
        else if(i==5){
            memcpy(port2, ptr, strlen(ptr));
        }
        i+=1;
        ptr = strtok (NULL, ",");
    }

    // get port number
    int port = atoi(port1)*256+atoi(port2);

    // process the client address on data transfer channel
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr  = inet_addr(ip);
    client_addr.sin_port = htons(port);

    char PORT_RESPONSE[] = "200 PORT command successful.";
    send(control_socket, PORT_RESPONSE, strlen(PORT_RESPONSE), 0);

    // POST request options from client: LIST, STOR, RETR
    char buffer[BUFFSIZE];
    memset(&buffer, 0, sizeof(buffer));
    char command[4];
    memset(&command, 0, sizeof(command));
    char filename[256];
    memset(&filename, 0, sizeof(filename));

    // store request into the array
    int bytes = recv(control_socket,buffer, sizeof(buffer), 0);
    if(bytes<0){
        perror("recv failed");
        exit(-1);
    }

    // split buffer into command and filename
    sscanf(buffer,"%s %s",command,filename);

    if(!strncmp(command, "LIST", 4) || !strncmp(command, "STOR", 4)){
        send(control_socket, DT_RESPONSE, strlen(DT_RESPONSE),0);
    } 

    else if(!strncmp(command, "RETR", 4)){
        FILE *fp = fopen(filename,"rb");
        // if file does not exist:
        if (!fp){
            send(control_socket, INVALID_FILE_DIR, strlen(INVALID_FILE_DIR),0);
            return;
        }
        // if file exists:
        else{
            fclose(fp);
            send(control_socket, DT_RESPONSE, strlen(DT_RESPONSE),0);
        }
    }
    else{
   		// invalid PORT command
        send(control_socket, INVALID_COMMAND, strlen(INVALID_COMMAND),0);
    }

    // fork to handle data connection
    int pid = fork();
    if (pid == -1){
        perror("fork failed");
        exit(-1);
    } 

    else if (pid == 0){
        // socket for data connection
        int data_socket = socket(AF_INET, SOCK_STREAM, 0);
        int optval = 1;
        setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));      
        if (data_socket == -1){
            perror("socket creation failed");
            exit(-1);
        }

        // new address for data connection
        struct sockaddr_in data_addr;
        data_addr.sin_family = AF_INET;
        data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        data_addr.sin_port = htons(PORTNODATA);
        
        // bind socket to new address
        if (bind(data_socket, (struct sockaddr*) &data_addr, sizeof(data_addr)) == -1){
            perror("bind failed");
            exit(-1);
        }

        // on channel, connect the server's data socket to client 
        while(connect(data_socket, (struct sockaddr*) &client_addr, sizeof(client_addr)) == -1){
        }

        // handle subrequests for POST
        // LIST
        if (!strncmp(command, "LIST", 4)){
            FILE* fp = popen("ls", "r");
            if (!fp){
            	perror("ls() failed");
            	close(data_socket); 
            	exit(-1);
            }

            // use fgets to print into a list
            char buffer[BUFFSIZE];
            char* line;
            memset(&buffer, 0, sizeof(buffer));
            while(fgets(buffer, sizeof(buffer), fp)){
                line = strchr(buffer, '\n')+1;
                send(data_socket, buffer, line-buffer, 0);
                memset(&buffer, 0, sizeof(buffer));
            }
            pclose(fp);

            char DT_SUCCESS[] = "226 Transfer completed";
            send(control_socket, DT_SUCCESS, strlen(DT_SUCCESS),0);
        }

        // STOR
        else if(!strncmp(command, "STOR", 4)){
        	printf("Start downloading the file %s\n", filename);

            // receive file
            char buffer[1000];
            memset(buffer, 0, sizeof(buffer));

            // open a new file
            FILE* fp = fopen(filename, "wb");
            if (fp == NULL){
                perror("file() error");
                fclose(fp);
                return;
            }

            // write until all the bytes are full
            while (1){
                int bytes = recv(data_socket, buffer, sizeof(buffer), 0);
                if (bytes <= 0){
                    break;
                }
                fwrite(buffer, 1, bytes, fp);
                memset(buffer, 0, sizeof(buffer));
            }
            memset(buffer, 0, sizeof(buffer));
            fclose(fp);

            // print + send messages
        	printf("Downloading complete\n");
            char DT_SUCCESS[] = "226 Transfer completed";
            send(control_socket, DT_SUCCESS, strlen(DT_SUCCESS),0);
        }

        // RETR
        else if(!strncmp(command, "RETR", 4)){
            FILE *fp = fopen(filename, "rb");
		    printf("Start sending the file %s\n", filename);
            
            // send files
            char buffer[1000];
            memset(buffer, 0, sizeof(buffer));

            // send until all the bytes are full
            while (1){
                int bytes = fread(buffer, sizeof(char), sizeof(buffer), fp);
                if (bytes <= 0){
                    break;
                }

                int sent_bytes = send(data_socket, buffer, bytes, 0);

                // close receiver if disconnects
                if (sent_bytes < 0){
                    printf("Connection closed from the client side \n");
                    close(data_socket);
                    fclose(fp);
                    return;
                }
                memset(buffer, 0, sizeof(buffer));
            }
            fclose(fp);

            // print + send messages
		    printf("Sending complete.\n");
            char DT_SUCCESS[] = "226 Transfer completed";
            send(control_socket, DT_SUCCESS, strlen(DT_SUCCESS),0);   
        }  
        close(data_socket);
        close(control_socket); 
        exit(EXIT_SUCCESS);
    }
}