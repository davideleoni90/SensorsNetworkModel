#include <ROOT-Sim.h>

/* DISTRIBUZIONI TIMESTAMP */
#define UNIFORM		0
#define EXPONENTIAL	1
#define DISTRIBUTION	1

/*
 * EVENT TYPES
 *
 * List of IDs associated to the events of the simulation
 */

enum{
        SEND_BEACONS_TIMER_FIRED=1, // The timer for beacons has been fired  => broadcast a beacon
        UPDATE_ROUTE_TIMER_FIRED=2, // The timer for updating the route has been fired
        SET_BEACONS_TIMER=3, // The interval of the timer for beacons has to be updated
        DATA_PACKET_RECEIVED=4, // The node has received a data packet
        BEACON_RECEIVED=5, // The node has received a data packet
        RETRANSMITT_DATA_PACKET=6, // Try to re-send a data packet whose first sending attempt failed
        CHECK_ACK_RECEIVED=7, // After the maximum time for receiving an ack has passed, check wheter it has been received
};

/*
 * CTP CONSTANTS
 */

enum{
        CTP_ROOT=3,
        CTP_PULL= 0x80, // TEP 123: P field
        CTP_CONGESTED= 0x40, // TEP 123: C field
        BROADCAST_ADDRESS=0xffff, // A packet with such an address is sent to all the neighbor nodes

        /*
         * Lower bound of data packets received by the root for the simulation to stop
         */

        COLLECTED_DATA_PACKETS_GOAL=10
};

/*
 * NODE COORDINATES
 *
 * Each node (logic process) has some spatia coordinates, represented by this structure
 */

typedef struct{
        int x;
        int y;
}node_coordinates;

/*
 * NODE
 *
 * Each logic process of the simulation corresponds to a node and is uniquely identified by its ID and its coordinates,
 * which are randomly assigned by the simulator with the INIT event
 */

typedef struct{
        unsigned int ID;
        node_coordinates coordinates;
}node;

/*
 * PHYSICAL & DATA LINK OVERHEAD
 */

typedef struct{
        node src;
        node dst;
}physical_datalink_overhead;

/*
 * CTP LINK ESTIMATOR FRAME
 */

typedef struct{
        unsigned char seq;
}ctp_link_estimator_frame;

/*
 * CTP ROUTING FRAME
 */

typedef struct{
        unsigned char options;
        unsigned int parent;
        unsigned char ETX;
}ctp_routing_frame;

/*
 * CTP ROUTING PACKET
 */

typedef struct{
        physical_datalink_overhead phy_mac_overhead;
        ctp_link_estimator_frame link_estimator_frame;
        ctp_routing_frame routing_frame;
}ctp_routing_packet;

/*
 * CTP DATA FRAME
 */

typedef struct{
        unsigned char options;
        unsigned char THL;
        unsigned short ETX;
        unsigned int origin;
        unsigned char seqNo;
}ctp_data_packet_frame;

/*
 * CTP DATA PACKET
 */

typedef struct{
        physical_datalink_overhead phy_mac_overhead;
        ctp_data_packet_frame data_packet_frame;
        int payload;
}ctp_data_packet;

/*
 * ROUTE INFO
 *
 * Structure describing the current path chosen by a node to send data packets
 */

typedef struct{
        unsigned int parent; // ID of the parent node
        unsigned short etx; // ETX of the parent node + 1-hop ETX of the link to the parent node
        //bool congested;
}route_info;

void wait_time(simtime_t interval,unsigned int type);
void collected_data_packet(ctp_data_packet* packet);