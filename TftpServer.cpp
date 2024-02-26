#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "TftpCommon.cpp"
#include "TftpError.h"

#define SERV_UDP_PORT 61125
#define SERV_HOST_ADDR "127.0.0.1"

char *program;

int handleIncomingRequest(int sockfd)
{
    struct sockaddr cli_addr;

    socklen_t cli_addr_len = sizeof(cli_addr);
    char requestPacket[MAX_PACKET_LEN];

    const char *serverDir = "server-files/";
    const char *clientDir = "client-files/";
    ssize_t bytesRead = 0;

    for (;;)
    {
        /*-------------------------------------------------------------
         *          RECEIVE THE 1ST REQUEST PACKET FROM THE CLIENT
         *///-------------------------------------------------------------
        printf("Waiting to receive request\n");
        ssize_t bytes_received = recvfrom(sockfd, requestPacket, MAX_PACKET_LEN, 0, (struct sockaddr *)&cli_addr, &cli_addr_len);
        if (bytes_received < 0)
        {
            perror("recvfrom");
            return -1;
        }
        /*-------------------------------------------------------------
         *          PARSING REQUEST PACKET
         *///-------------------------------------------------------------
        /*-------------------------------------------------------------
         *          GETTING THE OPCODE FROM REQUEST PACKET
         *///-------------------------------------------------------------
        unsigned short opcode = ntohs(*(unsigned short *)requestPacket);
        if (opcode != TFTP_RRQ && opcode != TFTP_WRQ)
        {
            fprintf(stderr, "Invalid request received\n");
            return -1;
        }
        /*-------------------------------------------------------------
         *          EXTRACTING FILENAME FROM REQUEST PACKET
         *///-------------------------------------------------------------
        char *filename = requestPacket + 2;
        printf("Requested filename is: %s\n", filename);
        /*-------------------------------------------------------------
         *          CREATING FULL PATH
         *///-------------------------------------------------------------
        char serverFullPath[1024];
        snprintf(serverFullPath, sizeof(serverFullPath), "%s%s", serverDir, filename);

        char clientFullPath[1024];
        snprintf(clientFullPath, sizeof(clientFullPath), "%s%s", clientDir, filename);

        /*-------------------------------------------------------------
         *          OPENING FILE FOR READ OR WRITE BASED ON REQUEST TYPE
         *///-------------------------------------------------------------
        FILE *file;
        if (opcode == TFTP_RRQ)
        {

            /*-------------------------------------------------------------
             *          TFTP_RRQ
             *///-------------------------------------------------------------
            file = fopen(serverFullPath, "rb");
            if (file == NULL)
            {
                printf("File not found\n");
            }
            /*-------------------------------------------------------------
             *          CREATING 1ST RESPONSE (DATA) PACKET
             *///-------------------------------------------------------------
            uint16_t blockNumber = 1;
            char dataPacket[MAX_PACKET_LEN];
            size_t packetSize;

            while (true)
            {
                size_t bytesRead = fread(dataPacket + 4, 1, 512, file); 
                if (bytesRead == 0 && ferror(file))
                {
                    perror("Error reading file");
                    fclose(file);
                    return -1;
                }
                createDataPacket(blockNumber, dataPacket + 4, bytesRead, dataPacket, &packetSize);
                /*-------------------------------------------------------------
                 *          SENDING 1ST RESPONSE (DATA) PACKET
                 *///-------------------------------------------------------------
                if (sendto(sockfd, &dataPacket, packetSize, 0, (const struct sockaddr *)&cli_addr, cli_addr_len) == -1)
                {
                    perror("sendto failed");
                    fclose(file);
                    close(sockfd);
                    exit(EXIT_FAILURE);
                }
                /*-------------------------------------------------------------
                 *          RECEIVING ACK PACKET
                 *///-------------------------------------------------------------
                char ackPacket[4]; 
                if (recvfrom(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&cli_addr, &cli_addr_len) < 0)
                {
                    perror("recvfrom error");
                    fclose(file);
                    return -1;
                }
                unsigned short ackOpcode = ntohs(*(unsigned short *)ackPacket);
                unsigned short ackBlockNum = ntohs(*(unsigned short *)(ackPacket + 2));

                printf("Received ACK #%hu\n", ackBlockNum);
                
                if (bytesRead < 512)
                {
                    break; 
                }
                blockNumber++; 
            }
            fclose(file);
        }
        else
        {
            /*-------------------------------------------------------------
             *          TFTP_WRQ
             *///-------------------------------------------------------------
            /*-------------------------------------------------------------
             *          OPENING THE FILE FOR WRITE
             *///-------------------------------------------------------------
            if (opcode == TFTP_WRQ)
            {

                file = fopen(serverFullPath, "wb");
                if (file == NULL)
                {
                    printf("File not found\n");
                }
                /*-------------------------------------------------------------
                 *          SENDING ACK FOR WRQ
                 *///-------------------------------------------------------------
                char ackPacket[4];
                uint16_t blockNumber = 0;
                // block number is 0 for ACK for WRQ
                createAckPacket(blockNumber, ackPacket);
                sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (const struct sockaddr *)&cli_addr, cli_addr_len);
                /*-------------------------------------------------------------
                 *          RECEIVING DATA PACKET FROM CLIENT
                 *///-------------------------------------------------------------
                char dataPacket[MAX_PACKET_LEN];
                while (true)
                {
                    ssize_t bytesReceived = recvfrom(sockfd, dataPacket, MAX_PACKET_LEN, 0, (struct sockaddr *)&cli_addr, &cli_addr_len);

                    if (bytesReceived < 0)
                    {
                        perror("recvfrom error");
                        fclose(file);
                        return -1; 
                    }
                    /*-------------------------------------------------------------
                     *          PARSING THE BLOCK NUMBER FROM THE RECEIVED DATA PACKET
                     *///-------------------------------------------------------------
                    uint16_t receivedBlockNumber = ntohs(*(uint16_t *)(dataPacket + 2));
                    printf("Received block #%hu\n", receivedBlockNumber);
                    /*-------------------------------------------------------------
                     *          WRITING DATA TO THE FILE
                     *///-------------------------------------------------------------
                    fwrite(dataPacket + 4, 1, bytesReceived - 4, file);
                    /*-------------------------------------------------------------
                     *          CREATING ACK PACKET
                     *///-------------------------------------------------------------
                    createAckPacket(receivedBlockNumber, ackPacket);
                    /*-------------------------------------------------------------
                     *          SENDING ACK PACKET TO THE CLIENT
                     *///-------------------------------------------------------------
                    if (sendto(sockfd, ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&cli_addr, cli_addr_len) == -1)
                    {
                        perror("sendto failed");
                        fclose(file);
                        return -1; 
                    }
                    /*-------------------------------------------------------------
                     *          CHECKING FOR THE END OF FILE
                     *///-------------------------------------------------------------
                    if (bytesReceived < MAX_PACKET_LEN)
                    {
                        break;
                    }
                    else if (bytesReceived == 0)
                    {
                        createAckPacket(receivedBlockNumber, ackPacket);
                        sendto(sockfd, ackPacket, 4, 0, (struct sockaddr *)&cli_addr, cli_addr_len);
                        break;
                    }
                }
                fclose(file);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    program = argv[0];

    int sockfd;
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(SERV_HOST_ADDR);
    serv_addr.sin_port = htons(SERV_UDP_PORT);

    // create a socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind error");
        close(sockfd);
        exit(2);
    }

    handleIncomingRequest(sockfd);

    close(sockfd);
    return 0;
}
