#include <stdbool.h>
#include "application.h"

/*
 * CONSTANTS RELATED TO ROUTING
 */

enum{
        BEACON_MIN_INTERVAL=8192,
        INVALID_ADDRESS=0xFFFF,
        ROUTING_TABLE_SIZE=10,
        MAX_ONE_HOP_ETX=50,
        INFINITE_ETX = 0xFFFF,
        PARENT_SWITCH_THRESHOLD=15

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
void receive_beacon(ctp_routing_frame* routing_frame, unsigned int from);