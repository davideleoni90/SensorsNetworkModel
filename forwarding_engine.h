#ifndef SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H
#define SENSORSNETWORKMODELPROJECT_FORWARDING_ENGINE_H

#include <stdbool.h>

typedef struct _ctp_data_packet ctp_data_packet;

#ifndef forwarding_queue_depth
#define forwarding_queue_depth 13 // Max number of packets that can be stored in the forwarding queue at the same time
#endif

#ifndef forwarding_pool_depth
#define forwarding_pool_depth 13 // Max number of packets that can be stored in the forwarding pool at the same time
#endif

#ifndef cache_size
#define cache_size 4 // Max number of packets that can be stored in the output cache at the same timee
#endif

/*
 * PARAMETERS RELATED TO FORWARDING ENGINE
 */

enum{
        MAX_RETRIES=30, // Max number of times the forwarding engine will try to transmit a packet before giving up

        /*
         * Interval of time after which the node tries to resend a data packet that has not been successfully sent or
         * acknowledged (in ms)
         */

        DATA_PACKET_RETRANSMISSION_OFFSET=22,
        DATA_PACKET_RETRANSMISSION_DELTA=7, // Delta applied to calculate the random interval before a retransmission


        /*
         * Interval of time after which the node tries to resend a data packet in case it has not chosen a parent yet
         * (in seconds)
         */

        NO_ROUTE_OFFSET=10,

        SEND_PACKET_TIMER=10, // Period of the timer that triggers the sending of a new data packet (in seconds)
        MIN_PAYLOAD=10, // Lower bound for the range of the data gathered by the node
        MAX_PAYLOAD=100 // Upper bound for the range of the data gathered by the node
};

/* FORWARDING ENGINE API */

void create_data_packet(node_state* state);
void start_forwarding_engine(node_state* state);
bool send_data_packet(node_state* state);
void forward_data_packet(ctp_data_packet* packet,node_state* state);
void transmitted_data_packet(node_state* state,bool result);
void received_data_packet(void* message,node_state* state);
bool is_congested(node_state* state);
void parse_forwarding_engine_parameters(void* event_content);

#endif