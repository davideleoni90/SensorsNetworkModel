/*
 * ROUTING ENGINE
 *
 * This module of the Collection Tree Protocol Stack is in charge of sending to and receiving beacons from other nodes
 * in the network in order to maintain a routing table.
 *
 * This table contains a list of neighbors among which a node can select its parent and it is filled with the
 * information extracted from beacons.
 *
 * The metric used to choose a parent is the ETX (Expected Transmissions), communicated by a node to its neighbors by
 * mean of beacons: the ETX of a node is equal to the ETX of its parent plus the 1-hop ETX of the link between them.
 * ETX gives an estimate of the number of transmissions required to send a data from a node to the root of the
 * collection tree.
 *
 * The frequency at which beacons are sent follows the Trickle Algorithm. The time instant for sending a beacon is
 * chosen randomly in the interval [I_b/2 , I_b]; the value of I_b is doubled after each transmission, so the frequency
 * is progressively reduced. The minimal value for I_b is I_bmin and is set a priori, as well as the maximal value
 * I_bmax.
 *
 *
 */

#include <stddef.h>
#include <ROOT-Sim.h>
#include "routing_engine.h"
#include "link_estimator.h"
#include "application.h"

//TODO node_running

/*
 * ROUTING FRAME
 *
 * Pointer to the data frame of the next data packet to be sent
 */

ctp_routing_frame* routing_frame;

/*
 * ROUTING PACKET (BEACON)
 *
 * Next routing packet to be sent: it contains the routing information of the node, namely the expected number of hops
 * necessary for him to deliver a message to the root of the collection tree
 */

ctp_routing_packet routing_packet;

route_info route; // The route from the current node to the root
unsigned char self; // ID of the current node
bool is_root; // Boolean variable which is set if the current node is the root of the collection tree

/* VIRTUAL TIME */

extern simtime_t timestamp;

/* DATA PACKETS DELIVERED */

extern unsigned int delivered_packets;

/*
 * BEACONS INTERVAL/SENDING TIME
 */

/*
 * The current value of I_b; at first set it to its minimum possible value
 */

unsigned long current_interval=BEACON_MIN_INTERVAL;

/*
 * Instant of time when the next beacon will be sent; it is chosen within the interval [I_b/2 , I_b]
 */

unsigned long beacon_sending_time;

/*
 * This variable indicates if a time equal to I_b has elapsed, since I_b has been set
 */

unsigned long max_beacon_interval_elapsed;

/*
 * ROUTING TABLE
 *
 * An array of routing_table_entry with a number of ROUTING_TABLE_SIZE elements
 */

routing_table_entry routing_table[ROUTING_TABLE_SIZE];
unsigned char neighbors; // number of active entries not in the routing table

/*
 * INITIALIZE ROUTING ENGINE
 *
 * This function is in charge of initializing the variables of the routing engine, in particular the ID of this node
 * and the ROUTING TABLE.
 * It is invoked when the node (logical process) is delivered the INIT event.
 *
 * @self: ID of this node (logical process)
 */

void init_routing_engine(unsigned int ID){

        /*
         * Store the ID of this node
         */

        self=ID;

        /*
         * Set the number of valid entries in the ROUTING TABLE to 0
         */

        neighbors=0;

        /*
         * Check if the current node is designed to be the root of the collection tree in this simulation; set the
         * flag "is_root" as consequence
         */

        if(self==CTP_ROOT)
                is_root=true;
        else
                is_root=false;

        /*
         * Initialize the route from this node to the root of the collection tree
         */

        init_route_info(&route);

        /*
         * Initialize the pointer to the next beacon to be sent
         */

        routing_frame=&routing_packet;
}

/*
 * Method to initialize a variable describing a path from one node to the root: simply sets all the fields to  their
 * default value
 */

void init_route_info(route_info * route) {
        route->parent= INVALID_ADDRESS; // ID of the parent of the node associated to the entry
        route->etx = 0; // multi-hop etx of the node
        route->congested = false;
}

/*
 * FIND INDEX ROUTING TABLE
 *
 * Find the entry in the routing table matching the given ID
 *
 * @address: the ID of the node we are looking for
 *
 * Returns the index of the entry matching the given ID, or the number of neighbors in the table otherwise
 */

