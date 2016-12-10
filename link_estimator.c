/*
 * LINK ESTIMATOR
 *
 * This layer determines the quality of the communications links of the nodes: in particular, the quality is evaluated
 * in terms of 1-hop ETX.
 *
 * The 1-hop ETX is computed from the number of beacons received and the number of successfully transmitted data packets
 * The ingoing quality of the link is calculated as the ratio between the number of beacons sent by the neighbor and
 * the number of beacons actually received. The outgoing quality is calculated as the ratio between the number the
 * number of data packets sent to the neighbor and the number of acks received from him.
 *
 * The ROUTING ENGINE relies on the layer provided by the LINK ESTIMATOR in order to send routing beacons. On the other
 * side, when a node receives a beacon, the information contained are used to update the link estimator table: this
 * table contains a list of the neighbors of a node along with information like the 1-hop ETX. This table is created
 * and updated by the link estimator layer.
 */

#include "link_estimator.h"
#include "application.h"
#include "routing_engine.h"

extern node me;

/*
 * LINK ESTIMATOR TABLE
 *
 * An array of link_estimator_table_entry with a number of NEIGHBOR_TABLE_SIZE
 * elements
 */

link_estimator_table_entry link_estimator_table[NEIGHBOR_TABLE_SIZE];

/*
 * BEACON SEQUENCE NUMBER
 *
 * Sequence number of the beacon, incremented by one at every beacon transmission.
 * By counting the number of beacons received from a neighbor and comparing this with the sequence number reported in
 * the packet, one can determine if, and how many if so, beacons from that neighbor have been lost => this provides an
 * estimate of the ingoing quality of the link to that neighbor
 */

unsigned char beacon_sequence=0;

/*
 * LINK ESTIMATOR TABLE OPERATIONS - start
 */

/*
 * SET TABLE ENTRY
 *
 * Initialize the entry in the estimator table for the neighbor with the given ID at the given position
 *
 * @neighbor: ID and coordinates of the neighbor the entry is being created for
 * @index: position occupied by the new entry in the table
 */

void init_estimator_entry(node neighbor, unsigned char index){

        /*
         * Pointer to the index-th element of the table
         */

        link_estimator_table_entry* new_entry;

        /*
         * Set pointer to the index-th element of the table
         */

        new_entry=&link_estimator_table[index];

        /*
         * Set the neighbor field to the node instance of the given neighbor
         */

        new_entry->neighbor=neighbor;

        /*
         * Initialize fields of the entry
         */

        new_entry->lastseq=0;
        new_entry->beacons_received=0;
        new_entry->beacons_missed=0;
        new_entry->data_acknowledged=0;
        new_entry->data_sent=0;
        new_entry->ingoing_quality=0;
        new_entry->one_hop_etx=0;

        /*
         * Set VALID and INIT entry flags
         */

        new_entry->flags=INIT_ENTRY|VALID_ENTRY;
}

/*
 * FIND NEIGHBOR INDEX
 *
 * Returns the index of the entry in the neighbor table corresponding to the given ID
 *
 * @neighbor: ID of the neighbor the entry corresponds to
 *
 * Returns the index if it's found, INVALID_ENTRY otherwise
 */

unsigned char find_estimator_entry(unsigned int neighbor){

        /*
         * Return value: index of the entry
         */

        unsigned char index;

        /*
         * Look for all the VALID entries of the neighbor table and stop when one whose ID fields corresponds to the
         * provided ID is found
         */

        for(index=0;index<NEIGHBOR_TABLE_SIZE;index++){

                /*
                 * Only check entries with VALID flag set
                 */

                if(link_estimator_table[index].flags & VALID_ENTRY){

                        /*
                         * Check if the current entry is the one we are looking for
                         */

                        if(link_estimator_table[index].neighbor.ID==neighbor) {

                                /*
                                 * Return the index of the entry corresponding to the neighbor
                                 */

                                return index;
                        }
                }
        }

        /*
         * We get here if none of the entries in the table matches the given ID -> return INVALID_ENTRY
         */

        return INVALID_ENTRY;
}

/*
 * FIND WORST ENTRY
 *
 * Returns the index of the entry in the table with the highest ETX which is higher than or equal to the given threshold
 *
 * @etx_threshold: threshold for the value of ETX
 *
 * If no entry is found, INVALID_ENTRY is returned
 */

