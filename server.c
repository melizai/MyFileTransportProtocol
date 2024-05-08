#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <stdlib.h>

/* portul folosit */
#define PORT 2882
#define MAX_BUFFER_SIZE 1024
#define CAESAR_SHIFT 23

#define MAX_USERS 100
#define USERNAME_SIZE 50
#define PASSWORD_SIZE 50

extern int errno; /* eroarea returnata de unele apeluri */

/* functie de convertire a adresei IP a clientului in sir de caractere */
char *conv_addr(struct sockaddr_in address)
{
    static char str[25];
    char port[7];

    /* adresa IP a clientului */
    strcpy(str, inet_ntoa(address.sin_addr));
    /* portul utilizat de client */
    bzero(port, 7);
    sprintf(port, ":%d", ntohs(address.sin_port));
    strcat(str, port);
    return (str);
}

struct User
{
    char username[USERNAME_SIZE];
    char password[PASSWORD_SIZE];
    int whitelisted;
};

struct User users[MAX_USERS];
int numUsers = 0;

void readUserCredentials()
{
    FILE *loginFile = fopen("login.txt", "r");
    if (loginFile == NULL)
    {
        perror("[server] Eroare la deschiderea fisierului login.txt pentru citire.\n");
        exit(1);
    }

    while (fscanf(loginFile, "%s %s %d", users[numUsers].username, users[numUsers].password, &users[numUsers].whitelisted) == 3)
    {
        numUsers++;
        if (numUsers >= MAX_USERS)
        {
            fprintf(stderr, "[server] Prea multi utilizatori. Cresteti dimensiunea MAX_USERS.\n");
            exit(1);
        }
    }

    fclose(loginFile);
}

void encryptPassword(char *text, int shift)
{
    size_t textLength = strlen(text);

    for (size_t i = 0; i < textLength; i++)
    {
        // criptez fiecare caracter folosind Caesar Cipher
        if (isalpha(text[i]))
        {
            char base = isupper(text[i]) ? 'A' : 'a';
            text[i] = (text[i] - base + shift) % 26 + base;
        }
    }
}

void decryptPassword(char *text, int shift)
{
    size_t textLength = strlen(text);

    for (size_t i = 0; i < textLength; i++)
    {
        // decriptez fiecare caracter folosind Caesar Cipher
        if (isalpha(text[i]))
        {
            char base = isupper(text[i]) ? 'A' : 'a';
            text[i] = (text[i] - base - shift + 26) % 26 + base;
        }
    }
}

int authenticateUser(const char *username, const char *password)
{
    // verific daca user si parola se potrivesc
    for (int i = 0; i < numUsers; i++)
    {
        if (strcmp(users[i].username, username) == 0)
        {
            // aloc dinamic memorie pt passwordCheck
            char *passwordCheck = malloc(strlen(users[i].password) + 1);

            // verific daca alocarea memoriei este successful
            if (passwordCheck == NULL)
            {
                perror("Error allocating memory");
                exit(0);
            }

            strcpy(passwordCheck, users[i].password);

            decryptPassword(passwordCheck, CAESAR_SHIFT);

            if (strcmp(passwordCheck, password) == 0)
            {

                free(passwordCheck); // Free allocated memory
                // verif daca user-ul e whitelisted
                if (!users[i].whitelisted)
                {
                    printf("[server] User %s is blacklisted.\n", username);
                    return 2;
                }
                return 1; // success autentificare
            }

            free(passwordCheck); // Free allocated memory daca autentificarea fails
        }
    }

    return 0;
}