unsigned char find_index_routing_table(unsigned int address){

        /*
         * Return value: the index of the matching entry
         */

        unsigned char index;

        /*
         * If the address is not valid, return the number of neighbors in the table
         */

        if(address==INVALID_ADDRESS)
                return neighbors;

        /*
         * Scan the routing table and stop when an entry matching the given ID is found
         */

        for (int index = 0; index < neighbors; ++index) {
                if(routing_table[index].neighbor==address)
                        break;
        }

        /*
         * Return the index: either it's equal to the index of the entry that matches the given address or it's equal
         * to the number of neighbors in the table
         */

        return index;
}

/*
 * REMOVE ENTRY ROUTING TABLE
 *
 * Remove from the ROUTING TABLE the entry corresponding to the given ID
 *
 * @address: ID of the node whose entry has to be removed
 */

void remove_entry_routing_table(unsigned int address){

        /*
         * Index of the entry corresponding to the ID of the given neighbor
         */

        unsigned char index;

        /*
         * Variable used to iterare through the entries of the routing table
         */

        unsigned char i;

        /*
         * Find the index
         */

        index=find_index_routing_table(address);

        /*
         * Check if an entry for the given neighbor exists: if not, return
         */

        if(index==neighbors) {

                /*
                 * The whole routing table was scanned without finding the entry of the neighbor => the index returned
                 * is equal to the number of "tracked" neighbors => return
                 */

                return;
        }

        /*
         * Removing an entry from the routing table means replacing each entry with the one after it, starting from
         * the entry that has to be removed => first the number of valid entries (associated to neighbors) has to be
         * updated
         */

        --neighbors;

        /*
         * Update the index of all the entries of the table after the one just removed
         */

        for(i=index;i<neighbors;i++)
                routing_table[i]=routing_table[i+1];
}

/*
 * UPDATE NEIGHBOR CONGESTED
 *
 * Update the flag "congested" of the entry corresponding to the given ID.
 * If the node that sent the beacon is not congested and the route of the receiver is congested instead,it triggers
 * a route update in order to select the sender as new parent.
 * Finally, if the node that sent the beacon is the actual parent of the node that received it and the sender is
 * congested, the receiver triggers a route update in order to select another node as parent.
 */

void update_neighbor_congested(unsigned int address,bool congested){

        /*
         * Index of the entry in the ROUTING TABLE corresponding to the given address
         */

        unsigned char index;

        /*
         * Find the index
         */

        index=find_index_routing_table(address);

        /*
         * If an entry corresponding to the given address exists, the index returned is less than the actual value of
         * variable "neighbors" => check such an entry exists
         */

        if(index<neighbors){

                /*
                 * Update the "congested" flag of the given neighbor
                 */

                routing_table[index].info.congested=congested;

                /*
                 * If the given node is congested and it's the actual parent or if the actual route is congested and the
                 * given node is not, trigger the route update
                 */

                if((congested && route.parent==address) ||(!congested && route.congested))
                        update_route();
        }
}

/*
 * UPDATE ROUTING TABLE
 *
 * When a beacon is received by a node, its routing table has to be updated
 *
 * @from: ID of the node that sent the beacon
 * @parent: the ID of the actual parent of the node that sent the beacon
 * @etx: the ETX of the actual route of the node that sent the beacon
 */

void update_routing_table(unsigned int from, unsigned int parent, unsigned short etx){

        /*
         * Index of the entry in the table to be updated
         */

        unsigned char index;

        /*
         * 1-hop ETX of the link to the neighbor associated to the entry of the table
         */

        unsigned short one_hop_etx;

        /*
         * Get 1-hop ETX of the link to the neighbor associated to the entry of the table
         */

        one_hop_etx=get_one_hop_etx(from);

        /*
         * Check whether an entry in the routing table exists for the node that sent the beacon
         */

        index=find_index_routing_table(from);

        /*
         * If the index is equal to the size of the routing table, it means that no entry matches the ID of the sender
         * and the table is full => discard packet
         */

        if(index==ROUTING_TABLE_SIZE){


                //TODO remove an entry???
        }
        else if(index==neighbors){

                /*
                 * No entry matches the ID of the sender, but the table is not full => we create a new entry for the
                 * sender, but only if the quality of the link to him is beyond a threshold (1-hop ETX<MAX_ONE_HOP_ETX)
                 */

                if(one_hop_etx<MAX_ONE_HOP_ETX){

                        /*
                         * The quality of the link to the sender passed the check => initialize the first free entry of
                         * the routing table with the info extracted from the beacon.
                         * First set the identity of the node in the new entry
                         */

                        routing_table[index].neighbor=from;

                        /*
                         * Set the parent of the sender
                         */

                        routing_table[from].info.parent=parent;

                        /*
                         * Set the ETX of the sender
                         */

                        routing_table[from].info.etx=etx;

                        /*
                         * Mark the node as non congested
                         */

                        routing_table[from].info.congested=false;

                        /*
                         * Update the counter of neighbors "tracked" in the neighbor table
                         */

                        ++neighbors;
                }
        }
        else{

                /*
                 * An entry dedicated to the given node already exists in the routing table, so we just have to update
                 * it according to the information brought by the beacon.
                 */

                routing_table[from].info.etx=etx;
                routing_table[from].info.parent=parent;
                routing_table[from].neighbor=from;
        }
}

