#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 6062        // Define the port number for the server
#define BUFFER_SIZE 1024 // Define the buffer size for data transfer

void handle_client(int client_sock);                            // Function prototype to handle client requests
void ensure_directory_exists(char *path);                       // Function prototype to ensure directory existence
void create_tarball(const char *filetype, const char *tarfile); // Function prototype to create a tarball

int main()
{
    int server_sock, client_sock;                     // File descriptors for the server and client sockets
    struct sockaddr_in server_addr, client_addr;      // Structures to hold server and client addresses
    socklen_t addr_size = sizeof(struct sockaddr_in); // Size of the address structure
    pid_t child_pid;                                  // Process ID for the child process

    // Creating socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed"); // Print error message if socket creation fails
        exit(EXIT_FAILURE);               // Exit the program with a failure status
    }

    // Binding socket to the port
    server_addr.sin_family = AF_INET;         // Set the address family to IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available IP address on the host
    server_addr.sin_port = htons(PORT);       // Convert the port number to network byte order

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed"); // Print error message if binding fails
        close(server_sock);    // Close the socket
        exit(EXIT_FAILURE);    // Exit the program with a failure status
    }

    // Listening for incoming connections
    if (listen(server_sock, 10) < 0)
    {
        perror("Listen failed"); // Print error message if listening fails
        close(server_sock);      // Close the socket
        exit(EXIT_FAILURE);      // Exit the program with a failure status
    }

    printf("Server listening on port %d\n", PORT); // Print message indicating the server is ready

    while (1)
    {
        // Accepting client connection
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size)) < 0)
        {
            perror("Client accept failed"); // Print error message if client acceptance fails
            continue;                       // Continue to the next iteration to accept another connection
        }

        // Creating a child process to handle the client
        if ((child_pid = fork()) == 0)
        {
            // In the child process
            close(server_sock);         // Close the server socket in the child process to avoid interference
            handle_client(client_sock); // Handle the client's request
            exit(0);                    // Exit the child process after handling the client
        }
        else
        {
            // In the parent process
            close(client_sock);         // Close the client socket in the parent process
            waitpid(-1, NULL, WNOHANG); // Reap any terminated child processes
        }
    }

    close(server_sock); // Close the server socket when the server is shutting down
    return 0;           // Return 0 to indicate successful execution
}

void handle_client(int client_sock)
{
    char buffer[BUFFER_SIZE];                         // Buffer to hold data during communication
    char command[BUFFER_SIZE], filepath[BUFFER_SIZE]; // Buffers to hold command and file path

    // Reading client command
    bzero(buffer, BUFFER_SIZE);                 // Clear the buffer
    read(client_sock, buffer, BUFFER_SIZE);     // Read data from client into buffer
    sscanf(buffer, "%s %s", command, filepath); // Parse command and file path from buffer

    printf("Received command: %s, for file path: %s\n", command, filepath); // Print received command and file path

    if (strcmp(command, "rmfile") == 0)
    {
        // Handle file removal
        if (remove(filepath) == 0) // Attempt to delete the file
        {
            printf("File %s deleted successfully.\n", filepath); // Print success message
        }
        else
        {
            perror("File deletion error"); // Print error message if deletion fails
        }
    }
    else if (strcmp(command, "ufile") == 0)
    {
        // Handle file upload
        printf("Received file upload request for: %s\n", filepath); // Print message about file upload request

        // Ensure the directory exists
        ensure_directory_exists(filepath); // Create directories if needed

        // Receiving file from Smain
        FILE *fp = fopen(filepath, "wb"); // Open the file for writing in binary mode
        if (fp == NULL)
        {
            perror("File open error"); // Print error message if file opening fails
            close(client_sock);        // Close the client socket
            return;                    // Exit function
        }

        int bytes_read;                                                   // Variable to store number of bytes read
        while ((bytes_read = read(client_sock, buffer, BUFFER_SIZE)) > 0) // Read data from client
        {
            fwrite(buffer, sizeof(char), bytes_read, fp); // Write data to file
            if (bytes_read < BUFFER_SIZE)                 // If less data was read than the buffer size
                break;                                    // Exit loop
        }

        printf("File received successfully: %s\n", filepath); // Print success message
        fclose(fp);                                           // Close the file
    }
    else if (strcmp(command, "dtar") == 0)
    {
        snprintf(filepath, BUFFER_SIZE, "%s/text.tar", getenv("HOME")); // Create tarball path
        create_tarball(".txt", filepath);                               // Create tarball with .txt files

        // Send the tarball to Smain
        FILE *fp = fopen(filepath, "rb"); // Open the tarball file for reading in binary mode
        if (fp == NULL)
        {
            perror("File open error"); // Print error message if file opening fails
            close(client_sock);        // Close the client socket
            return;                    // Exit function
        }

        int bytes_read;                                                         // Variable to store number of bytes read
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0) // Read data from file
        {
            write(client_sock, buffer, bytes_read); // Send data to client
        }

        fclose(fp);                                      // Close the file
        printf("Tarball %s sent to Smain.\n", filepath); // Print success message
    }
    else
    {
        printf("Unknown command: %s\n", command); // Print unknown command message
    }

    close(client_sock); // Close the client socket
}

void ensure_directory_exists(char *path)
{
    struct stat st = {0};                      // Structure to hold file status
    char temp[BUFFER_SIZE];                    // Temporary buffer for path manipulation
    char *dir_path = strdup(path);             // Duplicate the path to modify
    char *last_slash = strrchr(dir_path, '/'); // Find the last '/' in the path

    if (last_slash)
    {
        *last_slash = '\0'; // Strip the filename to get the directory path
    }

    snprintf(temp, sizeof(temp), "%s", dir_path); // Copy directory path to temporary buffer

    // Iterate over the path and create directories as needed
    for (char *p = temp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';                 // Temporarily terminate the string at the current '/'
            if (stat(temp, &st) == -1) // Check if the directory exists
            {
                if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST) // Attempt to create the directory
                {
                    perror("Failed to create directory"); // Print error message if directory creation fails
                }
            }
            *p = '/'; // Restore the original character
        }
    }

    // Ensure the final directory in the path is created
    if (stat(temp, &st) == -1)
    {
        if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
        {
            perror("Failed to create directory"); // Print error message if directory creation fails
        }
    }

    free(dir_path); // Free the duplicated path memory
}

void create_tarball(const char *filetype, const char *tarfile)
{
    char command[BUFFER_SIZE];   // Buffer to hold the command for tarball creation
    char *home = getenv("HOME"); // Get the user's home directory

    if (strcmp(filetype, ".txt") == 0)
    {
        // Create tarball of only .txt files in ~/stext directory
        snprintf(command, BUFFER_SIZE, "find %s/stext -type f -name '*.txt' -print | tar -cvf %s -T -", home, tarfile);
    }
    else
    {
        printf("Unknown filetype for tarball creation: %s\n", filetype); // Print unknown filetype message
        return;                                                          // Exit function
    }

    printf("Executing tar command: %s\n", command); // Print the tar command being executed
    int result = system(command);                   // Execute the tar command

    if (result != 0)
    {
        printf("Tar command failed with exit code %d\n", result); // Print error message if tar command fails
    }
}
