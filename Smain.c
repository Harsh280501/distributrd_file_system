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

#define PORT 6060             // Port number for the Smain server
#define BUFFER_SIZE 1024      // Buffer size for data transmission
#define PDF_SERVER_PORT 6061  // Port number for the PDF server
#define TEXT_SERVER_PORT 6062 // Port number for the Text server

// Function prototypes
void prcclient(int client_sock);
void ensure_directory_exists(char *path);
void expand_tilde(char *path);
void replace_smain_with_spdf(char *path);
void replace_smain_with_stext(char *path);
void upload_file_to_path(const char *filename, const char *destination_path, int client_sock);
void download_file(const char *filename, int client_sock);
void delete_file(const char *filename, int client_sock);
void fetch_file_from_server(const char *filename, const char *server_ip, int server_port, int client_sock);
void send_delete_request_to_server(const char *filename, const char *server_ip, int server_port);
void handle_dtar(const char *filetype, int client_sock);
void request_tarball_from_server(const char *command, const char *server_ip, int server_port, int client_sock);
void handle_display_command(const char *pathname, int client_sock);

int main()
{
    int server_sock, client_sock;                     // Server socket and client socket descriptors
    struct sockaddr_in server_addr, client_addr;      // Structures for server and client addresses
    socklen_t addr_size = sizeof(struct sockaddr_in); // Size of address structure
    pid_t child_pid;                                  // Process ID for the child process

    // Creating socket for the server
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setting up the server address structure
    server_addr.sin_family = AF_INET;         // IPv4 address family
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections from any IP address
    server_addr.sin_port = htons(PORT);       // Port number, converted to network byte order

    // Binding the socket to the specified port
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_sock, 10) < 0)
    {
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("Smain server listening on port %d\n", PORT);

    while (1)
    {
        // Accepting a client connection
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size)) < 0)
        {
            perror("Client accept failed");
            continue;
        }

        // Creating a child process to handle the client request
        if ((child_pid = fork()) == 0)
        {
            close(server_sock);     // Child process doesn't need the server socket
            prcclient(client_sock); // Handling client request in the child process
            exit(0);                // Exiting child process after handling client
        }
        else
        {
            close(client_sock);         // Parent process doesn't need the client socket
            waitpid(-1, NULL, WNOHANG); // Reap any finished child processes
        }
    }

    close(server_sock); // Closing server socket when exiting
    return 0;
}

void prcclient(int client_sock)
{
    char buffer[BUFFER_SIZE];           // Buffer for receiving data from the client
    char command[BUFFER_SIZE];          // Buffer for storing the command received from the client
    char filename[BUFFER_SIZE];         // Buffer for storing the filename received from the client
    char destination_path[BUFFER_SIZE]; // Buffer for storing the destination path received from the client

    while (1)
    {
        // Clear buffers before reading new data
        bzero(buffer, BUFFER_SIZE);
        bzero(command, BUFFER_SIZE);
        bzero(filename, BUFFER_SIZE);
        bzero(destination_path, BUFFER_SIZE);

        // Read the client command from the socket
        read(client_sock, buffer, BUFFER_SIZE);

        // Parse the command, filename, and destination path from the received buffer
        sscanf(buffer, "%s %s %s", command, filename, destination_path);

        // Handle file upload
        if (strcmp(command, "ufile") == 0)
        {
            printf("Uploading file: %s to %s\n", filename, destination_path);
            // Call function to handle uploading file to the specified path
            upload_file_to_path(filename, destination_path, client_sock);
        }
        // Handle file download
        else if (strcmp(command, "dfile") == 0)
        {
            printf("Requested file for download: %s\n", filename);
            // Call function to handle downloading the file
            download_file(filename, client_sock);
        }
        // Handle file removal
        else if (strcmp(command, "rmfile") == 0)
        {
            printf("Requested file for removal: %s\n", filename);
            // Call function to handle removing the file
            delete_file(filename, client_sock);
        }
        // Handle tar creation and download
        else if (strcmp(command, "dtar") == 0)
        {
            printf("Handling tar creation and download for filetype: %s\n", filename);
            // Call function to handle tarball creation and downloading
            handle_dtar(filename, client_sock);
        }
        // Handle display command
        else if (strcmp(command, "display") == 0)
        {
            printf("Displaying files in path: %s\n", filename);
            // Call function to handle displaying files in the specified path
            handle_display_command(filename, client_sock);
        }
    }
}