/*
 * UPDATE ROUTE
 *
 * On the basis of the beacons sent by neighbors (received by the link estimator and passed to the routing engine), the
 * route from the current node to root of the tree is (re)computed, i.e. a parent is chosen among the neighbors. Such
 * an update of the route may also be induced by the fact that a neighbor does not acknowledges data packets.
 */

void update_route(){

        /*
         * Index used to scan the routing table
         */

        unsigned char i;

        /*
         * Pointer to the current entry of the routing table, used during a scan
         */

        routing_table_entry* current_entry;

        /*
         * Pointer to the entry with the lowest ETX found so far, used during a scan
         */

        routing_table_entry* best_entry;

        /*
         * Lowest ETX found so far, while scanning the routing table
         */

        unsigned short min_etx;

        /*
         * ETX of the current entry of the routing table, used while scanning
         */

        unsigned short current_etx;

        /*
         * 1-hop ETX of the current entry of the routing table, used while scanning
         */

        unsigned short current_one_hop_etx;

        /*
         * ETX of the path through the actual route
         */

        unsigned short actual_etx;

        /*
         * If the current node is the root of the tree, there's no parent to select, so just return
         */

        if(is_root)
                return;

        /*
         * The current node is not the root => set the above variables before using them to scan the routing table.
         * Set the best entry to NULL, because the scan has not started yet
         */

        best_entry=NULL;

        /*
         * Initially set the minimum and current etx to infinite (INFINITE_ETX)
         */

        min_etx=INFINITE_ETX;
        current_etx=INFINITE_ETX;

        /*
         * Scan the entries of the routing table in order to select the new parent of the node
         */

        for(i=0;i<neighbors;i++){

                /*
                 * Get current entry
                 */

                current_entry=&routing_table[i];

                /*
                 * Skip entries whose "address" field is not valid or is set to the the same ID as the one of the
                 * current node (in order to avoid loops)
                 */

                if((current_entry->info.parent==INVALID_ADDRESS) || (current_entry->info.parent==self))
                        continue;

                /*
                 * The entry currently selected has a valid "address" field.
                 * Contact the link estimator asking for the 1-hop ETX of the neighbor matching the current address
                 */

                current_one_hop_etx=get_one_hop_etx(current_entry->neighbor);

                /*
                 * Compute the ETX of the route through the current entry of the routing table: it's the sum of the
                 * ETX of the node and the 1-hop ETX to the node itself
                 */

                current_etx=current_one_hop_etx+current_entry->info.etx;

                /*
                 * Check whether the node analyzed is the actual parent of the node
                 */

                if(current_entry->neighbor==route.parent){

                        /*
                         * In case the node analyzed is the actual parent of the node, update the corresponding entry
                         * and the "route" variable
                         */

                        actual_etx=current_etx;
                        route.etx=current_entry->info.etx;
                        route.congested=current_entry->info.congested;

                        /*
                         * Jump to the next entry in the table to check if it's necessary to change the parent
                         */

                        continue;
                }

                /*
                 * Ignore congested links
                 */

                if(current_entry->info.congested)
                        continue;

                /*
                 * Ignore links whose 1-hop ETX is beyond MAX_ETX
                 */

                if(current_one_hop_etx>=MAX_ONE_HOP_ETX)
                        continue;

                /*
                 * If the ETX of the current entry is less than the lowest ETX found so far in the table, select the
                 * current entry as chosen candidate for the new parent; then jump to next entry
                 */

                if(current_etx<min_etx){
                        min_etx=current_etx;
                        best_entry=current_entry;
                }
        }

        /*
         * At this point, the variable "best_entry" points to the entry of the node whose ETX is the lowest among the
         * nodes in the routing table other than the actual parent of this node; "min_etx" points to the corresponding
         * value of ETX.
         *
         * Now it's time to choose between the candidate node and the actual parent: the former is chosen as new parent
         * if and only if the following hold:
         *
         * 1 - the candidate is not congested and the quality of the link to it is good
         * 2 - the current parent is congested and the new route passing through the candidate is at least as good as
         *     the previous one or the current parent is not congested but the new route has is better (in terms of ETX)
         *     by at least PARENT_SWITCH_THRESHOLD
         *
         * In order to avoid loops, the alternative parent is selected if it is not a descendant of the current parent:
         * if x is the value of the actual parent's ETX, the value of the new parent's ETX has to be less than x+10
         * because each descendant of the actual parent has a value of ETX at least x+10 (1 hop more)
         */

        /*
         * Check if an alternative potential parent has been found, i.e. if the minimum ETX found is less than the
         * initial value INFINITE_ETX
         */

        if(min_etx!=INFINITE_ETX){

                /*
                 * Check if the conditions to change the parent hold:
                 * 1 - there's no parent yet (currentETX=INFINITE_ETX)
                 * OR
                 * 2 - the current route is congested AND the new parent is not descendant of the actual parent
                 * OR
                 * 3 - the route passing through the new parent has an ETX which is at least PARENT_SWITCH_THRESHOLD
                 *     less than the route passing through the actual parent
                 */

                if(current_etx==INFINITE_ETX || (route.congested && (min_etx<(route.etx+10))) ||
                   (min_etx+PARENT_SWITCH_THRESHOLD<current_etx)){

                        /*
                         * It's time to change parent => the corresponding entry in the estimator table has to be
                         * unpinned before removal
                         */

                        unpin_neighbor(route.parent);

                        /*
                         * Now pin the entry corresponding to the new parent
                         */

                        pin_neighbor(best_entry->neighbor);

                        /*
                         * Clear data link quality information of the new parent in the estimator table
                         */

                        clear_data_link_quality(best_entry->neighbor);

                        /*
                         * Update the "route" variable with data of the new parent
                         * First set the ID of the new parent
                         */

                        route.parent=best_entry->neighbor;

                        /*
                         * Then set the corresponding ETX
                         */

                        route.etx=best_entry->info.etx;

                        /*
                         * Finally copy the "congested" flag from the new parent
                         */

                        route.congested=best_entry->info.congested;

                        /*
                         * If there's a difference of more tha two hops between the new route and the old one, reset
                         * the interval to schedule the sending of a new beacon
                         */

                        if(current_etx-min_etx>20)
                                reset_interval();
                }
        }


}

