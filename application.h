#include <ROOT-Sim.h>

/* DISTRIBUZIONI TIMESTAMP */
#define UNIFORM		0
#define EXPONENTIAL	1
#define DISTRIBUTION	1

/*
 * EVENT TYPES
 */

enum{
        SEND_BEACON=1,
        SEND_DATA_PACKET=2,
};

/*
 * CTP CONSTANTS
 */

enum{
        CTP_ROOT=3,
        CTP_PULL= 0x80, // TEP 123: P field
        CTP_CONGESTED= 0x40, // TEP 123: C field
        BROADCAST_ADDRESS=0xffff
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
 */

typedef struct{
        unsigned int parent;
        unsigned short etx;
        bool congested;
}route_info;