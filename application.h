#ifndef SENSORSNETWORKMODELPROJECT_APPLICATION_H
#define SENSORSNETWORKMODELPROJECT_APPLICATION_H

#include "link_estimator.h"
#include "routing_engine.h"
#include "forwarding_engine.h"
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
        SEND_PACKET_TIMER_FIRED=2, // The timer for data packets has been fired  => send a data packet
        UPDATE_ROUTE_TIMER_FIRED=3, // The timer for updating the route has been fired
        SET_BEACONS_TIMER=4, // The interval of the timer for beacons has to be updated
        DATA_PACKET_RECEIVED=5, // The node has received a data packet
        BEACON_RECEIVED=6, // The node has received a data packet
        RETRANSMITT_DATA_PACKET=7, // Try to re-send a data packet whose first sending attempt failed
        CHECK_ACK_RECEIVED=8 // After the maximum time for receiving an ack has passed, check whether it has been received
};

/*
 * STATE FLAGS
 *
 * Flags that indicate the state of the node
 */

enum{
        SENDING=0x1, // Busy sending a data packet => wait before send another packet
        ACK_PENDING=0x2, // Waiting for the last sent data packet to be acknowledged
        RUNNING=0x4 // The node is running => has not failed (yet)
};

/*
 * CTP CONSTANTS
 */

enum{
        CTP_PULL= 0x80, // TEP 123: P field
        CTP_CONGESTED= 0x40, // TEP 123: C field
        BROADCAST_ADDRESS=0xffff, // A packet with such an address is sent to all the neighbor nodes

        /*
         * Time for a message to be delivered to its recipient
         */

        MESSAGE_DELIVERY_TIME=1,

        /*
         * Lower bound of data packets received by the root for the simulation to stop
         */

        COLLECTED_DATA_PACKETS_GOAL=10,

        /*
         * Maximum euclidean distance between two nodes for them to be considered neighbors
         */

        NEIGHBORS_MAX_DISTANCE=10,

        /*
         * If the euclidean distance between two neighbor nodes is less than this constants, every message sent by
         * either of the nodes is certainly received by the other node; if the distance is it equal or greater than,
         * message may or may not be received
         */

        NEIGHBORS_SAFE_DISTANCE=6
};

/*
 * PHYSICAL & DATA LINK OVERHEAD
 */

typedef struct _physical_datalink_overhead{
        node src;
        node dst;
}physical_datalink_overhead;

/*
 * CTP LINK ESTIMATOR FRAME
 */

typedef struct _ctp_link_estimator_frame{
        unsigned char seq;
}ctp_link_estimator_frame;

/*
 * CTP ROUTING FRAME
 */

typedef struct _ctp_routing_frame{
        unsigned char options;
        unsigned int parent;
        unsigned char ETX;
}ctp_routing_frame;

/*
 * CTP ROUTING PACKET
 */

typedef struct _ctp_routing_packet{
        physical_datalink_overhead phy_mac_overhead;
        ctp_link_estimator_frame link_estimator_frame;
        ctp_routing_frame routing_frame;
}ctp_routing_packet;

/*
 * CTP DATA FRAME
 */

typedef struct _ctp_data_packet_frame{
        unsigned char options;
        unsigned char THL;
        unsigned short ETX;
        unsigned int origin;
        unsigned char seqNo;
}ctp_data_packet_frame;

/*
 * CTP DATA PACKET
 */

typedef struct _ctp_data_packet{
        physical_datalink_overhead phy_mac_overhead;
        ctp_data_packet_frame data_packet_frame;
        int payload;
}ctp_data_packet;

/*
 * ROUTE INFO
 *
 * Structure describing the current path chosen by a node to send data packets
 */

typedef struct _route_info{
        unsigned int parent; // ID of the parent node
        unsigned short etx; // ETX of the parent node + 1-hop ETX of the link to the parent node
        bool congested; // Boolean flag telling whether the node is congested (half of its forwarding queue full) or not
}route_info;

