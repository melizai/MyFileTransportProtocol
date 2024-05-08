#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_PATH_LENGTH 256

extern int errno;

int port;

int uploadFile(int sd, const char *file_path);
void processCommand(int sd, const char *command);

void cleanupAndExit(int sd, const char *errorMsg)
{
    perror(errorMsg);
    close(sd);
    exit(errno);
}

void downloadFile(int sd, const char *file_path)
{
    FILE *file = fopen(file_path, "wb");
    if (file == NULL)
    {
        perror("[client] Error opening file for writing.\n");
        return;
    }

    char buffer[MAX_BUFFER_SIZE];
    size_t bytesRead;

    while (1)
    {
        size_t bufferLength;
        if (read(sd, &bufferLength, sizeof(bufferLength)) <= 0)
        {
            perror("[client] Error receiving buffer length from server.\n");
            fclose(file);
            return;
        }

        if (bufferLength == 0)
        {
            break;
        }

        bytesRead = read(sd, buffer, bufferLength);
        if (bytesRead <= 0)
        {
            perror("[client] Error receiving file content from server.\n");
            fclose(file);
            return;
        }

        size_t bytesWritten = fwrite(buffer, 1, bytesRead, file);
        if (bytesWritten != bytesRead)
        {
            perror("[client] Error writing to file.\n");
            fclose(file);
            return;
        }
    }

    fclose(file);
    printf("[client] File downloaded successfully: %s\n", file_path);
}

int main(int argc, char *argv[])
{
    int sd;
    struct sockaddr_in server;

    if (argc != 3)
    {
        printf("[client] Syntax: %s <server_address> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        cleanupAndExit(sd, "[client] Error creating socket.\n");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        cleanupAndExit(sd, "[client] Error connecting to server.\n");
    }

    int authLimit = 0;
    int auth = 0;

    while (authLimit < 5)
    {
        printf("[client] Enter your username: ");
        fflush(stdout);
        char username[MAX_BUFFER_SIZE];
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = '\0'; // Remove newline character

        printf("[client] Enter your password: ");
        fflush(stdout);
        char password[MAX_BUFFER_SIZE];
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = '\0'; // Remove newline character

        // Send login command with username and password to the server
        char loginCommand[MAX_BUFFER_SIZE];
        snprintf(loginCommand, sizeof(loginCommand), "login %s %s", username, password);
        write(sd, loginCommand, strlen(loginCommand) + 1);

        // Receive authentication result from the server
        int authenticated;
        read(sd, &authenticated, sizeof(authenticated));

        if (authenticated)
        {
            printf("[client] Authentication successful.\n");
            auth = 1;
            break; // Exit authentication loop
        }
        else
        {
            authLimit++;
            if (authLimit != 5)
            {
                printf("[client] Authentication failed %d/5. Please try again.\n", authLimit);
            }
            else
            {
                printf("[client] Authentication failed too many times. You'll be disconnected now. .\n");
            }
        }
    }

    if (auth)
    {
        while (1)
        {
            printf("[client] Enter a command : ");
            fflush(stdout);
            char commandBuffer[MAX_BUFFER_SIZE];
            fgets(commandBuffer, sizeof(commandBuffer), stdin);

            size_t len = strlen(commandBuffer);
            if (len > 0 && commandBuffer[len - 1] == '\n')
            {
                commandBuffer[len - 1] = '\0';
            }

            if (write(sd, commandBuffer, strlen(commandBuffer) + 1) < 0)
            {
                cleanupAndExit(sd, "[client] Error sending command to server.\n");
            }

            if (strcmp(commandBuffer, "exit") == 0)
            {
                break;
            }

            processCommand(sd, commandBuffer);
        }
    }

    close(sd);

    return 0;
}

int uploadFile(int sd, const char *file_path)
{
    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        perror("[client] Error opening file for reading.\n");
        return errno;
    }

    char buffer[MAX_BUFFER_SIZE];
    size_t bytesRead;
    size_t totalBytesSent = 0;

    size_t pathLength = strlen(file_path) + 1;
    write(sd, &pathLength, sizeof(pathLength));
    write(sd, file_path, pathLength);

    printf("[client] Sent file path: %s\n", file_path);

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        write(sd, &bytesRead, sizeof(bytesRead));
        write(sd, buffer, bytesRead);

        totalBytesSent += bytesRead;
    }

    fclose(file);

    size_t zeroLength = 0;
    write(sd, &zeroLength, sizeof(zeroLength));

    printf("[client] Total bytes sent: %zu\n", totalBytesSent);

    return 0;
}

