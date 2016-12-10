#include <stdbool.h>
#include "application.h"

/*
 * CONSTANTS RELATED TO FORWARDING
 */

enum{
        NEIGHBOR_TABLE_SIZE=10, // Number of entries in the link estimator table (aka neighbor table)

        /*
         * If a node has an 1-hop ETX below this threshold, it is evicted from the estimator table if a new entry has
         * to added and the table itself is full
         */

        EVICT_ETX_THRESHOLD=65,

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
        INVALID_ENTRY=0xff // Index returned when the entry corresponding to a neighbor is not found

};

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

/* LINK ESTIMATOR API */

unsigned short get_one_hop_etx(unsigned int address);
bool unpin_neighbor(unsigned int address);
bool pin_neighbor(unsigned int address);
bool clear_data_link_quality(unsigned int address);
void send_routing_packet(unsigned int src,unsigned int dst,ctp_routing_packet* beacon);
void receive_routing_packet(void* message);
bool pin_neighbor(unsigned int address);
void insert_neighbor(unsigned int address);
node_coordinates get_parent_coordinates(unsigned int parent);
void ack_received(unsigned int recipient,bool received)