// Function to handle the "display" command
void handle_display_command(const char *pathname, int client_sock)
{
    char buffer[BUFFER_SIZE]; // Buffer for sending data to the client
    DIR *dir;                 // Pointer to directory stream
    struct dirent *entry;     // Pointer to directory entry structure

    // Open the directory specified by pathname
    dir = opendir(pathname);
    if (dir == NULL)
    {
        // If directory cannot be opened, send an error message to the client
        perror("Could not open directory");
        snprintf(buffer, BUFFER_SIZE, "Error: Could not open directory %s\n", pathname);
        write(client_sock, buffer, strlen(buffer));
        return;
    }

    // Notify the client about the start of the list of .c files
    snprintf(buffer, BUFFER_SIZE, "C Files in %s:\n", pathname);
    write(client_sock, buffer, strlen(buffer));

    // Read and list all .c files in the directory
    while ((entry = readdir(dir)) != NULL)
    {
        // Check if the file has a .c extension
        if (strstr(entry->d_name, ".c"))
        {
            // Send the full path of the .c file to the client
            snprintf(buffer, BUFFER_SIZE, "%s/%s\n", pathname, entry->d_name);
            write(client_sock, buffer, strlen(buffer));
        }
    }

    // Close the directory stream
    closedir(dir);

    // Fetch and list .pdf files from the Spdf server
    request_tarball_from_server("display .pdf", "127.0.0.1", PDF_SERVER_PORT, client_sock);

    // Fetch and list .txt files from the Stext server
    request_tarball_from_server("display .txt", "127.0.0.1", TEXT_SERVER_PORT, client_sock);
}

// Function to handle tarball creation and sending based on filetype
void handle_dtar(const char *filetype, int client_sock)
{
    // Check the filetype and handle accordingly
    if (strcmp(filetype, ".c") == 0)
    {
        // Handle tarball creation for .c files in Smain
        char tarfile[BUFFER_SIZE];                                       // Buffer for the tarball filename
        snprintf(tarfile, BUFFER_SIZE, "%s/cfiles.tar", getenv("HOME")); // Define tarball file path

        // Create a tarball of all .c files in the Smain directory
        char command[BUFFER_SIZE]; // Buffer for system command
        snprintf(command, BUFFER_SIZE, "find %s/smain -maxdepth 1 -name '*.c' -print | tar -cvf %s -T -", getenv("HOME"), tarfile);
        printf("Executing: %s\n", command);
        system(command); // Execute the command to create the tarball

        // Send the tarball to the client
        FILE *fp = fopen(tarfile, "rb"); // Open the tarball file for reading in binary mode
        if (fp == NULL)
        {
            perror("File open error"); // Handle file open error
            return;
        }

        // Read and send the tarball content to the client
        int bytes_read;
        char buffer[BUFFER_SIZE]; // Buffer for reading file content
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
        {
            write(client_sock, buffer, bytes_read); // Send file content to the client
        }

        fclose(fp); // Close the file after sending
        printf("Tarball %s sent to client.\n", tarfile);
    }
    else if (strcmp(filetype, ".pdf") == 0)
    {
        // Forward the request to Spdf server to create and send the tarball
        request_tarball_from_server("dtar .pdf", "127.0.0.1", PDF_SERVER_PORT, client_sock);
    }
    else if (strcmp(filetype, ".txt") == 0)
    {
        // Forward the request to Stext server to create and send the tarball
        request_tarball_from_server("dtar .txt", "127.0.0.1", TEXT_SERVER_PORT, client_sock);
    }
    else
    {
        // Handle unknown filetype
        printf("Unknown filetype: %s\n", filetype);
    }
}