/*
 * NEW BEACON SENDING TIME
 *
 * Randomly choose a sending time for the next beacon in the interval [I_b/2 , I_b] and request a new event of type
 * SEND_BEACON at the new sending time: when the virtual time of the node will reach this value, a new beacon will be
 * sent.
 */

void set_beacon_sending_time(){

        /*
         * Get the upper bound for the beacon sending time
         */

        beacon_sending_time=current_interval;

        /*
         * Then get the lower bound for the beacon sending time
         */

        beacon_sending_time/=2;

        /*
         * Randomly choose a value less than I_b/2 and add it to I_b/2 so it is guaranteed that the new beacon
         * sending time lies in [I_b/2 , I_b]
         */

        beacon_sending_time+= ((unsigned long)Random())%beacon_sending_time;

        /*
         * We have just chosen a new sending time for the next beacon, so I_b has not elapsed yet
         */

        max_beacon_interval_elapsed=false;

        /*
         * Schedule the sending of the next beacon at the chosen sending time
         */

        ScheduleNewEvent(self,timestamp+beacon_sending_time,SEND_BEACON,NULL,0);
}



/*
 * RESET INTERVAL
 *
 * Set the value of I_b back to its minimum possible value and randomly choose a new sending time for next beacon
 */

void reset_interval(){

        /*
         * Restore minimum value the beacon interval (I_b)
         */

        current_interval=BEACON_MIN_INTERVAL;

        /*
         * Choose a sending time randomly
         */

        set_beacon_sending_time();
}

/*
 * SCHEDULE BEACONS INTERVAL UPDATE
 *
 * After a time equivalent to I_b has elapsed since I_b itself was set, its value is doubled, so that beacons are sent
 * with decreasing frequency => this function calculates the moment when the update will be necessary and requests an
 * event scheduled at that moment
 */

