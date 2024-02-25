#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "TftpError.h"
#include "TftpOpcode.h"
#include "TftpCommon.cpp"

#define SERV_UDP_PORT 61125
#define SERV_HOST_ADDR "127.0.0.1"

/* A pointer to the name of this program for error reporting.      */
char *program;

/* The main program sets up the local socket for communication     */
/* and the server's address and port (well-known numbers) before   */
/* calling the processFileTransfer main loop.                      */
int main(int argc, char *argv[])
{
    program = argv[0];

    int sockfd;
    struct sockaddr_in cli_addr, serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(&cli_addr, 0, sizeof(cli_addr));
    /*-------------------------------------------------------------
     *          INITIALIZING SERVER ADDRESS
     *///-------------------------------------------------------------
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_UDP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(SERV_HOST_ADDR);
    /*-------------------------------------------------------------
     *          INITIALIZING CLIENT ADDRESS
     *///-------------------------------------------------------------
    // initializing client address
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    cli_addr.sin_port = htons(0);
    /*-------------------------------------------------------------
     *          CREATING A SOCKET
     *///-------------------------------------------------------------
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket error");
        exit(1);
    }
    /*-------------------------------------------------------------
     *          BINDING THE SOCKET
     *///-------------------------------------------------------------
    if (bind(sockfd, (struct sockaddr *)&cli_addr, sizeof(cli_addr)) < 0)
    {
        perror("bind error");
        close(sockfd);
        exit(2);
    }
    
    printf("Bind socket successful\n");
    /*-------------------------------------------------------------
     *          VERIFYING ARGUMENTS
     *///-------------------------------------------------------------
    if (argc != 3)
    {
        return TFTP_ERROR_INVALID_ARGUMENT_COUNT;
    }
    /*-------------------------------------------------------------
     *          PARSING
     *///-------------------------------------------------------------
    char *request_type = argv[1];
    char *filename = argv[2];
    /*-------------------------------------------------------------
     *          VERIFYING THE REQUEST TYPE
     *///-------------------------------------------------------------
    if (strcmp(request_type, "r") != 0 && strcmp(request_type, "w") != 0)
    {
        fprintf(stderr, "Invalid request type\n");
        exit(EXIT_FAILURE);
    }
    /*-------------------------------------------------------------
     *          CREATING CLIENT FILE PATH
     *///-------------------------------------------------------------
    char clientFilepath[256] = "client-files/";
    strcat(clientFilepath, filename);
    /*-------------------------------------------------------------
     *          CREATING SERVER FILE PATH
     *///-------------------------------------------------------------
    char serverFilepath[256] = "server-files/";
    strcat(serverFilepath, filename);
    /*-------------------------------------------------------------
     *          OPENING FILE FOR READ OR WRITE BASED ON REQUEST TYPE
     *///-------------------------------------------------------------
    FILE *file;
    if (strcmp(request_type, "r") == 0)
    {
        /*-------------------------------------------------------------
         *          TFTP_RRQ
         *///-------------------------------------------------------------
        file = fopen(clientFilepath, "wb"); // wb - open a file for writing in binary mode
        if (!file)
        {
            perror("Failed to open file for writing\n");
            exit(EXIT_FAILURE);
        }
        /*-------------------------------------------------------------
         *          CREATING THE 1ST TFTP REQUEST PACKET
         *///-------------------------------------------------------------
        char requestPacket[MAX_PACKET_LEN];
        size_t packetSize = 0;
        if (strcmp(request_type, "r") == 0)
        {
            createRequestPacket(TFTP_RRQ, filename, requestPacket, &packetSize);
        }
        else if (strcmp(request_type, "w") == 0)
        {
            createRequestPacket(TFTP_WRQ, filename, requestPacket, &packetSize);
        }
        
        printf("The request packet is: \n");
        printBuffer(requestPacket, packetSize);
        /*-------------------------------------------------------------
         *          SENDING REQUEST PACKET TO SERVER
         *///-------------------------------------------------------------
        if (sendto(sockfd, &requestPacket, packetSize, 0, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        {
            perror("sendto failed");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        /*-------------------------------------------------------------
         *          FILE TRANSFER / PROCESSING TFTP REQUEST
         *///-------------------------------------------------------------
        char dataPacket[MAX_PACKET_LEN];
        struct sockaddr_in server_addr;
        socklen_t server_addr_len = sizeof(server_addr);
        
        printf("Processing tftp request...\n");
        
        while (true)
        {
            /*-------------------------------------------------------------
             *          RECEIVING THE FILE FROM THE SERVER
             *///-------------------------------------------------------------
            ssize_t bytes_received = recvfrom(sockfd, dataPacket, MAX_PACKET_LEN, 0, (struct sockaddr *)&server_addr, &server_addr_len);

            if (bytes_received < 0)
            {
                perror("recvfrom error");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            /*-------------------------------------------------------------
             *          PARSING DATA PACKET
             *///-------------------------------------------------------------
            /*-------------------------------------------------------------
             *          GETTING THE OPCODE FROM DATA PACKET
             *///-------------------------------------------------------------
            unsigned short opcode = ntohs(*(unsigned short *)dataPacket);
            if (opcode != TFTP_DATA)
            {
                fprintf(stderr, "Unexpected opcode received\n");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            /*-------------------------------------------------------------
             *          GETTING THE BLOCK NUMBER FROM DATA PACKET
             *///-------------------------------------------------------------
            unsigned short blockNum = ntohs(*(unsigned short *)(dataPacket + 2));
            printf("Received block #%u\n", blockNum);
            /*-------------------------------------------------------------
             *          WRITING RECEIVED DATA TO THE FILE
             *///-------------------------------------------------------------
            const char *receivedData = dataPacket + 4;
            size_t dataLen = bytes_received - 4;
            
            if (fwrite(receivedData, 1, dataLen, file) != dataLen)
            {
                perror("fwrite error");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            /*-------------------------------------------------------------
             *          CREATING ACK PACKET
             *///-------------------------------------------------------------
            char ackPacket[MAX_PACKET_LEN];
            createAckPacket(blockNum, ackPacket);
            /*-------------------------------------------------------------
             *          SENDING THE ACK PACKET TO THE SERVER
             *///-------------------------------------------------------------
            if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&server_addr, server_addr_len) == -1)
            {
                perror("sendto failed");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            /*-------------------------------------------------------------
             *          CHECKING IF WE REACHED THE END OF FILE
             *///-------------------------------------------------------------
            if (bytes_received < MAX_PACKET_LEN)
            {
                break;
            }
        }
        fclose(file);
    }
    else if (strcmp(request_type, "w") == 0)
    {
        /*-------------------------------------------------------------
         *          TFTP_WRQ
         *///-------------------------------------------------------------
        file = fopen(clientFilepath, "rb"); // rb - open the file for reading in binary mode
        if (!file)
        {
            perror("Failed to open file for writing\n");
            exit(EXIT_FAILURE);
        }
        /*-------------------------------------------------------------
         *          CREATING THE 1ST TFTP REQUEST PACKET
         *///-------------------------------------------------------------
        char requestPacket[MAX_PACKET_LEN];
        size_t packetSize = 0;
        createRequestPacket(TFTP_WRQ, filename, requestPacket, &packetSize);
        
        printf("The request packet is: \n");
        printBuffer(requestPacket, packetSize);
        /*-------------------------------------------------------------
         *          SENDING REQUEST PACKET TO SERVER
         *///-------------------------------------------------------------
        if (sendto(sockfd, &requestPacket, packetSize, 0, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        {
            perror("sendto failed");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr;
        socklen_t server_addr_len = sizeof(server_addr);

        /*-------------------------------------------------------------
         *          RECEIVING INITIAL ACK FROM THE SERVER
         *///-------------------------------------------------------------
        printf("Processing tftp request...\n");
        
        char ackPacket[4];
        if (recvfrom(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&serv_addr, &server_addr_len) < 0)
        {
            perror("recvfrom failed");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // parsing the initial ACK packet
        unsigned short ackOpcode = ntohs(*(unsigned short *)ackPacket);
        unsigned short ackBlockNum = ntohs(*(unsigned short *)(ackPacket + 2));

        printf("Received ACK #%hu\n", ackBlockNum);

        if (ackOpcode != TFTP_ACK || ackBlockNum != 0)
        {
            fprintf(stderr, "Did not receive expected initial ACK from server\n");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        /*-------------------------------------------------------------
         *          INITIALIZING THE BLOCK NUMBER
         *///-------------------------------------------------------------
        unsigned short blockNumber = ackBlockNum;
        /*-------------------------------------------------------------
         *          FILE TRANSFER / PROCESSING TFTP REQUEST
         *///-------------------------------------------------------------
        while (true)
        {
            /*-------------------------------------------------------------
             *          READING DATA FROM THE FILE
             *///-------------------------------------------------------------
            char dataPacket[MAX_PACKET_LEN - 4]; // Subtract 4 for opcode and block number
            size_t bytesRead = fread(dataPacket, 1, MAX_PACKET_LEN - 4, file);

            char lastDataPacket[MAX_PACKET_LEN];
            size_t lastPacketSize;
            /*-------------------------------------------------------------
             *          CREATING A DATA PACKET
             *///-------------------------------------------------------------
            blockNumber++;
            char fullDataPacket[MAX_PACKET_LEN];
            size_t fullPacketSize;
            createDataPacket(blockNumber, dataPacket, bytesRead, fullDataPacket, &fullPacketSize);
            /*-------------------------------------------------------------
             *          SENDING DATA PACKET TO THE SERVER
             *///-------------------------------------------------------------
            ssize_t bytesSent = sendto(sockfd, &fullDataPacket, fullPacketSize, 0, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
            /*-------------------------------------------------------------
             *          RECEIVING ACK FROM THE SERVER
             *///-------------------------------------------------------------
            char ackPacket[4];
            recvfrom(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&serv_addr, &server_addr_len);

            // parsing the ACK packet
            unsigned short ackOpcode = ntohs(*(unsigned short *)ackPacket);
            unsigned short ackBlockNum = ntohs(*(unsigned short *)(ackPacket + 2));

            printf("Received ACK #%hu\n", ackBlockNum);

            if (bytesSent < MAX_PACKET_LEN)
            {
                break;
            }
        }
        fclose(file);
    }
    close(sockfd);
    exit(0);
}