void processCommand(int sd, const char *command)
{
    char responseBuffer[MAX_BUFFER_SIZE];
    size_t bytesRead;

    if (strncmp(command, "upload", 6) == 0)
    {
        char file_path[MAX_PATH_LENGTH];
        sscanf(command, "upload %s", file_path);

        if (uploadFile(sd, file_path) == 0)
        {
            printf("[client] File uploaded successfully.\n");
        }
        else
        {
            printf("[client] Error uploading file.\n");
        }
    }
    else if (strncmp(command, "list", 4) == 0)
    {
        printf("[client] The following files are available:\n");

        size_t totalBytesReceived = 0;

        // Receive and print the list of files from the server
        while (1)
        {
            bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
            if (bytesRead <= 0)
            {
                perror("[client] Error receiving server response for 'list' command.\n");
                break;
            }

            printf("%s", responseBuffer);

            totalBytesReceived += bytesRead;

            if (totalBytesReceived >= MAX_BUFFER_SIZE)
            {
                // Reset totalBytesReceived for the next iteration
                totalBytesReceived = 0;
            }

            if (bytesRead < sizeof(responseBuffer))
            {
                // End of server response
                break;
            }
        }

        printf("[client] List command completed.\n");
    }
    else if (strncmp(command, "download", 8) == 0)
    {
        // Extract the file path from the command
        char file_path[256];
        sscanf(command, "download %s", file_path);

        // Call the function to download the file from the server
        downloadFile(sd, file_path);
    }
    else if (strncmp(command, "exit", 4) == 0)
    {
        // Exit command
        printf("[client] Exiting.\n");
        close(sd);
        exit(0);
    }
    else if (strncmp(command, "help", 4) == 0)
    {
        // Help command
        printf("Comenzi valabile:\n");
        printf("[client] - upload <path_name> : upload a file to the server.\n");
        printf("[client] - download <file_name> : download a file from the server.\n");
        printf("[client] - delete <file_name> : deletes a file from the server.\n");
        printf("[client] - rename <old_file_name> <new_file_name> - renames a file from the server.\n");
        printf("[client] - mkdir <directory_name> : creates a new director in the server.\n");
        printf("[client] - remove <directory_name> : removes a director from the server.\n");
        printf("[client] - move <file_name> <directory_name> : move a file from the server into a director from the server.\n");
        printf("[client] - d_rename <old_directory_name> <new_directory_name> : renames a director from the server.\n");
        printf("[client] - list : list all the files on the server.\n");
        printf("[client] - exit : end the connection with the server.\n");
        printf("[client] - help : print all the available commands.\n");
    }
    else if (strncmp(command, "delete", 6) == 0)
    {
        // Extract the file path from the command
        char file_path[256];
        sscanf(command, "delete %s", file_path);

        // Process the server response for the "delete" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "rename", 6) == 0)
    {
        // Extract the old and new file names from the command
        char old_file_name[256], new_file_name[256];
        sscanf(command, "rename %s %s", old_file_name, new_file_name);

        // Print or handle the names as needed (for testing purposes)
        printf("[client] Old File Name: %s\n", old_file_name);
        printf("[client] New File Name: %s\n", new_file_name);

        // Process the server response for the "rename" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "mkdir", 5) == 0)
    {
        // Process the server response for the "mkdir" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "remove", 6) == 0)
    {
        // Process the server response for the "remove" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "move", 4) == 0)
    {
        // Process the server response for the "move" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "d_rename", 8) == 0)
    {
        // Extract the old and new directory names from the command
        char old_directory_name[256], new_directory_name[256];
        sscanf(command, "d_rename %s %s", old_directory_name, new_directory_name);

        // Print or handle the names as needed (for testing purposes)
        printf("[client] Old Directory Name: %s\n", old_directory_name);
        printf("[client] New Directory Name: %s\n", new_directory_name);

        // Process the server response for the "r_rename" command
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
    else if (strncmp(command, "move", 4) == 0)
    {
        // Extract the file name and destination directory from the command
        char file_name[256], destination_directory[256];
        sscanf(command, "move %s %s", file_name, destination_directory);

        // Print or handle the names as needed (for testing purposes)
        printf("[client] File Name: %s\n", file_name);
        printf("[client] Destination Directory: %s\n", destination_directory);
    }
    else
    {
        bytesRead = read(sd, responseBuffer, sizeof(responseBuffer));
        if (bytesRead > 0)
        {
            printf("Server response: %s\n", responseBuffer);
        }
    }
}