/* functie pentru a gestiona receptia fisierelor de la client (upload)*/
int receiveFile(int fd)
{
    char buffer[MAX_BUFFER_SIZE];
    size_t bytesRead;
    size_t totalBytesReceived = 0;

    // Receive file path length and then the file path from the client
    size_t pathLength;
    if (read(fd, &pathLength, sizeof(pathLength)) <= 0)
    {
        perror("[server] Error receiving file path length from client.\n");
        return 0;
    }

    char file_path[256];
    if (read(fd, file_path, pathLength) <= 0)
    {
        perror("[server] Error receiving file path from client.\n");
        return 0;
    }

    printf("[server] Received file path: %s\n", file_path);

    // Construct the full path where the server should save the file
    char save_path[512];
    snprintf(save_path, sizeof(save_path), "/home/osboxes/Desktop/serverul/files/%s", basename(file_path));

    FILE *file = fopen(save_path, "wb");
    if (file == NULL)
    {
        perror("[server] Error opening file for writing.\n");
        return 0;
    }

    while (1)
    {
        // Receive buffer length and then the buffer from the client
        size_t bufferLength;
        if (read(fd, &bufferLength, sizeof(bufferLength)) <= 0)
        {
            perror("[server] Error receiving buffer length from client.\n");
            fclose(file);
            return 0;
        }

        if (bufferLength == 0)
        {
            // End of file transfer
            break;
        }

        bytesRead = read(fd, buffer, bufferLength);
        if (bytesRead <= 0)
        {
            perror("[server] Error receiving file content from client.\n");
            fclose(file);
            return 0;
        }

        totalBytesReceived += bytesRead; // Update total bytes received

        // Write the received buffer to the file
        size_t bytesWritten = fwrite(buffer, 1, bytesRead, file);
        if (bytesWritten != bytesRead)
        {
            perror("[server] Error writing to file.\n");
            fclose(file);
            return 0;
        }
    }

    fclose(file);

    printf("[server] Total bytes received: %zu\n", totalBytesReceived);
    return 1;
}

void sendFile(int fd, const char *file_path)
{
    char save_path[512];
    snprintf(save_path, sizeof(save_path), "/home/osboxes/Desktop/serverul/files/%s", basename(file_path));

    FILE *file = fopen(save_path, "rb");
    if (file == NULL)
    {
        perror("[server] Error opening file for reading.\n");
        return;
    }

    char buffer[MAX_BUFFER_SIZE];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        // Send the buffer length and then the buffer to the client
        write(fd, &bytesRead, sizeof(bytesRead));
        write(fd, buffer, bytesRead);
    }

    fclose(file);

    // Signal the end of the file transfer by sending a length of 0
    size_t zeroLength = 0;
    write(fd, &zeroLength, sizeof(zeroLength));

    printf("[server] File sent successfully: %s\n", file_path);
}