unsigned char find_estimator_worst_entry(unsigned char etx_threshold){

        /*
         * Index used to scan the estimator table
         */

        unsigned char i;

        /*
         * Return value: the index of the entry with the highest ETX beyond the given threshold
         */

        unsigned char worst_entry_index;

        /*
         * Highest value of ETX found while scanning the estimator table
         */

        unsigned char highest_etx;

        /*
         * ETX of the current entry while scanning the estimator table
         */

        unsigned char current_etx;

        /*
         * Highest value for ETX before start scanning is 0 (root node)
         */

        highest_etx=0;

        /*
         * Value returned in case no entry is found
         */

        worst_entry_index=INVALID_ENTRY;

        /*
         * Scan the estimator table looking for the entry with the highest ETX beyond the given threshold
         */

        for(i=0;i<NEIGHBOR_TABLE_SIZE;i++){

                /*
                 * Check whether the current entry is VALID: if not, jump to the next
                 */

                if(!(link_estimator_table[i].flags & VALID_ENTRY))
                        continue;

                /*
                 * Check whether the current entry is MATURE: if not, jump to the next
                 */

                if(!(link_estimator_table[i].flags & MATURE_ENTRY))
                        continue;

                /*
                 * Check whether the current entry is PINNED: if so, jump to the next
                 */

                if(link_estimator_table[i].flags & PINNED_ENTRY)
                        continue;

                /*
                 * The current is VALID, MATURE and NOT PINNED => check the value of its ETX
                 */

                current_etx=link_estimator_table[i].one_hop_etx;

                /*
                 * If the ETX of the current entry is bigger than the ETX found so far, keep track of its index and
                 * update the value for the highest ETX found so far
                 */

                if(current_etx>=highest_etx){
                        worst_entry_index=i;
                        highest_etx=current_etx;
                }
        }

        /*
         * The whole estimator table has been scanned and the highest value of ETX has been found: if this is higher
         * than or equal to the given threshold, return the index of the corresponding entry, otherwise return
         * INVALID_ENTRY
         */

        if(highest_etx>=etx_threshold)
                return worst_entry_index;
        else
                return INVALID_ENTRY;
}

/*
 * FIND RANDOM ENTRY
 *
 * Returns the index of a random entry that is VALID and NOT PINNED; if such an entry can't be found, INVALID_ENTRY is
 * returned
 */

unsigned char find_random_entry(){

        /*
         * Index used to scan the estimator table
         */

        unsigned char i;

        /*
         * Counter used to select a random entry
         */

        unsigned char counter;

        /*
         * Number of entries that can be selected, i.e. VALID AND NOT PINNED NOR MATURE
         */

        unsigned char candidates;

        /*
         * Scan the estimator table to count the number of candidates
         */

        for(i=0;i<NEIGHBOR_TABLE_SIZE;i++){

                /*
                 * The entry is a candidate if it's VALID
                 */

                if(link_estimator_table[i].flags & VALID_ENTRY){

                        /*
                         * If the entry has MATURE or PINNED flags set, is not a candidate
                         */

                        if((link_estimator_table[i].flags & MATURE_ENTRY)||(link_estimator_table[i].flags & PINNED_ENTRY)){

                        }
                        else{

                                /*
                                 * Otherwise, it can be randomly selected
                                 */

                                candidates++;

                        }

                }
        }

        /*
         * Check if at least one entry is eligible: if not, return INVALID_ENTRY
         */

        if(!candidates)
                return INVALID_ENTRY;

        /*
         * At least one entry is eligible => get the value of counter, which is used to randomly select an entry: it
         * is the result of modulo divion of a random value by the number of candidates
         */

        counter= ((unsigned char)Random()) % candidates;

        /*
         * The entry selected is the "counter-th" entry which is VALID AND NOT PINNED NOR MATURE
         */

        for(i=0;i<NEIGHBOR_TABLE_SIZE;i++){

                /*
                 * Discard invalid entries
                 */

                if(!(link_estimator_table[i].flags & VALID_ENTRY))
                        continue;

                /*
                 * Discard MATURE AND NOT PINNED entries
                 */

                if((link_estimator_table[i].flags & MATURE_ENTRY)||(link_estimator_table[i].flags & PINNED_ENTRY)){
                        continue;
                }

                /*
                 * The current entry is VALID AND NOT PINNED NOR MATURE => if it's the "counter-th" entry of the table
                 * with such characteristics, return its index, otherwise iterate
                 */

                if(counter--==0)
                        return i;
        }

        /*
         * If no entry is eligible, return INVALID_ENTRY
         */

        return INVALID_ENTRY;
}

