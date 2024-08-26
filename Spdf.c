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

#define PORT 6061
#define BUFFER_SIZE 1024

void handle_client(int client_sock);
void ensure_directory_exists(char *path);
void create_tarball(const char *filetype, const char *tarfile);

int main()
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    pid_t child_pid;

    // Creating a socket for communication
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed"); // Print error message if socket creation fails
        exit(EXIT_FAILURE);               // Exit the program with failure status
    }

    // Configuring server address structure
    server_addr.sin_family = AF_INET;         // Set address family to IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available network interface
    server_addr.sin_port = htons(PORT);       // Set port number for communication

    // Binding the socket to the address and port
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed"); // Print error message if binding fails
        close(server_sock);    // Close the socket
        exit(EXIT_FAILURE);    // Exit the program with failure status
    }

    // Listening for incoming client connections
    if (listen(server_sock, 10) < 0)
    {
        perror("Listen failed"); // Print error message if listening fails
        close(server_sock);      // Close the socket
        exit(EXIT_FAILURE);      // Exit the program with failure status
    }

    printf("Server listening on port %d\n", PORT); // Inform that server is ready to accept connections

    while (1)
    {
        // Accepting a new client connection
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size)) < 0)
        {
            perror("Client accept failed"); // Print error message if accepting client fails
            continue;                       // Continue to accept new connections
        }

        // Forking a child process to handle the client
        if ((child_pid = fork()) == 0)
        {
            close(server_sock);         // Close the listening socket in the child process
            handle_client(client_sock); // Handle client communication
            exit(0);                    // Exit child process after handling client
        }
        else
        {
            close(client_sock); // Close the client socket in the parent process
            // Reap any terminated child processes to avoid zombie processes
            waitpid(-1, NULL, WNOHANG);
        }
    }

    close(server_sock); // Close the server socket (though this will never be reached)
    return 0;           // Exit the program with success status
}

void handle_client(int client_sock)
{
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE], filepath[BUFFER_SIZE];

    // Read the command from the client
    bzero(buffer, BUFFER_SIZE);                 // Clear the buffer to ensure it is empty
    read(client_sock, buffer, BUFFER_SIZE);     // Read the client data into the buffer
    sscanf(buffer, "%s %s", command, filepath); // Parse command and file path from buffer

    printf("Received command: %s, for file path: %s\n", command, filepath);

    // Process the command received from the client
    if (strcmp(command, "rmfile") == 0)
    {
        // Handle file removal
        if (remove(filepath) == 0) // Try to remove the specified file
        {
            printf("File %s deleted successfully.\n", filepath);
        }
        else
        {
            perror("File deletion error"); // Print error message if file deletion fails
        }
    }
    else if (strcmp(command, "ufile") == 0)
    {
        // Handle file upload
        printf("Received file upload request for: %s\n", filepath);

        // Ensure the directory where the file will be saved exists
        ensure_directory_exists(filepath);

        // Receive the file from the client and save it
        FILE *fp = fopen(filepath, "wb"); // Open the file for writing in binary mode
        if (fp == NULL)
        {
            perror("File open error"); // Print error message if file open fails
            close(client_sock);        // Close the client socket
            return;
        }

        int bytes_read;
        while ((bytes_read = read(client_sock, buffer, BUFFER_SIZE)) > 0) // Read data from client
        {
            fwrite(buffer, sizeof(char), bytes_read, fp); // Write data to the file
            if (bytes_read < BUFFER_SIZE)                 // Check if the end of the file is reached
                break;
        }

        printf("File received successfully: %s\n", filepath);
        fclose(fp); // Close the file after writing
    }
    else if (strcmp(command, "dtar") == 0)
    {
        // Create and send a tarball of PDF files
        snprintf(filepath, BUFFER_SIZE, "%s/pdf.tar", getenv("HOME")); // Define the path for the tarball
        create_tarball(".pdf", filepath);                              // Create the tarball

        // Send the tarball to the client
        FILE *fp = fopen(filepath, "rb"); // Open the tarball for reading in binary mode
        if (fp == NULL)
        {
            perror("File open error"); // Print error message if file open fails
            close(client_sock);        // Close the client socket
            return;
        }

        int bytes_read;
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0) // Read data from the tarball
        {
            write(client_sock, buffer, bytes_read); // Send data to the client
        }

        fclose(fp); // Close the tarball file after sending
        printf("Tarball %s sent to client.\n", filepath);
    }
    else
    {
        // Handle unknown commands
        printf("Unknown command: %s\n", command);
    }

    close(client_sock); // Close the client socket after processing the request
}

void ensure_directory_exists(char *path)
{
    struct stat st = {0}; // Structure to hold file status information
    char temp[BUFFER_SIZE];
    char *dir_path = strdup(path);             // Duplicate path for manipulation
    char *last_slash = strrchr(dir_path, '/'); // Find the last '/' in the path

    if (last_slash)
    {
        *last_slash = '\0'; // Remove the filename to get the directory path
    }

    snprintf(temp, sizeof(temp), "%s", dir_path); // Copy the directory path

    // Create directories as needed along the path
    for (char *p = temp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';                 // Temporarily terminate the string at the current '/'
            if (stat(temp, &st) == -1) // Check if the directory exists
            {
                if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST) // Create directory if it does not exist
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
    char command[BUFFER_SIZE];
    char *home = getenv("HOME"); // Get the user's home directory

    if (strcmp(filetype, ".pdf") == 0)
    {
        // Create a tarball of PDF files in the ~/spdf directory
        snprintf(command, BUFFER_SIZE, "find %s/spdf -type f -name '*.pdf' -print | tar -cvf %s -T -", home, tarfile);
    }
    else
    {
        // Handle unknown file types for tarball creation
        printf("Unknown filetype for tarball creation: %s\n", filetype);
        return;
    }

    printf("Executing tar command: %s\n", command); // Print the command to be executed
    int result = system(command);                   // Execute the tar command

    if (result != 0)
    {
        printf("Tar command failed with exit code %d\n", result); // Print error message if tar command fails
    }
}
