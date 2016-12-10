#include <stdbool.h>
#include "application.h"

/*
 * CONSTANTS RELATED TO FORWARDING
 */

enum{
        FORWARDING_QUEUE_DEPTH=13, // Max number of packets that can be stored in the forwarding queue at the same time
        CACHE_SIZE=4, // Max number of packets that can be stored in the output cache at the same time
        MAX_RETRIES=30, // Max number of times the forwarding engine will try to transmit a packet before giving up

        /*
         * When a data packet is sent, after the following time interval an event of time ACK_INTERVAL is delivered to
         * the sender node => at this point it checks whether the packet has been acknowledged by the recipient or not
         * and reacts properly
         */

                DATA_PACKET_ACK_OFFSET=50,

        /*
         * Interval of time after which the node tries to resend a data packet that has not been acknowledged
         */

                DATA_PACKET_RETRANSMISSION_OFFSET=50,
        MIN_PAYLOAD=0, // Lower bound for the range of the data gathered by the node
        MAX_PAYLOAD=100 // Upper bound for the range of the data gathered by the node
};

/*
 * Structure associated to an element of the forwarding queue: it features a pointer to a data packet and a counter of
 * the number of times the engine has already tried to transmitting the packet.
 *
 * In order to send a data packet, a corresponding element of this type has to be stored in the forwarding queue => it
 * will remain in the queue until the packet is sent or the limit for the number of transmissions is reached
 */

typedef struct {
        ctp_data_packet* data_packet; // Pointer to the data packet to send
        unsigned char retries; // Number of transmission attempts performed so far

        /*
         * Flag indicating whether the data packet is local, namely it has been created by the 
         */

        bool is_local;
} forwarding_queue_entry;

/* FORWARDING ENGINE API */

bool send_data_packet();
void receive_ack(bool is_packet_acknowledged);
bool forward_data_packet();