typedef struct _routing_table_entry{
        unsigned int neighbor;
        route_info info;
}routing_table_entry;

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
         * Time to wait before sending another beacon; it is chosen within the interval [I_b/2 , I_b]
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

        /* FORWARDING ENGINE FIELDS - start */

        /*
         * FORWARDING POOL - start
         *
         * When a data packet has to be forwarded, the node extracts one entry from this pool, initializes it to the
         * data of the data packet received and finally stores a pointer to the entry in the forwarding queue.
         *
         * The pool is nothing more than fixed-size array, whose elements are of type "forwarding_queue_entry": in fact
         * packets to be forwarded are stored in the same output queue as packets created by the node itself and as soon
         * as they reach the head of the queue they are sent.
         *
         * An entry is taken from the pool using the "get" method and it is given back to the pool using the "put"
         * method: the entries are taken in order, according to their position, and are released in order.
         *
         * Two variables help handling the pool:
         *
         * 1-forwarding_pool_count
         * 2-forwarding_pool_index
         */

        forwarding_queue_entry forwarding_pool[FORWARDING_POOL_DEPTH];
        unsigned char forwarding_pool_count; // Number of elements in the pool
        unsigned char forwarding_pool_index; // Index of the array where the next entry put will be collocated

        /* FORWARDING POOL - end */

        /*
         * FORWARDING QUEUE - start
         *
         * An array of elements of pointers to "forwarding_queue_entry" represents the output queue of the node; pointers
         * refer to packets (actually entries) created by the node or packets (entries) received by other nodes
         * that have to be forwarded.
         *
         * Three variables are necessary to implement the logic of a FIFO queue using such an array:
         *
         * 1-forwarding_queue_count
         * 2-forwarding_queue_head
         * 3-forwarding_queue_tail
         *
         * They are all set to 0 at first. Entries are not explicitly cleared when elements are dequeued, nor the head
         * of the queue is always represented by the first element of the array => the actual positions of the elements
         * in the array don't correspond to their logical position within the queue.
         *
         * As a consequence, it is necessary to check the value of "forwarding_queue_count" in order to determine
         * whether the queue is full or not.
         *
         * A packet is enqueued before being sent: when it reaches the head of the queue it is forwarded; at some point
         * it is then dequeued
         */

        forwarding_queue_entry* forwarding_queue[FORWARDING_QUEUE_DEPTH];

        unsigned char forwarding_queue_count; // The counter of the elements in the forwarding queue
        unsigned char forwarding_queue_head; // The index of the first element in the queue (least recently added)
        unsigned char forwarding_queue_tail; // The index of the last element in the queue (most recently added)

        /* FORWARDING QUEUE - end */

        /*
         * OUTPUT CACHE - start
         *
         * An array of pointers to data packets represents the output LRU (Least Recently Used )cache of the node, where
         * are stored the most recently packets sent by the node => it's used to avoid forwarding the same packet twice.
         *
         * NOTE it is assumed that the node does not produce duplicates on its own => duplicates only regard packets to
         * be forwarded; usually they are caused by not acknowledged packets
         *
         * Two variables are necessary to implement the logic of a LRU cache:
         *
         * 1-output_cache_count
         * 2-output_cache_first
         *
         * They are both set to 0 at first. Entries are not explicitly cleared when elements are removed, nor the least
         * recent entry of the cache is always represented by the first element of the array => the actual positions of
         * the elements in the array don't correspond to their logical position within the cache.
         *
         * As a consequence, it is necessary to check the value of "output_cache_count" in order to determine whether
         * the cache is full or not.
         *
         * When a packet is inserted in the cache, it means it has been used => if the same packet is already in the
         * cache, it is moved in order to indicate that it has been recently accessed, otherwise the element that was
         * least recently used is removed
         */

        ctp_data_packet output_cache[CACHE_SIZE];

        unsigned char output_cache_count; // Number of sent data packets cached
        unsigned char output_cache_first; // Index of the entry in the cache that was least recently added

        /* OUTPUT CACHE - end */

        /*
         * DATA PACKET
         *
         * Next data packet to be sent: it carries the actual payload the node wants to be delivered to the root of the
         * tree
         */

        ctp_data_packet data_packet;

        /*
         * LOCAL FORWARDING QUEUE ENTRY
         *
         * The entry of the forwarding queue associated to the current node when this has some data to be sent
         */

        forwarding_queue_entry local_entry;

        unsigned char data_packet_seqNo; // Sequence number of the data packet to be sent (initially 0)

        /* FORWARDING ENGINE FIELDS - end */

        bool root; // Boolean variable that is set to true if the node is the designated root of the collection tree
        node me; // ID and coordinates of this node (logical process)
        unsigned char state; // Bit-wise OR combination of flags indicating the state of the node
        simtime_t lvt; // Value of the Local Virtual Time
} node_state;

void wait_until(unsigned int me,simtime_t timestamp,unsigned int type);
void collected_data_packet(ctp_data_packet* packet);
void broadcast_event(ctp_routing_packet* beacon,simtime_t time);
void unicast_event(ctp_data_packet* packet,simtime_t time);

#endif