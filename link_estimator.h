#ifndef SENSORSNETWORKMODELPROJECT_LINK_ESTIMATOR_H
#define SENSORSNETWORKMODELPROJECT_LINK_ESTIMATOR_H

#include <stdbool.h>

typedef struct _ctp_routing_packet ctp_routing_packet;
typedef struct _node_state node_state;
typedef double simtime_t;

/*
 * NODE COORDINATES
 *
 * Each node (logic process) has some spatia coordinates, represented by this structure
 */

typedef struct _node_coordinates{
        int x;
        int y;
}node_coordinates;

/*
 * NODE
 *
 * Each logic process of the simulation corresponds to a node and is uniquely identified by its ID and its coordinates,
 * which are randomly assigned by the simulator with the INIT event
 */

typedef struct _node{
        unsigned int ID;
        node_coordinates coordinates;
}node;

//#include "application.h"

/*
 * CONSTANTS RELATED TO THE LINK ESTIMATOR
 */

enum{
        NEIGHBOR_TABLE_SIZE=10, // Number of entries in the link estimator table (aka neighbor table)

        /*
         * If a node has an 1-hop ETX below this threshold, it is evicted from the estimator table if a new entry has
         * to added and the table itself is full
         */

        EVICT_WORST_ETX_THRESHOLD=65,

        /*
         * If a node has an 1-hop ETX below this threshold, it is evicted from the estimator table if a new entry has
         * to added and the table itself is full AND A FREE PLACE FOR THE ROOT NODE HAS TO BE FOUND
         *
         * Since the root is the most important, it's crucial to create an entry for it when a beacon by it is received
         * => if the table is full, another node has to be replaced => with such a tighter threshold, which corresponds
         * to one hop (recall that ETX is about ten times the number of hops), we are likely to find a victim node
         */

        EVICT_BEST_ETX_THRESHOLD=10,

        /*
         * If the number of beacons lost from a neighbor is bigger than this value, the entry for the neighbor is
         * reinitialized
         */

        MAX_PKT_GAP=10,

        /*
         * If it's not possible to compute the link quality, the 1-hop ETX is set to the highest value as possible, so
         * that the corresponding node will never be chosen as parent
         */

        VERY_LARGE_ETX_VALUE=0xffff,
        ALPHA=9, // The link estimation is exponentially decayed with this parameter ALPHA
        DLQ_PKT_WINDOW=5, // Number of packets to send before updating the outgoing quality of the link to a neighbor
        BLQ_PKT_WINDOW=3, // Number of beacons to receive before updating the ingoing quality of the link to a neighbor
        INVALID_ENTRY=0xff // Value returned when the entry corresponding to a neighbor is not found

};

/*
 * Structure that describes an entry in the link estimator table (or neighbor table): it reports the features of a link
 * to a neighbor node
 */

typedef struct _link_estimator_table_entry{
        node neighbor; // ID and coordinates of the neighbor
        unsigned char lastseq; // Last beacon sequence number received from the neighbor

        /*
         * Number of beacons received after the last update of the outgoing link quality: such an update takes place
         * after BLQ_PKT_WINDOW beacons have been received
         */

        unsigned char beacons_received;

        /*
         * Number of beacons missed after the last update of the outgoing link quality: such an update takes place
         * after BLQ_PKT_WINDOW beacons have been received
         */

        unsigned char beacons_missed;
        unsigned char flags; // Flags describing the state of the entry (see above)
        unsigned char ingoing_quality; // Ingoing quality of the link ranges from 1 (bad) to 255 (good)
        unsigned short one_hop_etx; // 1-hop etx of the neighbor

        /*
         * Number of data packets acknowleged after the last update of the outgoing link quality: such an update takes
         * place after DLQ_PKT_WINDOW data packets have been sent
         */

        unsigned char data_acknowledged;

        /*
         * Number of data packets transmitted after the last update of the outgoing link quality: such an update takes
         * place after DLQ_PKT_WINDOW data packets have been sent
         */

        unsigned char data_sent;
}link_estimator_table_entry;

/*
 * Flags for the neighbor table entry
 */

enum {

        /*
         * The entry corresponding to a neighbor becomes invalid if no beacon is received from him within a certain
         * timeout
         */

                VALID_ENTRY = 0x1,

        /*
         * A link becomes mature after BLQ_PKT_WINDOW packets are received and an estimate is computed
         */

                MATURE_ENTRY = 0x2,

        /*
         * Flag to indicate that this link has received the first sequence number
         */

                INIT_ENTRY = 0x4,

        /*
         * Flag indicates that the 1-hop ETX of the neighbor is 0, thus it's the root of the tree; also it indicates
         * that the node is selected as the current parent node
         */

                PINNED_ENTRY = 0x8
};

/* LINK ESTIMATOR API */

unsigned short get_one_hop_etx(unsigned int address,link_estimator_table_entry* link_estimator_table);
bool unpin_neighbor(unsigned int address,link_estimator_table_entry* link_estimator_table);
bool pin_neighbor(unsigned int address,link_estimator_table_entry* link_estimator_table);
bool clear_data_link_quality(unsigned int address,link_estimator_table_entry* link_estimator_table);
void send_routing_packet(ctp_routing_packet* beacon,unsigned char beacon_sequence,node me,simtime_t now);
void receive_routing_packet(void* message,node_state* state);
bool pin_neighbor(unsigned int address,link_estimator_table_entry* link_estimator_table);
void insert_neighbor(node neighbor,link_estimator_table_entry* link_estimator_table);
node_coordinates* get_parent_coordinates(unsigned int parent,link_estimator_table_entry* link_estimator_table);
void check_if_ack_received(unsigned int recipient,bool ack_received,link_estimator_table_entry* link_estimator_table);

#endif