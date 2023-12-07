#include<stdio.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<string.h>
#include<pthread.h>
#include<stdlib.h>
#include<ctype.h>
#include<time.h>
#include<time.h>

//global variables
int server_control_port = 21;
int server_data_port = 20;
int client_control_port = 5000;
int client_data_port = 5001;

int login_check (char* server_responce)
{
    if (strncmp(server_responce, "530", 3) == 0 )
    {
        printf("Login failed\n");
        return -1;
    }    
    return 0;

}

char **split_string(const char *command, const char *separator) 
{
    char **args = (char **)malloc(200 * sizeof(char *)); // Allocate memory for the arguments
    if (!args) 
    {   // Make sure the allocation was successful
        perror("Failed to allocate memory for args");
        exit(EXIT_FAILURE);
    }

    char *command_copy = strdup(command);//make a copy of the command because strtok will modify it
    if (!command_copy) { // Make sure the allocation was successful
        perror("Failed to copy command");
        free(args);
        exit(EXIT_FAILURE);
    }

    int arg_count = 0; // Number of arguments
    char *component = strstr(command_copy, separator);  // Find first occurrence of separator
    while (component != NULL && arg_count < 200 - 1) // -1 to leave room for NULL
    {
        *component = '\0';//replace the separator with a null character
        args[arg_count] = command_copy;//add the component to the array of arguments
        arg_count++;//increment the number of arguments
        command_copy = component + strlen(separator);  // Move past the separator
        component = strstr(command_copy, separator);  // Find next occurrence of separator
    }

    // Append the remaining part or the entire string if no separator was found
    if (*command_copy) {
        args[arg_count++] = command_copy;
    }
    args[arg_count] = NULL;//set the last element of the array to NULL

    return args;
}