void schedule_beacons_interval_update(){

        /*
         * Calculate time left before the beacons interval has to be updated
         */

        unsigned long remaining=current_interval-beacon_sending_time;

        /*
         * Set the flag indicating that the beacons interval has to be updated
         */

        max_beacon_interval_elapsed=true;

        /*
         * Request an event scheduled at the time in the future when the update will have to be performed
         */

        ScheduleNewEvent(self,timestamp+remaining,SEND_BEACON,NULL,0);
}

/*
 * SEND BEACON
 *
 * Send a new beacon containing information about the route of the node
 */

void send_beacon(){

        /*
         * Get the pointer to the routing info within the beacon
         */

        routing_frame=&routing_packet.routing_frame;

        /*
         * Default value for the options flag is 0
         */

        routing_frame->options=0;

        /*
         * Ask the FORWARDING ENGINE whether the parent is congested: if so, set the corresponding flag in the routing
         * frame of the beacon being sent
         */

        if(is_congested())
                routing_frame->options|=CTP_CONGESTED;

        /*
         * Set the current parent in the corresponding field of the routing frame
         */

        routing_frame->parent=route.parent;

        /*
         * Check whether the node is the root of the tree: if so, only set the ETX field in the routing frame (it should
         * be 0)
         */

        if(is_root)
                routing_frame->ETX=route.etx;
        else if(route.parent==INVALID_ADDRESS){

                /*
                 * If the current route of the node does not have a valid parent, it asks the other neighbors to send
                 * him beacons in order for him to update its route => the flag PULL is set in the routing frame; also
                 * the etx is transmitted
                 */

                routing_frame->ETX=route.etx;
                routing_frame->options |= CTP_PULL;
        }
        else{

                /*
                 * If the route of this node is valid, the ETX written into the beacon is equal to the ETX of the route
                 * plus the 1-hop ETX to the parent
                 */

                routing_frame->ETX=route.etx+get_one_hop_etx(route.parent);
        }
        neighbor_evicted
        /*
         * At this point the beacon is ready to be sent as broadcast message. The routing layer relies on the link
         * estimator for this service
         */

        send_routing_packet(self,BROADCAST_ADDRESS,&routing_packet);
}

/*
 * RECEIVE BEACON
 *
 * This function is invoked by the LINK ESTIMATOR when a beacon is received from one of the neighbors => the ROUTING
 * TABLE has to be updated
 *
 * @routing_frame: the routing frame extracted from the beacon
 * @from: ID of the node that sent the message
 */

void receive_beacon(ctp_routing_frame* routing_frame, unsigned int from){

        /*
         * Flag indicating that the sender is congested
         */

        bool congested=routing_frame->options & CTP_CONGESTED;

        /*
         * Now update the ROUTING TABLE using the info contained in the routing layer frame
         * First check that the node has a valid route, i.e. a chosen parent, otherwise there's no point with updating
         * the ROUTING TABLE
         */

        if(routing_frame->parent!=INVALID_ADDRESS){

                /*
                 * The sender has a valid rouote.
                 * If the sender is the root node (ETX=0), force the LINK ESTIMATOR to insert the sender in the neighbor
                 * table and then to pin it (since it's the root node)
                 */

                if(!routing_frame->ETX){
                        insert_neighbor(from);
                        pin_neighbor(from);
                }

                /*
                 * Update the ROUTING TABLE using info contained in the beacon
                 */

                update_routing_table(from,routing_frame->parent,routing_frame->ETX);

                /*
                 * Update the "congested" flag to the value reported in the beacon.
                 * Possibly update the route
                 */

                update_neighbor_congested(from,congested);
        }

        /*
         * Check if the PULL flag is set in the routing layer frame: if this is the case, the sender has no route and
         * needs its neighbors to send beacons in order for him to update its routing table and thus being able to
         * choose a parent => the frequency of the beacons is reset to its maximum initial value, so that more beacons
         * are injected into the network and the sender has higher chances to select its parent
         */

        if(routing_frame->options & CTP_PULL)
                reset_interval();
}

/*
 * NEIGHBOR EVICTED
 *
 * This function is used by the LINK ESTIMATOR to signal the ROUTING ENGINE about the fact that one of its neighbors is
 * no longer reachable => the ROUTING TABLE has to be updated as a consequence and also the route has to be updated if
 * the neighbor was the parent of this node
 *
 * @address: ID of the ex-neighbor
 */

