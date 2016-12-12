#include <stdbool.h>
#include "application.h"

/*
 * CONSTANTS RELATED TO ROUTING
 */

enum{
        UPDATE_ROUTE_TIMER=8192, // After such interval of time, the route of the node is (re)computed
        INVALID_ADDRESS=0xFFFF, // Value used for the ID of neighbor that is not valid
        ROUTING_TABLE_SIZE=10, // Max number of entries in the routing table

        /*
         * Neighbors whose links have a 1-hop ETX bigger than or equal to this threshold can't be selected as parent
         */

        MAX_ONE_HOP_ETX=50,
        INFINITE_ETX = 0xFFFF, // Highest value for ETX => it's used to avoid that neighbor is selected as parent

        /*
         * If the current parent is not congested, a new parent is chosen only if the associated route has an ETX that
         * is at least PARENT_SWITCH_THRESHOLD less than the ETX of the current route
         */

        PARENT_SWITCH_THRESHOLD=15,
        MIN_BEACONS_SEND_INTERVAL=128, // Minimum value (max frequency) for the interval between two beacons sent
        MAX_BEACONS_SEND_INTERVAL=512000 // Maximum value (min frequency) for the interval between two beacons sent
};

typedef struct{
        unsigned int neighbor;
        route_info info;


}routing_table_entry;

/* ROUTING ENGINE API */

void neighbor_evicted(unsigned int address);
bool get_etx(unsigned short* etx);
node get_parent();
void update_route();
void receive_beacon(ctp_routing_frame* routing_frame, node from);
void send_beacon();
void schedule_beacons_interval_update();
void double_beacons_send_interval();