/*
 * API FUNCTIONS - start
 *
 * Functions exposed by the link estimator, available to the Routing and Forwarding Engine
 */

/*
 * GET 1-HOP ETX
 *
 * This function is invoked to ask the link estimator for the 1-hop ETX of the node with given ID; this is a query
 * to the neighbor table
 *
 * @address: the ID of the neighbor whose 1-hop ETX is queried
 *
 * Returns the 1-hop ETX of the neighbor; if the ID doesn't match any entry in the estimator table; in case the given
 * address does not match any entry, VERY_LARGE_ETX_VALUE is returned as error code and the same holds if the entry has
 * not the MATURE flag set
 */

unsigned short get_one_hop_etx(unsigned int address){

        /*
         * Index of the entry in the estimator table that matches the entry (if any)
         */

        unsigned char index;

        /*
         * Search the estimator table for an entry with the given address
         */

        index=find_estimator_entry(address);

        /*
         * Check result of the query
         */

        if(index==INVALID_ENTRY){

                /*
                 * The query failed because no neighbor with given address has been found => return VERY_LARGE_ETX_VALUE
                 */

                return VERY_LARGE_ETX_VALUE;
        }

        /*
         * The given ID corresponds to a valid neighbor.
         * Check if the corresponding entry is MATURE
         */

        if(link_estimator_table[index].flags & MATURE_ENTRY){

                /*
                 * The entry is not being initialized => return its 1-hop TX
                 */

                return link_estimator_table[index].one_hop_etx;
        }

        /*
         * The entry is NOT MATURE => return VERY_LARGE_ETX_VALUE
         */

        return VERY_LARGE_ETX_VALUE;
}

/*
 * GET PARENT COORDINATES
 *
 * Function used by the routing engine to get the coordinates of the neighbor that is currently selected as parent of
 * the current node
 *
 * @parent: ID of the current parent
 */

node_coordinates get_parent_coordinates(unsigned int parent){

        /*
         * Index of the entry in the estimator table corresponding to the parent
         */

        unsigned char index;

        /*
         * Get the index
         */

        index=find_estimator_entry(parent);

        /*
         * Check that the entry matching the given parent ID exists
         */

        if(index==INVALID_ADDRESS)

                /*
                 * The given ID is unknown to the link estimator => return NULL
                 */

                return NULL;

        /*
         * The given ID has a corresponding entry in the estimator table => return the value of the coordinates
         */

        return link_estimator_table[index].neighbor.coordinates;
}

/*
 * UNPIN NEIGHBOR
 *
 * Clear the PINNED flag of the entry in the estimator table corresponding to the given ID
 *
 * @address: the ID of the node whose entry has to be unpinned
 *
 * Returns true if the entry is found and unpinned, false if otherwise something goes wrong
 */

bool unpin_neighbor(unsigned int address){

        /*
         * The index of the entry corresponding to the given ID
         */

        unsigned char index;

        /*
         * Find the index
         */

        index=find_estimator_entry(address);

        /*
         * Check whether the entry has been found or not
         */

        if(index==INVALID_ENTRY){

                /*
                 * No entry in the neighbor table is referred to given node => return false
                 */

                return false;
        }

        /*
         * The entry has been found => unpin it
         */

        link_estimator_table[index].flags&=~PINNED_ENTRY;

        /*
         * Unpinning was successful
         */

        return true;

}

/*
 * PIN NEIGHBOR
 *
 * Set the PINNED flag of the entry in the estimator table corresponding to the given ID
 *
 * @address: the ID of the node whose entry has to be unpinned
 *
 * Returns true if the entry is found and pinned, false if otherwise something goes wrong
 */

bool pin_neighbor(unsigned int address){

        /*
         * The index of the entry corresponding to the given ID
         */

        unsigned char index;

        /*
         * Find the index
         */

        index=find_estimator_entry(address);

        /*
         * Check whether the entry has been found or not
         */

        if(index==INVALID_ENTRY){

                /*
                 * No entry in the neighbor table is referred to given node => return false
                 */

                return false;
        }

        /*
         * The entry has been found => pin it
         */

        link_estimator_table[index].flags|=PINNED_ENTRY;

        /*
         * Pinning was successful
         */

        return true;

}