int main()
{
    readUserCredentials();
    struct sockaddr_in server; /* structurile pentru server si clienti */
    struct sockaddr_in from;
    fd_set readfds;    /* multimea descriptorilor de citire */
    fd_set actfds;     /* multimea descriptorilor activi */
    struct timeval tv; /* structura de timp pentru select() */
    int sd, client;    /* descriptori de socket */
    int optval = 1;    /* optiune folosita pentru setsockopt()*/
    int fd;            /* descriptor folosit pentru
                           parcurgerea listelor de descriptori */
    int nfds;          /* numarul maxim de descriptori */
    int len;           /* lungimea structurii sockaddr_in */
    int authenticated = 0;

    /* creare socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server] Eroare la socket().\n");
        return errno;
    }

    /* setam pentru socket optiunea SO_REUSEADDR */
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    /* pregatim structurile de date */
    bzero(&server, sizeof(server));

    /* umplem structura folosita de server */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    /* atasam socketul */
    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server] Eroare la bind().\n");
        return errno;
    }

    /* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen(sd, 5) == -1)
    {
        perror("[server] Eroare la listen().\n");
        return errno;
    }

    /* completam multimea de descriptori de citire */
    FD_ZERO(&actfds);    /* initial, multimea este vida */
    FD_SET(sd, &actfds); /* includem in multime socketul creat */

    tv.tv_sec = 1; /* se va astepta un timp de 1 sec. */
    tv.tv_usec = 0;

    /* valoarea maxima a descriptorilor folositi */
    nfds = sd;

    printf("[server] Asteptam la portul %d...\n", PORT);
    fflush(stdout);

    /* servim in mod concurent (!?) clientii... */
    while (1)
    {
        /* ajustam multimea descriptorilor activi (efectiv utilizati) */
        bcopy((char *)&actfds, (char *)&readfds, sizeof(readfds));

        /* apelul select() */
        if (select(nfds + 1, &readfds, NULL, NULL, &tv) < 0)
        {
            perror("[server] Eroare la select().\n");
            return errno;
        }
        /* vedem daca e pregatit socketul pentru a-i accepta pe clienti */
        if (FD_ISSET(sd, &readfds))
        {
            /* pregatirea structurii client */
            len = sizeof(from);
            bzero(&from, sizeof(from));

            /* a venit un client, acceptam conexiunea */
            client = accept(sd, (struct sockaddr *)&from, &len);

            /* eroare la acceptarea conexiunii de la un client */
            if (client < 0)
            {
                perror("[server] Eroare la accept().\n");
                continue;
            }

            if (nfds < client) /* ajusteaza valoarea maximului */
                nfds = client;

            /* includem in lista de descriptori activi si acest socket */
            FD_SET(client, &actfds);

            printf("[server] S-a conectat clientul cu descriptorul %d, de la adresa %s.\n", client, conv_addr(from));
            fflush(stdout);
        }

        /* vedem daca e pregatit vreun socket client pentru a trimite raspunsul */
        for (fd = 0; fd <= nfds; fd++) /* parcurgem multimea de descriptori */
        {
            /* este un socket de citire pregatit? */
            if (fd != sd && FD_ISSET(fd, &readfds))
            {
                char commandBuffer[256];
                size_t bytesRead = read(fd, commandBuffer, sizeof(commandBuffer));

                if (bytesRead <= 0)
                {
                    perror("[server] Eroare la primirea comenzii din client.\n");
                    close(fd);
                    FD_CLR(fd, &actfds);
                    break;
                }

                char blacklistMessage[] = "Esti blacklisted, nu poti folosi comanda.\n";

                /* Procesam comenzile primite */
                if (strncmp(commandBuffer, "login", 5) == 0)
                {
                    char username[MAX_BUFFER_SIZE], password[MAX_BUFFER_SIZE];

                    // Extract the username and password from the command
                    sscanf(commandBuffer, "login %s %s", username, password);
                    // Attempt to authenticate the user
                    authenticated = authenticateUser(username, password);

                    // Send authentication result to the client
                    write(fd, &authenticated, sizeof(authenticated));
                }
                else if (strncmp(commandBuffer, "upload", 6) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                    }
                    else if (authenticated == 1)
                    {
                        if (!receiveFile(fd))
                        {
                            printf("[server] Eroare la primirea fisierului din client cu descriptor %d.\n", fd);
                        }
                        else
                        {
                            printf("[server] Fisier primit cu succes de la client cu descriptor %d.\n", fd);
                        }
                        fflush(stdout);
                    }
                }
                else if (strncmp(commandBuffer, "list", 4) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Execute the "find" command to list all files recursively
                        FILE *findOutput = popen("find /home/osboxes/Desktop/serverul/files -type f", "r");
                        if (findOutput == NULL)
                        {
                            perror("[server] Eroare executie comanda 'find'.\n");

                            // Send an error message to the client
                            char errorMessage[] = "Error executing 'list' command.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                            break; // Break out of the loop after sending the error message
                        }

                        // Read the output of the "find" command, remove the prefix, and send it to the client
                        char buffer[MAX_BUFFER_SIZE];
                        size_t bytesRead;
                        while ((bytesRead = fread(buffer, 1, sizeof(buffer), findOutput)) > 0)
                        {
                            // Remove the prefix using sed and remove empty lines
                            char sedCommand[512];
                            snprintf(sedCommand, sizeof(sedCommand), "echo '%s' | sed -e 's|^/home/osboxes/Desktop/serverul/files/||' -e '/^$/d'", buffer);

                            FILE *sedOutput = popen(sedCommand, "r");
                            if (sedOutput == NULL)
                            {
                                perror("[server] Error executing 'sed' command.\n");
                                fclose(findOutput);
                                break;
                            }

                            // Read the output of the sed command and send it to the client
                            size_t sedBytesRead;
                            while ((sedBytesRead = fread(buffer, 1, sizeof(buffer), sedOutput)) > 0)
                            {
                                write(fd, buffer, sedBytesRead);
                            }

                            // Close the sed output stream
                            pclose(sedOutput);
                        }

                        // Close the output stream of the "find" command
                        pclose(findOutput);

                        // Signal the end of the server response by sending an empty buffer
                        char endOfResponse = '\0';
                        write(fd, &endOfResponse, 1);

                        printf("[server] List command completed.\n");
                        fflush(stdout);
                    }
                }
                else if (strncmp(commandBuffer, "exit", 4) == 0)
                {
                    /* Close the connection */
                    printf("[server] Closing connection with client descriptor %d.\n", fd);
                    fflush(stdout);
                    close(fd);
                    FD_CLR(fd, &actfds);
                }
                else if (strncmp(commandBuffer, "help", 4) == 0)
                {
                    /* Print available commands */
                    printf("Comanda 'help' a fost trimisa de client \n");
                    fflush(stdout);
                }
                else if (strncmp(commandBuffer, "download", 8) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the file path from the command
                        char file_path[256];
                        sscanf(commandBuffer, "download %s", file_path);

                        // Call the function to send the file to the client
                        sendFile(fd, file_path);
                    }
                }
                else if (strncmp(commandBuffer, "delete", 6) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the file path from the command
                        char file_path[256];
                        sscanf(commandBuffer, "delete %s", file_path);

                        // Construct the full path for the file
                        char full_file_path[512];
                        snprintf(full_file_path, sizeof(full_file_path), "/home/osboxes/Desktop/serverul/files/%s", file_path);

                        // Check if the file exists
                        if (access(full_file_path, F_OK) == 0)
                        {
                            // Delete the file
                            if (remove(full_file_path) == 0)
                            {
                                printf("[server] File deleted successfully: %s\n", file_path);
                                // Send a success message to the client
                                char successMessage[] = "File deleted successfully.\n";
                                write(fd, successMessage, sizeof(successMessage));
                            }
                            else
                            {
                                perror("[server] Error deleting file.\n");
                                // Send an error message to the client
                                char errorMessage[] = "Error deleting file.\n";
                                write(fd, errorMessage, sizeof(errorMessage));
                            }
                        }
                        else
                        {
                            // Send an error message to the client if the file does not exist
                            char errorMessage[] = "Error: File does not exist.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                        fflush(stdout);
                    }
                }
                else if (strncmp(commandBuffer, "rename", 6) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the old and new file names from the command
                        char old_file_name[256], new_file_name[256];
                        sscanf(commandBuffer, "rename %s %s", old_file_name, new_file_name);

                        // Construct the full paths for the old and new files
                        char old_file_path[512], new_file_path[512];
                        snprintf(old_file_path, sizeof(old_file_path), "/home/osboxes/Desktop/serverul/files/%s", old_file_name);
                        snprintf(new_file_path, sizeof(new_file_path), "/home/osboxes/Desktop/serverul/files/%s", new_file_name);

                        // Check if the old file exists
                        if (access(old_file_path, F_OK) == 0)
                        {
                            // Rename the file
                            if (rename(old_file_path, new_file_path) == 0)
                            {
                                printf("[server] File renamed successfully: %s to %s\n", old_file_name, new_file_name);
                                fflush(stdout);
                                // Send a success message to the client
                                char successMessage[] = "File renamed successfully.\n";
                                write(fd, successMessage, sizeof(successMessage));
                            }
                            else
                            {
                                perror("[server] Error renaming file.\n");
                                // Send an error message to the client
                                char errorMessage[] = "Error renaming file.\n";
                                write(fd, errorMessage, sizeof(errorMessage));
                            }
                        }
                        else
                        {
                            // Send an error message to the client if the old file does not exist
                            char errorMessage[] = "Error: Old file does not exist.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                    }
                }

                else if (strncmp(commandBuffer, "mkdir", 5) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the directory name from the command
                        char directory_name[256];
                        sscanf(commandBuffer, "mkdir %s", directory_name);

                        // Construct the full path for the new directory
                        char new_directory_path[512];
                        snprintf(new_directory_path, sizeof(new_directory_path), "/home/osboxes/Desktop/serverul/files/%s", directory_name);

                        // Create the new directory
                        if (mkdir(new_directory_path, 0777) == 0)
                        {
                            printf("[server] Directory created successfully: %s\n", new_directory_path);
                            fflush(stdout);
                            // Send a success message to the client
                            char successMessage[] = "Directory created successfully.\n";
                            write(fd, successMessage, sizeof(successMessage));
                        }
                        else
                        {
                            perror("[server] Error creating directory.\n");
                            // Send an error message to the client
                            char errorMessage[] = "Error creating directory.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                    }
                }
                else if (strncmp(commandBuffer, "remove", 6) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the directory name from the command
                        char directory_name[256];
                        sscanf(commandBuffer, "remove %s", directory_name);

                        // Construct the full path for the directory to be removed
                        char directory_path[512];
                        snprintf(directory_path, sizeof(directory_path), "/home/osboxes/Desktop/serverul/files/%s", directory_name);

                        // Remove the directory
                        if (rmdir(directory_path) == 0)
                        {
                            printf("[server] Directory removed successfully: %s\n", directory_path);
                            fflush(stdout);
                            // Send a success message to the client
                            char successMessage[] = "Directory removed successfully.\n";
                            write(fd, successMessage, sizeof(successMessage));
                        }
                        else
                        {
                            perror("[server] Error removing directory.\n");
                            // Send an error message to the client
                            char errorMessage[] = "Error removing directory.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                    }
                }
                else if (strncmp(commandBuffer, "d_rename", 8) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the old and new directory names from the command
                        char old_directory_name[256], new_directory_name[256];
                        sscanf(commandBuffer, "d_rename %s %s", old_directory_name, new_directory_name);

                        // Construct the full paths for the old and new directories
                        char old_directory_path[512], new_directory_path[512];
                        snprintf(old_directory_path, sizeof(old_directory_path), "/home/osboxes/Desktop/serverul/files/%s", old_directory_name);
                        snprintf(new_directory_path, sizeof(new_directory_path), "/home/osboxes/Desktop/serverul/files/%s", new_directory_name);

                        // Check if the old directory exists
                        if (access(old_directory_path, F_OK) == 0)
                        {
                            // Rename the directory
                            if (rename(old_directory_path, new_directory_path) == 0)
                            {
                                printf("[server] Directory renamed successfully: %s to %s\n", old_directory_name, new_directory_name);
                                fflush(stdout);
                                // Send a success message to the client
                                char successMessage[] = "Directory renamed successfully.\n";
                                write(fd, successMessage, sizeof(successMessage));
                            }
                            else
                            {
                                perror("[server] Error renaming directory.\n");
                                // Send an error message to the client
                                char errorMessage[] = "Error renaming directory.\n";
                                write(fd, errorMessage, sizeof(errorMessage));
                            }
                        }
                        else
                        {
                            // Send an error message to the client if the old directory does not exist
                            char errorMessage[] = "Error: Old directory does not exist.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                    }
                }
                else if (strncmp(commandBuffer, "move", 4) == 0)
                {
                    if (authenticated == 2)
                    {
                        write(fd, blacklistMessage, sizeof(blacklistMessage));
                        fflush(stdout);
                    }
                    else if (authenticated == 1)
                    {
                        // Extract the file name and destination directory from the command
                        char file_name[256], destination_directory[256];
                        sscanf(commandBuffer, "move %s %s", file_name, destination_directory);

                        // Construct the full paths for the file and destination directory
                        char file_path[512], destination_path[512];
                        snprintf(file_path, sizeof(file_path), "/home/osboxes/Desktop/serverul/files/%s", file_name);
                        snprintf(destination_path, sizeof(destination_path), "/home/osboxes/Desktop/serverul/files/%s", destination_directory);

                        // Check if the file exists
                        if (access(file_path, F_OK) == 0)
                        {
                            // Check if the destination directory exists
                            if (access(destination_path, F_OK) == 0)
                            {
                                // Construct the full destination path for the file in the destination directory
                                char destination_file_path[512];
                                snprintf(destination_file_path, sizeof(destination_file_path), "%s/%s", destination_path, file_name);

                                // Move the file to the destination directory
                                if (rename(file_path, destination_file_path) == 0)
                                {
                                    printf("[server] File moved successfully: %s to %s\n", file_name, destination_directory);
                                    fflush(stdout);
                                    // Send a success message to the client
                                    char successMessage[] = "File moved successfully.\n";
                                    write(fd, successMessage, sizeof(successMessage));
                                }
                                else
                                {
                                    perror("[server] Error moving file.\n");
                                    // Send an error message to the client
                                    char errorMessage[] = "Error moving file.\n";
                                    write(fd, errorMessage, sizeof(errorMessage));
                                }
                            }
                            else
                            {
                                // Send an error message to the client if the destination directory does not exist
                                char errorMessage[] = "Error: Destination directory does not exist.\n";
                                write(fd, errorMessage, sizeof(errorMessage));
                            }
                        }
                        else
                        {
                            // Send an error message to the client if the file does not exist
                            char errorMessage[] = "Error: File does not exist.\n";
                            write(fd, errorMessage, sizeof(errorMessage));
                        }
                    }
                }
                else
                {
                    /* Unknown command */
                    char errorMessage[] = "Unknown command. Use 'help' for a list of commands.\n";
                    write(fd, errorMessage, sizeof(errorMessage));
                }
            }
        } /* for */
    }     /* while */
    return 0;
}  /* main */