// Function to request a tarball from a server
void request_tarball_from_server(const char *command, const char *server_ip, int server_port, int client_sock)
{
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Print connection details for debugging
    printf("Connecting to server at %s:%d to request: %s\n", server_ip, server_port, command);

    // Create a socket for communication with the server
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Send the command to the server
    write(sock, command, strlen(command));

    // Receive the tarball from the server and send it to the client
    int bytes_read;
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        write(client_sock, buffer, bytes_read);
    }

    // Close the socket after communication is complete
    close(sock);
    printf("Tarball received and sent to client.\n");
}

// Function to upload a file to a specified path, potentially redirecting to other servers
void upload_file_to_path(const char *filename, const char *destination_path, int client_sock)
{
    char buffer[BUFFER_SIZE];    // Buffer for file data
    char full_path[BUFFER_SIZE]; // Full path where the file will be saved
    char file_type[10];          // File extension

    // Extract the file extension from the filename
    sscanf(filename, "%*[^.].%s", file_type);

    // Expand any tilde (~) in the destination path and build the full path
    strcpy(full_path, destination_path);
    expand_tilde(full_path);

    // Handle different file types
    if (strcmp(file_type, "c") == 0)
    {
        // Ensure the destination directory exists
        ensure_directory_exists(full_path);
        strcat(full_path, "/");      // Append a slash to the path
        strcat(full_path, filename); // Append the filename to the path

        printf("Saving .c file to: %s\n", full_path);

        // Open the file for writing in binary mode
        FILE *fp = fopen(full_path, "wb");
        if (fp == NULL)
        {
            perror("File open error");
            return;
        }

        // Read the file data from the client and write it to the file
        int bytes_read;
        while ((bytes_read = read(client_sock, buffer, BUFFER_SIZE)) > 0)
        {
            fwrite(buffer, sizeof(char), bytes_read, fp);
            if (bytes_read < BUFFER_SIZE)
                break; // End of file
        }

        fclose(fp); // Close the file after writing
        printf("File upload complete: %s\n", full_path);
    }
    else if (strcmp(file_type, "pdf") == 0)
    {
        // Redirect the path to the Spdf server for .pdf files
        replace_smain_with_spdf(full_path);
        ensure_directory_exists(full_path);
        strcat(full_path, "/");      // Append a slash to the path
        strcat(full_path, filename); // Append the filename to the path

        printf("Redirecting and saving .pdf file to: %s\n", full_path);

        // Upload the .pdf file to the Spdf server
        int sock;
        struct sockaddr_in server_addr;

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("Socket creation failed");
            return;
        }

        // Set up the server address for Spdf
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PDF_SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        // Connect to the Spdf server
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Connection to Spdf server failed");
            close(sock);
            return;
        }

        // Send the filename to the Spdf server
        snprintf(buffer, BUFFER_SIZE, "ufile %s", full_path);
        write(sock, buffer, strlen(buffer));

        // Send the file content to the Spdf server
        FILE *fp = fopen(filename, "rb");
        if (fp == NULL)
        {
            perror("File open error");
            close(sock);
            return;
        }

        int bytes_read;
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
        {
            write(sock, buffer, bytes_read);
        }

        fclose(fp);  // Close the file after sending
        close(sock); // Close the socket connection
        printf("File upload to Spdf complete: %s\n", full_path);
    }
    else if (strcmp(file_type, "txt") == 0)
    {
        // Redirect the path to the Stext server for .txt files
        replace_smain_with_stext(full_path);
        ensure_directory_exists(full_path);
        strcat(full_path, "/");      // Append a slash to the path
        strcat(full_path, filename); // Append the filename to the path

        printf("Redirecting and saving .txt file to: %s\n", full_path);

        // Upload the .txt file to the Stext server
        int sock;
        struct sockaddr_in server_addr;

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("Socket creation failed");
            return;
        }

        // Set up the server address for Stext
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(TEXT_SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        // Connect to the Stext server
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Connection to Stext server failed");
            close(sock);
            return;
        }

        // Send the filename to the Stext server
        snprintf(buffer, BUFFER_SIZE, "ufile %s", full_path);
        write(sock, buffer, strlen(buffer));

        // Send the file content to the Stext server
        FILE *fp = fopen(filename, "rb");
        if (fp == NULL)
        {
            perror("File open error");
            close(sock);
            return;
        }

        int bytes_read;
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
        {
            write(sock, buffer, bytes_read);
        }

        fclose(fp);  // Close the file after sending
        close(sock); // Close the socket connection
        printf("File upload to Stext complete: %s\n", full_path);
    }
}

