#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 6060
#define BUFFER_SIZE 1024

// Function prototypes
void upload_file(int sock, const char *filename, const char *destination_path);
void download_file(int sock, const char *filename);
void download_tarball(int sock, const char *tarfile);

int main()
{
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE], filename[BUFFER_SIZE], destination_path[BUFFER_SIZE];

    // Creating socket
    // SOCK_STREAM indicates that this will be a TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configuring server address struct
    server_addr.sin_family = AF_INET;                       // IPv4
    server_addr.sin_port = htons(PORT);                     // Port number
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr); // Localhost

    // Connecting to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Main loop to process commands
    while (1)
    {
        // Taking user input for command
        printf("Enter command (ufile/dfile/rmfile/dtar/display/exit): ");
        fgets(buffer, BUFFER_SIZE, stdin);
        sscanf(buffer, "%s %s %s", command, filename, destination_path);

        // Exit the loop and close connection if 'exit' command is received
        if (strcmp(command, "exit") == 0)
        {
            break;
        }

        // Sending command to the server
        write(sock, buffer, strlen(buffer));

        // Handle different commands
        if (strcmp(command, "ufile") == 0)
        {
            upload_file(sock, filename, destination_path);
        }
        else if (strcmp(command, "dfile") == 0)
        {
            download_file(sock, filename);
        }
        else if (strcmp(command, "dtar") == 0)
        {
            download_tarball(sock, filename); // filename here will be the filetype
        }
        // Additional command handling like rmfile, display could be added here
    }

    // Close the socket
    close(sock);
    return 0;
}

// Function to upload a file to the server
void upload_file(int sock, const char *filename, const char *destination_path)
{
    // Open the file in binary read mode
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        perror("File open error");
        return;
    }

    // Prepare a buffer for file data and command
    char buffer[BUFFER_SIZE];

    // Send an initial command to the server indicating a file transfer with its destination
    snprintf(buffer, BUFFER_SIZE, "ufile %s", destination_path);
    if (write(sock, buffer, strlen(buffer)) < 0)
    {
        perror("Failed to send initial file transfer command");
        fclose(fp);
        return;
    }

    // Read file data and send it to the server
    int bytes_read;
    while ((bytes_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
    {
        if (write(sock, buffer, bytes_read) != bytes_read)
        {
            perror("Failed to send file data");
            break;
        }
    }

    if (ferror(fp))
    {
        perror("Error reading from file");
    }

    fclose(fp);
    printf("File %s uploaded successfully.\n", filename);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

void download_file(int sock, const char *filename);
void download_tarball(int sock, const char *filetype);

void download_file(int sock, const char *filename)
{
    char buffer[BUFFER_SIZE];

    // Extract just the filename from the full path
    // Find the last occurrence of '/' in the filename
    const char *base_filename = strrchr(filename, '/');
    if (base_filename)
    {
        base_filename++; // Skip the '/' to get the actual filename
    }
    else
    {
        base_filename = filename; // No '/' found, so use the whole string
    }

    // Open the file for writing in the current directory (PWD)
    FILE *fp = fopen(base_filename, "wb");
    if (fp == NULL)
    {
        perror("File open error");
        return;
    }

    int bytes_read;
    // Read data from the socket and write it to the file
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        fwrite(buffer, sizeof(char), bytes_read, fp);
        // If less data than the buffer size is read, assume it's the end of the file
        if (bytes_read < BUFFER_SIZE)
            break;
    }

    fclose(fp);
    printf("File %s downloaded successfully.\n", base_filename);
}

void download_tarball(int sock, const char *filetype)
{
    char buffer[BUFFER_SIZE];
    char tarfile[BUFFER_SIZE];

    // Determine the tarfile name based on the filetype
    // Create tarfile name by appending ".tar" to the appropriate type
    snprintf(tarfile, BUFFER_SIZE, "%s.tar", filetype[0] == 'p' ? "pdf" : (filetype[0] == 't' ? "text" : "cfiles"));

    // Open the tar file for writing in the current directory (PWD)
    FILE *fp = fopen(tarfile, "wb");
    if (fp == NULL)
    {
        perror("Tar file open error");
        return;
    }

    int bytes_read;
    // Read data from the socket and write it to the tar file
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        fwrite(buffer, sizeof(char), bytes_read, fp);
        // If less data than the buffer size is read, assume it's the end of the file
        if (bytes_read < BUFFER_SIZE)
            break;
    }

    fclose(fp);
    printf("Tarball %s downloaded successfully.\n", tarfile);
}