int PORT_helper(int client_socket, struct sockaddr_in client_address)
{

    // create new stuff for the data transfer

    // create a reusable socket for the client
    int client_data_socket = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(client_data_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if(client_data_socket < 0)
    {
        perror("Socket error\n");
        return -1;
    }

    // make a copy of the client address
    struct sockaddr_in client_data_address;
    client_data_address.sin_family = AF_INET;
    client_data_address.sin_addr.s_addr = client_address.sin_addr.s_addr;

    client_data_port += 1;
	while(1)
    {
		client_data_address.sin_port = htons(client_data_port);
		if(bind(client_data_socket,(struct sockaddr *)&client_data_address,sizeof(client_data_address))>=0)
        {
			break;
		}
        else
        {
            client_data_port += 1;
        }
	}
    

    // get the ip address of the client
    char *client_ip = inet_ntoa(client_address.sin_addr);

    // get the first 4 bytes of the ip address
    char **ip_parts = split_string(client_ip, ".");
    char *ip_part1 = ip_parts[0];
    char *ip_part2 = ip_parts[1];
    char *ip_part3 = ip_parts[2];
    char *ip_part4 = ip_parts[3];

    // get the last 2 bytes of the port number
    int port_part1 = client_data_port / 256;
    int port_part2 = client_data_port % 256;

    // send the PORT command to the server
    char port_command[200];
    memset(port_command, 0, sizeof(port_command));
    sprintf(port_command, "PORT %s,%s,%s,%s,%d,%d", ip_part1, ip_part2, ip_part3, ip_part4, port_part1, port_part2);
    int send_status = send(client_socket, port_command, sizeof(port_command), 0);
    if (send_status == -1)
    {
        perror("Error sending PORT command\n");
        return -1;
    }
    memset(port_command, 0, sizeof(port_command));

    // receive the confirmation response from the server
    // "200 PORT command successful"
    char response[200];
    memset(response, 0, sizeof(response));
    int recv_status = recv(client_socket, response, sizeof(response), 0);
    if (recv_status == -1)
    {
        perror("Error receiving response from server\n");
        return -1;
    }
    printf("%s\n", response);

    // check if PORT is successful
    if (strncmp(response, "200", 3) != 0)
    {
        printf("PORT command failed\n");
        return -1;
    }
	memset(response, 0, sizeof(response));

    // check login
    if (login_check(response) == -1)
    {
        return -2;
    }
    // use -2 to indicate login failure

	/*
    // free the memory allocated for ip_parts
    for (int i = 0; i < 4; i++)
    {
        free(ip_parts[i]);
    }
    free(ip_parts);
	*/

    return client_data_socket;
}

int main()
{       
    // create a reusable socket for the client
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if(client_socket < 0)
    {
        perror("Socket error\n");
        exit(1);
    }

    // set up the server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
	server_address.sin_port = htons(21);
	server_address.sin_addr.s_addr = INADDR_ANY; 

    // need to know the info of the client for PORT command
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    if (getsockname(client_socket, (struct sockaddr *)&client_address, &client_address_len) == -1)
     {
        perror("Getsockname error\n");
        return -1;
    }
    
    // connect to the server
    int connection_status = connect(client_socket, (struct sockaddr*) &server_address, sizeof(server_address));
    if(connection_status < 0)
    {
        perror("Connection error\n");
        exit(1);
    }

    // receive the welcome message from the server "200 Service ready for new user."
    char welcome_message[100];
    memset(welcome_message, 0, sizeof(welcome_message));
    recv(client_socket, welcome_message, sizeof(welcome_message), 0);
    printf("%s\n", welcome_message);
    memset(welcome_message, 0, sizeof(welcome_message));

    // we don't need to seperatly code for login part, since this is determined by the server
    // so we can just iteratively ask for user input and send it to the server
    char input[500];
    memset(input, 0, sizeof(input));

    while (1)
    {
        printf("ftp> ");

        // read the user input until a new line is encountered and discard '\n'
        scanf("%[^\n]%*c", input);

        // Command 1: USER; Command 2: PASS; Command 3: PWD
        // these three commands has similar behavior, so is grouped together
        if (strncmp(input, "USER", 4) == 0 || strncmp(input, "PASS", 4) == 0 || strncmp(input, "PWD", 3) == 0)
        {
            // send the user input to the server
            send(client_socket, input, sizeof(input), 0);
            // receive the response from the server
            char response[100];
            memset(response, 0, sizeof(response));
            recv(client_socket, response, sizeof(response), 0);
            printf("%s\n", response);
            memset(response, 0, sizeof(response));
        }

        // Now, we move on to handle local commands with beginning "!"

        // Command 4: !LIST
        else if (strncmp(input, "!LIST", 5) == 0)
        {
            int execute_status = system("ls");  
            if (execute_status == -1)
            {
                perror("Error executing ls\n");
                continue;
            }
        }

        // Command5: !PWD
        else if (strncmp(input, "!PWD", 5) == 0)
        {
            char directory[100];
            memset(directory, 0, sizeof(directory));
            if (getcwd(directory, sizeof(directory)) == NULL)
            {
                perror("Error getting current working directory\n");
                continue;
                //no need to exit here
            }
            else
            {
                printf("%s\n", directory);
                memset(directory, 0, sizeof(directory));
            }
            
        }

        // Command 6: !CWD folder_name
        else if (strncmp(input, "!CWD", 4) == 0)
        {
            char **parts = split_string(input, " ");
            if (parts[1] == NULL)
            {
                printf("Please enter a folder name\n");
                continue;
            }
            char *folder_name = parts[1];

            int chdir_status = chdir(folder_name);
            if (chdir_status == -1)
            {
                perror("Error changing directory\n");
                continue;
            }
            else
            {
                char directory[100];
                memset(directory, 0, sizeof(directory));
                
                if (getcwd(directory, sizeof(directory)) == NULL)
                {
                    perror("Error getting current working directory\n");
                }
                else
                {
                    printf("Directory changed to %s\n", directory);
                    memset(directory, 0, sizeof(directory));
                }
            }
			/*
            // free the memory allocated for parts
            for (int i = 0; i < 2; i++)
            {
                free(parts[i]);
            }
            free(parts);
			*/   
        }

        // Now we start to handle commands that operate on the server side

        // Command 7: LIST
        else if (strncmp("LIST", input, 4)==0)
        {
            // send the PORT command automatically using PORT_helper to the server
            int client_data_socket = PORT_helper(client_socket, client_address);
            if (client_data_socket < 0)
            {
                printf("socket error\n");
                continue;
            }
            
            // send the LIST command to the server
            int send_status = send(client_socket, input, sizeof(input), 0);
            if (send_status == -1)
            {
                perror("Error sending LIST command\n");
                continue;
            }

            // receive the response from the server
            char response[10000];
            memset(response, 0, sizeof(response));
            int recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);

            // check if the confirmation is "150 File status okay; about to open data connection"
            if (strncmp(response, "150", 3) != 0)
            {
                printf("LIST command failed --150\n");
                continue;
            }

            // now we switch to the data port to receive the file
            // since client is receiving LIST result from the server, so their roles are reversed
            
            // client listens on the data port 
            int listen_status = listen(client_data_socket, 5);
            if (listen_status < 0)
            {
                perror("Error listening on data port\n");
                continue;
            }
            printf("Listening on data port\n");

            // accept the connection from the server 
            int server_data_socket = accept(client_data_socket, NULL, NULL);
            if (server_data_socket < 0)
            {
                perror("Error accepting connection from server\n");
                continue;
            }
            printf("Connection accepted from server\n");
            
            memset(response, 0, sizeof(response));
            //continuouslly receive the file from the server
            while(1)
            {
                int bytes = recv(server_data_socket, response, sizeof(response), 0);
                if(bytes <= 0)
                {
                    break;
                }
                printf("%s", response);
                // reset the buffer
                memset(response, 0, sizeof(response));
            }

            // close the data connection
            close(server_data_socket);
            close(client_data_socket);

            // check for the confirmation from the server
            // it should be "226 Transfer complete"
            recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);
            if (strncmp(response, "226", 3) != 0)
            {
                printf("LIST command failed --226\n");
                continue;
            }
            memset(response, 0, sizeof(response));
        }


        // Command 8: RETR file_name
        // to download a file to the server
        else if (strncmp("RETR", input, 4)==0)
        {
            // check if the file exists
            char **parts = split_string(input, " ");
            char *file_name = parts[1];

            // send the PORT command automatically using PORT_helper to the server
            int client_data_socket = PORT_helper(client_socket, client_address);
            if (client_data_socket < 0)
            {
                continue;
            }

            // send the RETR command to the server
            int send_status = send(client_socket, input, sizeof(input), 0);
            if (send_status == -1)
            {
                perror("Error sending RETR command\n");
                continue;
            }

            // receive the response from the server
            char response[200];
            memset(response, 0, sizeof(response));
            int recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);
            
            // check if the confirmation is "150 File status okay; about to open data connection"
            if (strncmp(response, "150", 3) != 0)
            {
                printf("RETR command failed\n");
                continue;
            }

            // fork a child process to receive the file to the server
            pid_t pid = fork();
            if (pid<0)
            {
                perror("Error forking child process\n");
                continue;
            }
            else if (pid == 0)// in the child process
            {
                // now we switch to the data port to upload the file

                // open the file for writing
                FILE *file = fopen(file_name, "wb");
                if (file == NULL)
                {
                    perror("Error opening file\n");
                    continue;
                }

                char buffer[10000];
                memset(buffer, 0, sizeof(buffer));

                //listen on the data port
                int listen_status = listen(client_data_socket, 5);
                if (listen_status < 0)
                {
                    perror("Error listening on data port\n");
                    continue;
                }

                //accept the connection from the server
                int server_data_socket = accept(client_data_socket, (struct sockaddr*) &client_address, &client_address_len);
                if (server_data_socket < 0)
                {
                    perror("Error accepting connection from server\n");
                    continue;
                }

                // keep receiving the file until there is no byte left to receive
                while(1)
                {
                    int bytes = recv(server_data_socket, buffer, sizeof(buffer), 0);
                    if(bytes <= 0)
                    {
                        break;
                    }
                    fwrite(buffer, 1, bytes, file);
                    // reset the buffer
                    memset(buffer, 0, sizeof(buffer));
                }
                //close the file
                fclose(file);

                // close the data connection
                close(server_data_socket);
                close(client_data_socket);

                
                char response[1000];
                memset(response, 0, sizeof(response));
                int recv_status = recv(client_socket, response, sizeof(response), 0);
                if (recv_status == -1)
                {
                    perror("Error receiving response from server\n");
                    return -1;
                }
                printf("%s\n", response);
                memset(response, 0, sizeof(response));

                exit(0);
  
            }
            else
            {
                // parent process
                // wait for the child process to finish
                wait(NULL);
            
            }

        }

        // command 9: STOR file_name
        // to upload a file to the server
        else if (strncmp("STOR", input, 4)==0)
        {

            // send the PORT command automatically using PORT_helper to the server
            int client_data_socket = PORT_helper(client_socket, client_address);
            if (client_data_socket < 0)
            {
                continue;
            }

            // send the STOR command to the server
            int send_status = send(client_socket, input, sizeof(input), 0);
            if (send_status == -1)
            {
                perror("Error sending STOR command\n");
                continue;
            }

            // receive the response from the server
            char response[10000];
            memset(response, 0, sizeof(response));
            int recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);
            
            // check if the confirmation is "150 File status okay; about to open data connection"
            if (strncmp(response, "150", 3) != 0)
            {
                printf("STOR command failed\n");
                continue;
            }

            // fork a child process to upload the file to the server
            pid_t pid = fork();
            if (pid<0)
            {
                perror("Error forking child process\n");
                continue;
            }
            else if (pid == 0)// in the child process
            {
                // now we switch to the data port to upload the file

                // listen on the data port
                int listen_status = listen(client_data_socket, 5);
                if (listen_status < 0)
                {
                    perror("Error listening on data port\n");
                    continue;
                }

                // accept the connection from the server
                //int server_data_socket = accept(client_data_socket, (struct sockaddr*) &client_address, &client_address_len);
                int server_data_socket = accept(client_data_socket, NULL, NULL);
                if (server_data_socket < 0)
                {
                    perror("Error accepting connection from server\n");
                    continue;
                }

                // check if the file exists: the file should be local in the client directory
                char **parts = split_string(input, " ");
                char *file_name = parts[1];
                FILE *file = fopen(file_name, "rb");
                if (file == NULL)
                {
                    perror("Error opening file\n");
                    continue;
                }
                else
                {
                    // send the file to the server
                    char buffer[10000];
                    memset(buffer, 0, sizeof(buffer));
                    while(1)
                    {
                        int read_bytes = fread(buffer, 1, sizeof(buffer), file);
                        if(read_bytes <= 0)
                        {
                            break;
                        }
                        int sent_bytes = send(server_data_socket, buffer, read_bytes, 0);
                       
                        if(sent_bytes<0)
                        {
                            printf("Connection failed\n");
                            close(client_data_socket);
                            fclose(file);
                            break;
                        }
                        memset(buffer, 0, sizeof(buffer));
                    }
                }
                fclose(file);

                // close the data connection
                close(client_data_socket);
                close(server_data_socket);

                // receive the confirmation from the server
                // it should be "226 Transfer complete"
                memset(response, 0, sizeof(response));
                recv(client_socket, response, sizeof(response), 0);
                printf("%s\n", response);
                if (strncmp(response, "226", 3) != 0)
                {
                    printf("STOR command failed\n");
                    continue;
                }
                memset(response, 0, sizeof(response));

                exit(0);

            }
            else
            {
                // parent process
                // wait for the child process to finish
                wait(NULL);
            
            }
        }

        // Command 10 :QUIT
        else if (strncmp("QUIT", input, 4)==0)
        {
            // send the QUIT command to the server
            int send_status = send(client_socket, input, sizeof(input), 0);
            if (send_status == -1)
            {
                perror("Error sending QUIT command\n");
                continue;
            }

            // receive the response from the server
            char response[200];
            memset(response, 0, sizeof(response));
            int recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);
            memset(response, 0, sizeof(response));

            // check if the confirmation is "221 Service closing control connection"
            if (strstr(response, "221") != 0)
            {
                printf("QUIT command failed\n");
                continue;
            }
            memset(response, 0, sizeof(response));

            // close the socket
            close(client_socket);
            break;
        }

        // for invalid commands
        else
        {
            // send the command to the server anyway
            int send_status = send(client_socket, input, sizeof(input), 0);
            if (send_status == -1)
            {
                perror("Error sending command\n");
                continue;
            }
            memset(input, 0, sizeof(input));

            // receive the response from the server
            char response[200];
            memset(response, 0, sizeof(response));
            int recv_status = recv(client_socket, response, sizeof(response), 0);
            if (recv_status == -1)
            {
                perror("Error receiving response from server\n");
                return -1;
            }
            printf("%s\n", response);//should be "503 Bad sequence of commands"
            continue;
        }
        memset(input, 0, sizeof(input));
    
    }
    return 0;
}