// Function to ensure that a directory and its parent directories exist
void ensure_directory_exists(char *path)
{
    struct stat st = {0};   // Structure to hold directory status
    char temp[BUFFER_SIZE]; // Temporary buffer to manipulate the path
    char *p = NULL;         // Pointer for traversing the path

    // Copy the path to a temporary buffer
    snprintf(temp, sizeof(temp), "%s", path);

    // Iterate over each character in the path
    for (p = temp + 1; *p; p++)
    {
        // If a '/' character is found, create the directory up to that point
        if (*p == '/')
        {
            *p = '\0'; // Temporarily terminate the string at the '/'
            if (stat(temp, &st) == -1)
            {
                // Directory does not exist, attempt to create it
                if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
                {
                    perror("Failed to create directory");
                }
            }
            *p = '/'; // Restore the '/' character
        }
    }

    // Ensure the final directory in the path is created
    if (stat(temp, &st) == -1)
    {
        if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
        {
            perror("Failed to create directory");
        }
    }
}

// Function to download a file based on its type and send it to the client
void download_file(const char *filename, int client_sock)
{
    char file_type[10];                       // Buffer to store the file extension
    sscanf(filename, "%*[^.].%s", file_type); // Extract the file extension

    // Expand any tilde (~) in the filename and get the full path
    char full_path[BUFFER_SIZE];
    strcpy(full_path, filename);
    expand_tilde(full_path);

    // Print the full path after tilde expansion
    printf("Full path after expansion: %s\n", full_path);

    // Handle the file based on its type
    if (strcmp(file_type, "c") == 0)
    {
        // Handle .c files locally
        printf("Handling .c file locally: %s\n", full_path);
        FILE *fp = fopen(full_path, "rb"); // Open the file for reading in binary mode
        if (fp == NULL)
        {
            perror("File open error");
            return;
        }

        char buffer[BUFFER_SIZE]; // Buffer for file data
        int bytes_read;
        // Read the file in chunks and send to the client
        while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
        {
            write(client_sock, buffer, bytes_read);
        }

        fclose(fp); // Close the file after sending
    }
    else if (strcmp(file_type, "pdf") == 0)
    {
        // Handle .pdf files by fetching from the Spdf server
        replace_smain_with_spdf(full_path);
        printf("Fetching .pdf file from Spdf: %s\n", full_path);
        fetch_file_from_server(full_path, "127.0.0.1", PDF_SERVER_PORT, client_sock);
    }
    else if (strcmp(file_type, "txt") == 0)
    {
        // Handle .txt files by fetching from the Stext server
        replace_smain_with_stext(full_path);
        printf("Fetching .txt file from Stext: %s\n", full_path);
        fetch_file_from_server(full_path, "127.0.0.1", TEXT_SERVER_PORT, client_sock);
    }
}

// Function to delete a file based on its type and send the request to the appropriate server if needed
void delete_file(const char *filename, int client_sock)
{
    char file_type[10];                       // Buffer to store the file extension
    sscanf(filename, "%*[^.].%s", file_type); // Extract the file extension

    // Expand any tilde (~) in the filename and get the full path
    char full_path[BUFFER_SIZE];
    strcpy(full_path, filename);
    expand_tilde(full_path);

    // Print the full path for deletion
    printf("Full path for deletion: %s\n", full_path);

    // Handle the file based on its type
    if (strcmp(file_type, "c") == 0)
    {
        // Handle .c files locally
        printf("Deleting .c file locally: %s\n", full_path);
        if (remove(full_path) == 0) // Remove the file
        {
            printf("File deleted successfully.\n");
        }
        else
        {
            perror("File deletion error"); // Print error if file deletion fails
        }
    }
    else if (strcmp(file_type, "pdf") == 0)
    {
        // Handle .pdf files by requesting deletion from Spdf server
        replace_smain_with_spdf(full_path);
        send_delete_request_to_server(full_path, "127.0.0.1", PDF_SERVER_PORT);
    }
    else if (strcmp(file_type, "txt") == 0)
    {
        // Handle .txt files by requesting deletion from Stext server
        replace_smain_with_stext(full_path);
        send_delete_request_to_server(full_path, "127.0.0.1", TEXT_SERVER_PORT);
    }
}