void neighbor_evicted(unsigned int address){

        /*
         * Evict the given neighbor from the routing table
         */

        remove_entry_routing_table(address);

        /*
         * Check if the evicted neighbor is the actual parent: if so, reset the route and ask for a route update
         */

        if(address==route.parent){
                init_route_info(&route);
                update_route();
        }
}

/*
 * SHOULD THE NEIGHBOR BE INSERTED?
 *
 * This function is invoked by the link estimator when a new neighbor has been detected but there's no space left in the
 * neighbor table to store a new entry => the link estimator asks the routing engine whether it's worth it to insert
 * the neighbor or not.
 *
 * This decision is taken by analyzing the routing layer frame of the beacon, forwarded by the LINK ESTIMATOR.
 * The routing engine returns true (insertion is advised) if:
 *
 * 1-the sender of the beacon is the root of the three (ETX=0) => obviously the current node should connect to it
 * 2-the sender of the beacon has a route whose ETX is better than the ETX of the route of at least one neighbor in the
 * ROUTING TABLE
 *
 * Otherwise it returns false (insertion is discouraged)
 */

bool is_neighbor_worth_inserting(ctp_routing_frame* routing_frame){

        /*
         * Return value: should the neighbor be inserted or not?
         */

        bool insert=false;

        /*
         * The ETX of the neighbor that sent the beacon
         */

        unsigned short neighbor_etx;

        /*
         * The ETX of the entries in the routing table
         */

        unsigned short etx;

        /*
         * Variable used to iterate through the entries of the ROUTING TABLE
         */

        unsigned char i;

        /*
         * Pointer to an entry of the ROUTING TABLE
         */

        routing_table_entry* entry;

        /*
         * Get the ETX claimed by the neighbor through the beacon
         */

        neighbor_etx=routing_frame->ETX;

        /*
         * Check every single entry of the ROUTING TABLE and for each check if the corresponding ETX is less than the one of the new
         * neighbor: if such an entry is found, stop the loop and return true
         */

        for(i=0;i<neighbors && !insert;i++){

                /*
                 * Get the current entry
                 */

                entry=&routing_table[i];

                /*
                 * Skip the parent of this node, because it can't be removed
                 */

                if(entry->neighbor=route.parent)
                        continue;

                /*
                 * Get the ETX of the curren entry
                 */

                etx=entry->info.etx;

                /*
                 * If the ETX of the new neighbor is less than the one of the current entry, set found to true, thus
                 * stopping the loop, otherwise keep scanning entries
                 */

                insert |= (neighbor_etx<etx);
        }

        /*
         * Return true if the neighbor should be inserted in the estimator table, false otherwise
         */

        return insert;
}

/*
 * GET ETX
 *
 * Function invoked by the forwarding engine to get the ETX of the current route of the node.
 *
 * @etx: pointer to the variable declared by the forwarding engine that has to be initialized to the value ETX of the
 * current route
 *
 * Returns true if the node has a valid parent, false if not or if the provided pointer is NULL
 */

bool get_etx(unsigned short* etx){

        /*
         * Return false if the given pointer is not valid or if the node hasn't a valid parent
         */

        if(!etx || route.parent==INVALID_ADDRESS)
                return false;

        /*
         * The node has a valid parent.
         * Check if it's the root node
         */

        if(is_root)

                /*
                 * This node is the root of the collection tree => its ETX is 0 by definition
                 */

                *etx=0;

        else
                /*
                 * The ETX of the node is equal to the 1-hop ETX of the link to the parent plus the ETX of the parent
                 * itself
                 */

                *etx=route.etx+get_one_hop_etx(route.parent);

        /*
         * The ETX has been successfully provided => return true
         */

        return true;
}

/*
 * GET PARENT
 *
 * Function invoked by the forwarding engine to get the ID and coordinates of the parent in the current route.
 * The routing engine can only provide the ID of the parent, but as regards with the coordinates of the parent, on its
 * turn it asks the link estimator
 */

node get_parent(){

        /*
         * The "node" object returned
         */

        node parent;

        /*
         * Get the parent ID
         */

        parent.ID=route.parent;

        /*
         * Ask the link estimator for coordinates of the parent
         */

        parent.coordinates=get_parent_coordinates(parent.ID);
}