/*
 * CLEAR DATA QUALITY
 *
 * When a new neighbor is selected as parent, the info related to the number of data packets sent to and acknowledged
 * by him has to cleared
 *
 * @address: the ID of the node selected as new parent
 *
 * Returns true if the info related to link quality of the entry corresponding to the new parent are cleared, false if
 * otherwise something goes wrong
 */

bool clear_data_link_quality(unsigned int address){

        /*
         * The index of the entry in the neighbor table corresponding to the new parent
         */

        unsigned char index;

        /*
         * Find the index
         */

        index=find_estimator_entry(address);

        /*
         * Check whether the entry has been found or not
         */

        if(index==INVALID_ENTRY){

                /*
                 * No entry in the neighbor table is referred to given node => return false
                 */

                return false;
        }

        /*
         * Clear the info about data sent to the new parent in the corresponding entry
         */

        link_estimator_table[index].data_sent=0;

        /*
         * Clear the info about data acknowledged by the new parent in the corresponding entry
         */

        link_estimator_table[index].data_acknowledged=0;

        /*
         * Info related to data link quality have been successfully cleared => return success
         */

        return true;
}

/*
 * SEND ROUTING PACKET
 *
 * The function invoked by the ROUTING ENGINE when a beacon has to be broadcasted to all the nodes in the tree.
 * Set the fields of the link estimator frame and combine this with the given routing frame => the resulting packet is
 * sent to the other nodes by scheduling a new simulator event.
 *
 * @src: ID of the current node, that sends the beacon
 * @dst: th ID of the recipient node or BROADCAST_ADDRESS in case of broadcast messages
 * @routing_frame: pointer to the routing frame, initialized by the routing engine, of the routing packet to send
 */

void send_routing_packet(unsigned int src,unsigned int dst,ctp_routing_packet* beacon){

        /*
         * Pointer to the physical and data link frame of the beacon being sent
         */

        physical_datalink_overhead* phy_mac_overhead;

        /*
         * Pointer to the link estimator frame of the beacon being sent
         */

        ctp_link_estimator_frame* link_estimator_frame;

        /*
         * Index of the entry in the estimator table corresponding to the recipient of the packet
         */

        unsigned char index;

        /*
         * Get the index of the entry
         */

        index=find_estimator_entry(dst);

        /*
         * Check that the recipient is know to the link estimator
         */

        if(index==INVALID_ENTRY)

                /*
                 * No entry in the neoghbor table matches the recipient => no beacon can be sent to him
                 */

                return;

        /*
         * Extract the physical and data link frame from the given beacon
         */

        phy_mac_overhead=&beacon->phy_mac_overhead;

        /*
         * Set the fields of the physical and data link layer, starting from the source of the packet: the ID and the
         * coordinates of this node
         */

        phy_mac_overhead->src=me;

        /*
         * Then set the ID and coordinates of the destination node with corresponding values from the entry in the
         * estimator table
         */

        phy_mac_overhead->dst=link_estimator_table[index].neighbor;

        /*
         * Extract the link estimator frame from the given beacon
         */

        link_estimator_frame=&beacon->link_estimator_frame;

        /*
         * Now store the sequence number of this beacon in the corresponding field of the link estimator frame and then
         * increment the sequence number itself
         */

        link_estimator_frame->seq=beacon_sequence++;

        /*
         * At this point the beacon is well formed, so it's time to send it to the specified destination => schedule a
         * new event
         */

        broadcast_event(&beacon,BEACON_PACKET);
}

/*
 * API FUNCTIONS - end
 */

/*
 * FIND FREE ENTRY
 *
 * Returns the index of the free first entry in the estimator table, if one exists, INVALID_ENTRY otherwise
 */

unsigned char find_estimator_free_entry(){

        /*
         * Return value: index of the entry
         */

        unsigned char index;

        /*
         * Look for all the entries of the neighbor table and stop when one is not valid
         */

        for(index=0;index<NEIGHBOR_TABLE_SIZE;index++){

                /*
                 * Skip valid entries
                 */

                if(link_estimator_table[index].flags & VALID_ENTRY){

                }

                        /*
                         * If an entry is not marked as VALID, it can be re-used
                         */

                else{
                        return index;
                }
        }

        /*
         * We get here if none of the entries in the table is free -> return INVALID_ENTRY
         */

        return INVALID_ENTRY;
}

/*
 * LINK ESTIMATOR TABLE OPERATIONS - end
 */

