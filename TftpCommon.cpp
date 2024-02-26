#include <cstdio>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <string>
#include <cstring>

#include "TftpError.h"
#include "TftpOpcode.h"
#include "TftpConstant.h"

// helper method to create request packet
static void createRequestPacket(int opcode, char *filename, char *buffer, size_t *packetSize) {
    const char *mode = "octet";
    char *bpt = buffer; // point to the beginning of the buffer
    *(unsigned short *)bpt = htons((unsigned short)opcode);
    bpt += 2;
    memcpy(bpt, filename, strlen(filename) + 1);
    bpt += strlen(filename) + 1;
    memcpy(bpt, mode, strlen(mode) + 1);
    bpt += strlen(mode) + 1;  
    *packetSize = bpt - buffer; 
}

// helper method to create data packet
static void createDataPacket(unsigned short blockNum, const char* data, size_t dataLen, char* buffer, size_t* packetSize) {
    char * bpt = buffer; // point to the beginning of the buffer
    *(unsigned short*)bpt = htons(TFTP_DATA);
    bpt += 2;
    *(unsigned short*)bpt = htons(blockNum);
    bpt += 2;
    if (dataLen > MAX_PACKET_LEN - 4) {
        dataLen = MAX_PACKET_LEN - 4;
    }
    memcpy(bpt, data, dataLen);
    bpt += dataLen;
    *packetSize = bpt - buffer;
}

// helper method to create ACK packet
static void createAckPacket(unsigned short blockNum, char* buffer) {
    char* bpt = buffer;
    *(unsigned short*)bpt = htons(TFTP_ACK);
    bpt += 2; 
    *(unsigned short*)bpt = htons(blockNum);
    bpt += 2; 
}

// Helper function to print the first len bytes of the buffer in Hex
static void printBuffer(const char *buffer, unsigned int len)
{
    for (int i = 0; i < len; i++)
    {
        printf("%x,", buffer[i]);
    }
    printf("\n");
}