// Function to send a delete request to a server
void send_delete_request_to_server(const char *filename, const char *server_ip, int server_port)
{
    int sock;
    struct sockaddr_in server_addr;

    // Print the details of the delete request
    printf("Sending delete request to server at %s:%d for file: %s\n", server_ip, server_port, filename);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Send delete command to the server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "rmfile %s", filename);
    write(sock, command, strlen(command));

    close(sock);
}

// Function to fetch a file from a server and send it to the client
void fetch_file_from_server(const char *filename, const char *server_ip, int server_port, int client_sock)
{
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE]; // Buffer for file data

    // Print the details of the fetch request
    printf("Connecting to server at %s:%d to fetch file: %s\n", server_ip, server_port, filename);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Send the filename to the server
    write(sock, filename, strlen(filename));

    // Open the file for writing (to save data received from the server)
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL)
    {
        perror("File open error");
        close(sock);
        return;
    }

    // Receive the file from the server and write to local file
    int bytes_read;
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        fwrite(buffer, sizeof(char), bytes_read, fp);
        if (bytes_read < BUFFER_SIZE)
            break;
    }

    fclose(fp);  // Close the local file
    close(sock); // Close the connection to the server

    // Open the file for reading (to send data to the client)
    fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        perror("File open error");
        return;
    }

    // Send the file data to the client
    while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
    {
        write(client_sock, buffer, bytes_read);
    }

    fclose(fp); // Close the file after sending
}

// Function to expand a tilde (~) in the path to the user's home directory
void expand_tilde(char *path)
{
    // Check if the path starts with a tilde
    if (path[0] == '~')
    {
        // Retrieve the user's home directory from the environment variables
        char *home = getenv("HOME");
        if (home)
        {
            // Temporary buffer to build the new path
            char temp[BUFFER_SIZE];
            // Construct the new path by combining the home directory and the rest of the path
            snprintf(temp, sizeof(temp), "%s%s", home, path + 1);
            // Copy the new path back to the original path variable
            strcpy(path, temp);
        }
    }
}

// Function to replace "/smain/" with "/spdf/" in the path and update it to point to the Spdf server
void replace_smain_with_spdf(char *path)
{
    // Find the position of "/smain/" in the path
    char *pos = strstr(path, "/smain/");
    if (pos)
    {
        // Move the position pointer to the end of "/smain/"
        pos += strlen("/smain/");
        // Temporary buffer to build the new path
        char new_path[BUFFER_SIZE];
        // Construct the new path by replacing "/smain/" with "/spdf/" and prepending the home directory
        snprintf(new_path, BUFFER_SIZE, "%s/spdf/%s", getenv("HOME"), pos);
        // Copy the new path back to the original path variable
        strcpy(path, new_path);
    }
}

// Function to replace "/smain/" with "/stext/" in the path and update it to point to the Stext server
void replace_smain_with_stext(char *path)
{
    // Find the position of "/smain/" in the path
    char *pos = strstr(path, "/smain/");
    if (pos)
    {
        // Move the position pointer to the end of "/smain/"
        pos += strlen("/smain/");
        // Temporary buffer to build the new path
        char new_path[BUFFER_SIZE];
        // Construct the new path by replacing "/smain/" with "/stext/" and prepending the home directory
        snprintf(new_path, BUFFER_SIZE, "%s/stext/%s", getenv("HOME"), pos);
        // Copy the new path back to the original path variable
        strcpy(path, new_path);
    }
}