/*
 * COMPUTE ETX OF A NEIGHBOR
 *
 * This function is used to compute the value of ETX to be stored in the corresponding field of an entry of the
 * estimator table, in case the ETX the ingoing quality of a link gets updated.
 * In particular, this function returns ten times the value of the actual ETX value.
 * This is needed because the function "updateETX" works on values scaled by 10, in order to avoid float variables yet
 * not losing too much precision due to integer division.
 *
 * @new_quality: new estimation of the ingoing link quality
 *
 * Returns the value of ETX of a neighbor, multiplied by 10
 */

unsigned short compute_ETX(unsigned char new_quality){

        /*
         * Final value of etx, to be stored in the entry of the estimator table
         */

        unsigned short etx;

        /*
         * Check if the new value for the link quality is not zero: if so, return VERY_LARGE_ETX_VALUE (see definition)
         */

        if(new_quality<=0)
                return VERY_LARGE_ETX_VALUE;
        else{

                /*
                 * The computed ingoing quality of the link is not null
                 */

                etx=2500/new_quality;
                if(etx>250){

                        /*
                         * The computed quality (scaled by 250) is less than 0,04, namely
                         *
                         * beacons_received/total_beacons<0,04
                         *
                         * less than 1 out 25 beacons is received => the ingoing quality is low => return VERY_LARGE_ETX
                         * so that this neighbor won't be involved in the collection of information
                         */

                        etx=VERY_LARGE_ETX_VALUE;

                }

                /*
                 * Return 10*ETX
                 */

                return etx;

        }
}

/*
 * UPDATE ETX OF A NEIGHBOR
 *
 * When a new estimation of the outgoing quality (sending data packets) or the ingoing quality (sending beacons)
 * is performed, the ETX is recomputed.
 * IMPORTANT NOTE: both the ingoing and outgoing quality parameters are scaled by 10 in order not too lose too much
 * precision when due to integer divisions, so the final value computed for ETX has to be "rescaled" dividing by 10
 *
 * @table_entry: pointer to the entry in the table corresponding to the ETX to be updated
 * @new_quality: new estimation of the link quality
 */

void update_ETX(link_estimator_table_entry* entry,unsigned short new_quality){
        entry->one_hop_etx=(ALPHA * entry->one_hop_etx + (10 - ALPHA) * new_quality)/10;
}

/*
 * UPDATE OUTGOING LINK QUALITY
 *
 * Set the outgoing quality of the link to a specific neighbor to the number of packets sent to the neighbor over
 * the number of acks received.
 * If no ack is received at all, set the outgoing quality to the number of failed deliveries since the last successful
 * deliver => since the counters of sent packets and acknowledged packets are set to 0 after DLQ_PKT_WINDOW packets have
 * been sent, the value for outgoing link quality in case no ack is received can be at most DLQ_PKT_WINDOW
 *
 * @table_entry: pointer to the entry in the table corresponding to the ETX to be updated
 */

void update_outgoing_quality(link_estimator_table_entry* entry){

        /*
         * Updated value of the outgoing quality of the link to the neighbor
         */

        unsigned short new_outgoing_quality;

        /*
         * If no packet was acknowledged, set the new estimation to the total number of packets sent times 10
         * (multiplication by 10 is necessary to be coherent with the values of outgoing quality when the number
         * of acknowledgments is not null (see below))
         */

        if(!entry->data_acknowledged){
                new_outgoing_quality=entry->data_sent*10;
        }

                /*
                 * Otherwise, set the new value for the quality to the ratio of the packets sent over the number of
                 * packets acknowledged.
                 * Since both "data_sent" and "data_acknowledged" are defined as integers, in order not to lose too much
                 * precision due to truncation by the integer division, the numerator is scaled by 10
                 */

        else{
                new_outgoing_quality=(10*entry->data_sent)/entry->data_acknowledged;

                /*
                 * Reset the counters related to data packets
                 */

                entry->data_acknowledged=0;
                entry->data_sent=0;
        }

        /*
         * Recompute the ETX of the link to the neighbor
         */

        update_ETX(entry,new_outgoing_quality);
}

/*
 * A beacon may not include all the entries of the estimator table => we keep track to the index of the index of the
 * last entry transmitted through the beacon, so it's possible to send all the entries soon or later, applying a round
 * robin algorithm
 */

//unsigned char last_index=0;

