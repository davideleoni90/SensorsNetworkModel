#ifndef SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H
#define SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H

#include <stdbool.h>

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

        DATA_PACKET_ACK_OFFSET=2,

        /*
         * Interval of time after which the node tries to resend a data packet that has not been acknowledged
         */

        DATA_PACKET_RETRANSMISSION_OFFSET=2,

        /*
         * Interval of time after which the node tries to resend a data packet in case it has not chosen a parent yet
         */

        NO_ROUTE_OFFSET=4,

        /*
         * Interval of time after which the node tries to resend a data packet after a routing loop has been detected
         * and (hopefully) fixed
         */

        LOOP_DETECTED_OFFSET=2,

        /*
         * Period of the timer that triggers the sending of a new data packet
         */

        SEND_PACKET_TIMER=10,
        MIN_PAYLOAD=10, // Lower bound for the range of the data gathered by the node
        MAX_PAYLOAD=100 // Upper bound for the range of the data gathered by the node
};

/* FORWARDING ENGINE API */

void create_data_packet(node_state* state);
void start_forwarding_engine(node_state* state);
void receive_ack(bool is_packet_acknowledged,node_state* state);
bool receive_data_packet(void* message,node_state* state);
bool send_data_packet(node_state* state);
void forward_data_packet(ctp_data_packet* packet,node_state* state);
bool is_congested(node_state* state);

#endif