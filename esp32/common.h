#ifndef COMMON_H
#define COMMON_H

#define MAGIC           0xABCDDCBA
#define MAX_DATA_SIZE   8

//type
//8 bits. 4msb for the method or result while 4lsb for url
//bitwise or to combine the status part with url part
#define URL_CONTROLLER_INPUT    0b00000001 //1 controller/input
#define URL_GAME_INPUT          0b00000010 //2 game/input
#define URL_CONTROLLER_OUTPUT   0b00000011 //3 controller/output
#define URL_GAME_OUTPUT         0b00000100 //4 game/output
#define URL_MASK                0b00001111

#define STATUS_CREATE           0b00010000 //1 << 4 
#define STATUS_POST             0b00100000 //2 << 4 
#define STATUS_GET              0b00110000 //3 << 4 
#define STATUS_FAIL             0b01000000 //4 << 4 
#define STATUS_SUCCESS          0b01010000 //5 << 4 
#define STATUS_MASK             0b11110000

#define get_status(status_url)  ((status_url) & STATUS_MASK)
#define get_url(status_url)  ((status_url) & URL_MASK)

#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#ifdef MY_ESP
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

std::atomic<bool> connected{false};
#endif //MY_ESP

//4 + 8 + 1 + 3 = 16bytes
//copy from CG2111 but I rename the struct
typedef struct netpacket {
	uint32_t magic;
	uint8_t buffer[MAX_DATA_SIZE];
	uint8_t checksum;
	uint8_t dummy[3];
} NPACKET;

typedef struct packet {
    uint8_t status_url;
    uint8_t data[MAX_DATA_SIZE - 1];
} PACKET;


void Send(uint8_t data[MAX_DATA_SIZE - 1], uint8_t status_url, int sockfd);
void Recv(uint8_t data[MAX_DATA_SIZE - 1], uint8_t *status_url, int sockfd);

// void create(uint8_t data[MAX_DATA_SIZE - 1], uint8_t url, int sockfd);
// void post(uint8_t data[MAX_DATA_SIZE - 1], uint8_t url, int sockfd);
// void get(uint8_t (*data)[MAX_DATA_SIZE - 1], uint8_t url, int sockfd);

void make_packet(PACKET *packet, uint8_t status_url, uint8_t *data);
size_t serialise(PACKET packet, NPACKET *npacket);
size_t deserialise(PACKET *packet, NPACKET npacket);



#ifdef COMM_IMPLEMENTATION


void Send(uint8_t data[MAX_DATA_SIZE - 1], uint8_t status_url, int sockfd) {
    PACKET packet;
    make_packet(&packet, status_url, data);

    NPACKET npacket;
    size_t nsent = 0;
    size_t npacket_size = serialise(packet, &npacket);

    while (nsent < npacket_size) {
        int n = send(sockfd, (uint8_t *)&npacket + nsent, npacket_size - nsent, 0); 

        if (n < 0) {
            int error = errno;
#ifdef MY_ESP
            Serial.printf("send error %d: %s\n", error, strerror(error));

            if (error == EINTR) {
                continue;
            }

            if (error == EAGAIN || error == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            return;
#else
            printf("send error\n");
            if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK) {
                continue;
            }
            //exit(0);
#endif //MY_ESP
        }

        if (n == 0) {
#ifdef MY_ESP
            Serial.print("Disconnected\n");
            return;
#else 
            printf("Disconnected\n");
            exit(0);
#endif //MY_ESP
        }

        nsent += n;
    }
}

void Recv(uint8_t data[MAX_DATA_SIZE - 1], uint8_t *status_url, int sockfd) {
    NPACKET npacket;
    size_t nreceived = 0;
    while (nreceived < sizeof(NPACKET)) {
        int n = recv(sockfd, (uint8_t *)&npacket + nreceived, sizeof(NPACKET) - nreceived, 0);

        if (n < 0) {
            int error = errno;
#ifdef MY_ESP
            Serial.printf("recv error %d: %s\n", error, strerror(error));
            if (error == EINTR ) {
                continue;
            }
            if (error == EAGAIN || error == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            connected = false;
            vTaskDelete(NULL);
            return;
#else
            printf("recv error\n");
            if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK) {
                continue;
            }
            exit(0);
#endif //MY_ESP
            
        }

        if (n == 0) {
#ifdef MY_ESP
            Serial.print("Disconnected\n");
            connected = false;
            vTaskDelete(NULL);
            return;
#else 
            printf("Disconnected\n");
            exit(0);
#endif //MY_ESP
        }

        nreceived += n;
    }
    
    
    PACKET packet;
    deserialise(&packet, npacket);

    *status_url = packet.status_url;
    memcpy(data, packet.data, MAX_DATA_SIZE - 1);
}


//------------------------Serialisaton--------------------------------------
void make_packet(PACKET *packet, uint8_t status_url, uint8_t *data) {
    packet->status_url = status_url;
    
    memcpy((void *)packet->data, (void *)data, MAX_DATA_SIZE - 1);
}

size_t serialise(PACKET packet, NPACKET *npacket) {
    npacket->magic = MAGIC;

    memcpy((void *)npacket->buffer, (void *)&packet, MAX_DATA_SIZE);

    //xor
    uint8_t checksum = 0; //identity
    for (size_t i = 0; i < MAX_DATA_SIZE; i++) {
        checksum ^= npacket->buffer[i];
    }
    npacket->checksum = checksum;

    //paddings
    npacket->dummy[0] = 0;
    npacket->dummy[1] = 0;
    npacket->dummy[2] = 0;

    return sizeof(NPACKET);
}

size_t deserialise(PACKET *packet, NPACKET npacket) {
    if (npacket.magic != MAGIC) {
        return 1;
    }

    //xor
    uint8_t checksum = 0; //identity
    for (size_t i = 0; i < MAX_DATA_SIZE; i++) {
        checksum ^= npacket.buffer[i];
    }
    if (npacket.checksum != checksum) {
        return 1;
    }

    memcpy((void *)packet, npacket.buffer, MAX_DATA_SIZE);
    return 0;
}
#endif //FAKE_REST_IMPLEMENTATION
#endif //COMMON_H