/*
 * UPDATE INGOING LINK QUALITY
 *
 * After BLQ_PKT_WINDOW beacons have been received from a neighbor, the value for the ingoing quality of the
 * corresponding link is updated.
 * Its value is equal to the number of beacons received from a neighbor over the number of beacons broadcasted by it.
 * Before being used to compute the 1-hop ETX, the value for ingoing quality is passed through ax exponential smoothing
 * filter: this filter averages the new value of ingoing quality and those computed in the past, but weighting the
 * latter according to an exponentially decaying function.
 *
 * @neighbor: ID of the neighbor node whose entry in the estimator table needs to be updated
 */

void update_ingoing_quality(unsigned int neighbor){

        /*
         * Index to iterate through the entries of the table
         */

        unsigned char i;

        /*
         * Number of beacons received from the neighbor
         */

        unsigned char total_beacons;

        /*
         * Pointer to the entry associated to the neighbor
         */

        link_estimator_table_entry* entry;

        /*
         * Updated value for the ingoing quality
         */

        unsigned char new_ingoing_quality;

        /*
         * Scan the estimator table looking for the entry corresponding to the ID of the neighbor
         */

        for(i=0;i<NEIGHBOR_TABLE_SIZE;i++){

                /*
                 * Get the current entry
                 */

                entry=&link_estimator_table[i];

                /*
                 * Check if the current entry corresponds to the neighbor
                 */

                if(entry->neighbor.ID==neighbor){

                        /*
                         * Entry corresponding to the neighbor found
                         * Check if the entry is VALID
                         */

                        if(entry->flags & VALID_ENTRY){

                                /*
                                 * The entry is valid
                                 * Get the total number of beacons sent by the neighbor so far
                                 */

                                total_beacons=entry->beacons_missed+entry->beacons_received;

                                /*
                                 * Check if the flag MATURE is set (i.e. if the total number of beacons received by the
                                 * neighbor was bigger than BLQ_PKT_WINDOW before this function was invoked): if not,
                                 * this means the ingoing quality has not been evaluated yet, then do this now; also
                                 * compute the first value for 1-hop ETX
                                 */

                                if(!(entry->flags & MATURE_ENTRY)){

                                        /*
                                         * The entry is NOT MATURE => update the value for ingoing link quality.
                                         * This values is equal to the number of beacons received over the number of
                                         * beacons sent by the neighbor; also the value is scaled by 250 in order not
                                         * to lose too much precision because of truncation of integer division
                                         */

                                        new_ingoing_quality=(250UL * entry->beacons_received) / total_beacons;

                                        /*
                                         * Update the corresponding field in the entry
                                         */

                                        entry->ingoing_quality=new_ingoing_quality;

                                        /*
                                         * Compute the first value for ETX of the neighbor
                                         */

                                        entry->one_hop_etx=compute_ETX(new_ingoing_quality);
                                }

                                /*
                                 * Set the flag MATURE in the entry (even if already set)
                                 */

                                entry->flags |=MATURE_ENTRY;

                                /*
                                 * When we get here, we can be sure that the entry of the neighbor has some values
                                 * for ingoing quality and ETX.
                                 * First compute the new value for ingoing quality
                                 */

                                new_ingoing_quality=(250UL * entry->beacons_received) / total_beacons;

                                /*
                                 * Filter the new value of ingoing quality and store the result in the corresponding
                                 * field of the entry
                                 */

                                entry->ingoing_quality=(ALPHA*entry->ingoing_quality+(10-ALPHA)*new_ingoing_quality)/10;

                                /*
                                 * Reset counters for received and missed beacons
                                 */

                                entry->beacons_received=0;
                                entry->beacons_missed=0;

                                /*
                                 * Update the value of ETX for this entry with the new value for ingoing quality
                                 */

                                update_ETX(entry,entry->ingoing_quality);
                        }
                }
        }
}

/*
 * UPDATE NEIGHBOR ENTRY
 *
 * When a message is received, this function is called in order to update the entry in the estimator table associated
 * to the sender of the message
 *
 * @index: index of the entry corresponding to the neighbor that sent the message
 * @seq: sequence number of the message received
 */

