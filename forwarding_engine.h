#ifndef SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H
#define SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H

#include <stdbool.h>
//#include "application.h"
typedef struct _ctp_data_packet ctp_data_packet;


/*
 * CONSTANTS RELATED TO FORWARDING
 */

enum{
        FORWARDING_QUEUE_DEPTH=13, // Max number of packets that can be stored in the forwarding queue at the same time
        FORWARDING_POOL_DEPTH=13, // Max number of packets that can be stored in the forwarding pool at the same time
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

        /*
         * Interval of time after which the node tries to resend a data packet in case it has not chosen a parent yet
         */

        NO_ROUTE_OFFSET=100,

        /*
         * Interval of time after which the node tries to resend a data packet after a routing loop has been detected
         * and (hopefully) fixed
         */

        LOOP_DETECTED_OFFSET=100,

        /*
         * Period of the timer that triggers the sending of a new data packet
         */

        SEND_PACKET_TIMER=50,
        MIN_PAYLOAD=0, // Lower bound for the range of the data gathered by the node
        MAX_PAYLOAD=100 // Upper bound for the range of the data gathered by the node
};

/*
 * FORWARDING ENGINE STATE FLAGS
 */

enum{
        SENDING=0x10, // Busy sending a data packet => wait before send another packet
        ACK_PENDING=0x8 // Waiting for the last sent data packet to be acknowledged
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

void create_data_packet(node_state* state);
void start_forwarding_engine(node_state* state);
void receive_ack(bool is_packet_acknowledged,node_state* state);
bool receive_data_packet(void* message,node_state* state);
bool send_data_packet(node_state* state);
void forward_data_packet(ctp_data_packet* packet,node_state* state);

#endif