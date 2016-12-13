#include <ROOT-Sim.h>
//#include "link_estimator.h"
//#include "routing_engine.h"

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
        CHECK_ACK_RECEIVED=7 // After the maximum time for receiving an ack has passed, check whether it has been received
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

/*
 * NODE STATE
 *
 * Structure representing the state of a node (logic process) at any point in the virtual time.
 * Its main data structures are those related to the stack of the Collection Tree Protocol, i.e. the Link Estimator, the
 * Routing Engine and the Forwarding Engine
 */

typedef struct _node_state{

        /* LINK ESTIMATOR FIELDS - start */

        /*
         * LINK ESTIMATOR TABLE
         *
         * An array of link_estimator_table_entry with a number of NEIGHBOR_TABLE_SIZE elements: each entry corresponds
         * to a neighbor node
         */

        link_estimator_table_entry link_estimator_table[NEIGHBOR_TABLE_SIZE];

        /*
         * BEACON SEQUENCE NUMBER
         *
         * Sequence number of the beacon, incremented by one at every beacon transmission.
         * By counting the number of beacons received from a neighbor and comparing this with the sequence number
         * reported in the packet, one can determine if, and how many if so, beacons from that neighbor have been lost
         * => this provides an estimate of the ingoing quality of the link to that neighbor
         */

        unsigned char beacon_sequence_number;

        /* LINK ESTIMATOR FIELDS - end */

        /* ROUTING ENGINE FIELDS - start */

        /*
         * ROUTING PACKET (BEACON)
         *
         * Next routing packet to be sent: it contains the routing information of the node, namely the expected number
         * of hops necessary for him to deliver a message to the root of the collection tree
         */

        ctp_routing_packet routing_packet;
        route_info route; // The route from the current node to the root

        /*
         * BEACONS INTERVAL/SENDING TIME
         */

        /*
         * The current value of I_b (interval between sending of two successive beacons)
         */

        unsigned long current_interval;

        /*
         * Instant of time when the next beacon will be sent; it is chosen within the interval [I_b/2 , I_b]
         */

        unsigned long beacon_sending_time;

        /*
         * ROUTING TABLE
         *
         * An array of routing_table_entry with a number of ROUTING_TABLE_SIZE elements, each corresponding to a
         * neighbor node => the routing engine maintains for each the value of ETX and selects the one with the lowest
         * value as parent
         */

        routing_table_entry routing_table[ROUTING_TABLE_SIZE];
        unsigned char neighbors; // Number of active entries in the routing table

        /* ROUTING ENGINE FIELDS - end */

        bool root; // Boolean variable that is set to true if the node is the designated root of the collection tree
        node me; // ID and coordinates of this node (logical process)
        simtime_t lvt; // Value of the Local Virtual Time
} node_state;

void wait_time(simtime_t interval,unsigned int type);
void collected_data_packet(ctp_data_packet* packet);