void update_neighbor_entry(unsigned char index, unsigned char seq){

        /*
         * The number of beacons sent by the neighbor that have been lost: this is the difference between the sequence
         * number written on the last message received and the sequence number stored in the entry in the table
         */

        unsigned char lost_beacons;

        /*
         * Check if the entry of the neighbor has the "INIT" flag set: if so, clear the flag because this is not the
         * first message from the neighbor; in fact, there's an entry dedicated to him in the estimator table
         */

        if(link_estimator_table[index].flags & INIT_ENTRY){

                /*
                 * Clear the flag
                 */

                link_estimator_table[index].flags&=~INIT_ENTRY;
        }

        /*
         * Get number of lost beacons from neighbor
         */

        lost_beacons=seq-link_estimator_table[index].lastseq;

        /*
         * Update the entry of the neighbor with the last sequence number
         */

        link_estimator_table[index].lastseq=seq;

        /*
         * Increment the counter for beacons received from the neighbor
         */

        link_estimator_table[index].beacons_received++;

        /*
         * If some beacons have been lost, update the dedicated counter
         */

        if(lost_beacons)
                link_estimator_table[index].beacons_missed+=lost_beacons-1;

        /*
         * If the number of lost beacons is bigger than MAX_PKT_GAP, the entry has to be re-initialized
         */

        if(lost_beacons>MAX_PKT_GAP){

                /*
                 * Reinitialize entry
                 */

                init_estimator_entry(link_estimator_table[index].neighbor,index);

                /*
                 * Update the sequence number and counter of beacons received of re-created entry
                 */

                link_estimator_table[index].lastseq=seq;
                link_estimator_table[index].beacons_received=1;
        }
        else{

                /*
                 * The number of lost beacons from the neighbor is below the limit.
                 * If the number of beacons received from the neighbor since the last update or the number of lost
                 * beacons are bigger than or equal to BLQ_PKT_WINDOW, the ETX of the neighbor has to be recomputed
                 */

                if((link_estimator_table[index].beacons_missed+link_estimator_table[index].beacons_received>=BLQ_PKT_WINDOW)
                   || (lost_beacons>=BLQ_PKT_WINDOW))
                        update_ingoing_quality(link_estimator_table[index].neighbor.ID);

        }
}

/*
 * PROCESS RECEIVED BEACON
 *
 * When a beacon is received by a node, its neighbor table has to be updated
 *
 * @physical_link_frame: pointer to the physical layer frame of the beacon received by the node
 * @link_estimator_frame: pointer to the link estimator frame of the beacon received by the node
 */

void process_received_beacon(physical_frame* physical_link_frame,ctp_link_estimator_frame* link_estimator_frame){

        /*
         * Index of the entry corresponding to the neighbor that sent the message: if no message had ever been received
         * by the sender, such an index will not be found and a new entry has to be created
         */

        unsigned char index;

        /*
         * ID and coordinates of the sender
         */

        node sender;

        /*
         * Check if it is a broadcast message or a unicast message
         */

        if(physical_link_frame->dst.ID==BROADCAST_ADDRESS){

                /*
                 * It's a broadcast message; get the identity of the sender
                 */

                sender=physical_link_frame->src;

                /*
                 * Get the index of the entry in the estimator table that matches the identity of the sender
                 */

                index=find_estimator_entry(sender.ID);

                /*
                 * Check a matching index has been found
                 */

                if(index!=INVALID_ENTRY){

                        /*
                         * The entry corresponding to the sender has been found => update it with the sequence number
                         * of the link estimator frame
                         */

                        update_neighbor_entry(index,link_estimator_frame->seq);
                }
                else{

                        /*
                         * No entry corresponding to the sender has been found => this is the first message received by
                         * him and a corresponding entry has to be created => look for a free entry in the neighbor
                         * table
                         */

                        index=find_estimator_free_entry();

                        /*
                         * Check whether an empty entry exists in the neighbor table
                         */

                        if(index!=INVALID_ENTRY){

                                /*
                                 * A free space to create a new entry in the estimator table has been found and its
                                 * index is stored in the variable index.
                                 * Now initialise the entry
                                 */

                                init_estimator_entry(sender,index);

                                /*
                                 * Get the sequence number of the packet from the link estimator frame and set the
                                 * corresponding field in the new entry; this is necessary because of the later
                                 * invocation of "update_neighbor_entry", which check if some beacons from a
                                 * neighbor have been lost
                                 */

                                link_estimator_table[index].lastseq=link_estimator_frame->seq;

                                /*
                                 * Set the other fields of the newly created entry of the table
                                 */

                                update_neighbor_entry(index,link_estimator_frame->seq);
                        }
                        else{

                                /*
                                 * No space left in the neighbor table to create a new entry => we have to replace the
                                 * entry of the neighbor with the highest EXT, which is unlikely to be selected to
                                 * forward data to the root of the collection tree => find such an entry
                                 */

                                index=find_estimator_worst_entry(EVICT_ETX_THRESHOLD);

                                /*
                                 * Check whether an entry to be evicted has been found
                                 */

                                if(index!=INVALID_ENTRY){

                                        /*
                                         * A victim entry has been found => evict it by notifying the event to the
                                         * ROUTING ENGINE (so it can update the ROUTING TABLE) and re-initializing the
                                         * entry in the estimator table with the ID of the node to be added
                                         */

                                        neighbor_evicted(link_estimator_table[index].neighbor.ID);
                                        init_estimator_entry(sender,index);
                                }
                                else
                                {

                                        /*
                                         * No entry in the neighbor table has an ETX higher than the threshold chosen
                                         * for the eviction of neighbors.
                                         *
                                         * In the original implementation of the CTP, at this point the link estimator
                                         * would ask the PHYSICAL LAYER about the so called "white bit", namely it would
                                         * ask whether the channel to the sender has a high quality: if so the beacon
                                         * would be further processed, otherwise it would be dropped.
                                         *
                                         * This model ignores the PHYSICAL LAYER, so the white bit is not considered and
                                         * a beacon is further processed.
                                         *
                                         * In particular, the link estimator asks the ROUTING LAYER whether the sender
                                         * of the beacon should be inserted in the estimator table or not: if so, a
                                         * random entry is replaced with the entry for the new neighbor
                                         */



                                        index=find_random_entry();

                                        /*
                                         * Check if an entry VALID AND NOT PINNED NOR MATURE exists
                                         */

                                        if(index!=INVALID_ENTRY){

                                                //TODO evict

                                                /*
                                                 * Initialize the new entry
                                                 */

                                                init_estimator_entry(sender,index);
                                        }

                                }

                        }

                }
        }
}

/*
 * RECEIVE BEACON
 *
 * When an event with type BEACON_PACKET is delivered to the node (logic process), this function is invoked to process
 * the received message. The LINK estimator is in charge of extracting the physical and link estimator frames from the
 * message and use the contained info to keep the neighbor table up to date. Then message is then passed to the upper
 * layer of the CTP stack, the ROUTINE ENGINE, to be further processed
 *
 * @message: the payload from the content of the event delivered to the node
 */

void receive_routing_packet(void* message){

        /*
         * Parse the buffer received to a routing packet (beacon)
         */

        ctp_routing_packet beacon=*((ctp_routing_packet*)message);

        /*
         * Extract the physical and the link estimator frames from the beacon and process it at this level
         * (LINK ESTIMATOR)
         */

        process_received_beacon(&beacon.physical_link_frame,&beacon.link_estimator_frame);

        /*
         * Then extract the routing layer frame and pass it to the ROUTING ENGINE
         */

        receive_beacon(&beacon.routing_frame,beacon.physical_link_frame.src.ID);
}

/*
 * ACKNOWLEDGMENT NOT RECEIVED
 *
 * Function called by the FORWARDING ENGINE to signal that the intended recipient of a data packet sent did not send the
 * corresponding acknowledgment => as a consequence, the outgoing link quality between the current node and the recipient
 * has to be re-computed.
 *
 * @recipient: ID of the intended recipient
 */

void ack_not_received(unsigned int recipient){

        /*
         * Pointer to the entry of the table corresponding to the given recipient
         */

        link_estimator_table_entry* entry;

        /*
         * Index of the above entry
         */

        unsigned char index;

        /*
         * Get the index
         */

        index=find_estimator_entry(recipient);

        /*
         * Check if the given ID has a corresponding entry in the neighbor table: go ahead only if so
         */

        if(index!=INVALID_ENTRY){

                /*
                 * There's an entry mathing the given ID => get it
                 */

                entry=&link_estimator_table[index];

                /*
                 * Update the counter of the data packet sent to this neighbor
                 */

                entry->data_sent++;

                /*
                 * Check whether the number of data packets sent to the neighbor has reached the bound that triggers an
                 * update of the outgoing link quality
                 */

                if(entry->data_sent>=DLQ_PKT_WINDOW)

                        /*
                         * The time has come to update the outgoing link quality of the link between the current node
                         * and the one corresponding to the given ID
                         */

                        update_outgoing_quality(entry);
        }
}

