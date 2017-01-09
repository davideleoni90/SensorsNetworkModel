/*
 * FORWARDING ENGINE
 *
 * Its main task is forwarding data packets received from neighbors as well as sending packets created by the node
 * itself. Moreover, it is in charge of detect duplicate packets and routing loops. Finally, it snoops data packets
 * directed to other nodes
 *
 * The forwarding engine has a FIFO queue of fixed depth at its disposal to store packets before forwarding them: here
 * packets are both those coming from other neighbors and those created by the node itself.
 * TODO we allow only one data packet at a time
 *
 * The forwarding engine waits for an acknowledgment for each packet sent => if this is not received within a certain
 * timeout, it tries to retransmit the packet for a limited number of times: if no acknowledgement is ever received, the
 * packet is discarded.
 *
 * The forwarding engine is also capable of detecting duplicated packets. In fact, the tuple <Origin,SeqNo,THL> identify
 * a packet uniquely => comparing each received data packet with those in the forwarding queue, it is possible to say
 * whether a packet is duplicated or not. Also the engine maintains a cache of the last four transmitted packets so
 * duplicates can be detected even when they are not in the input queue
 *
 * Another interesting feature of the forwarding engine is the detection of ROUTING LOOPS.
 * This is achieved by comparing the ETX reported in the received packet with the ETX of the current node: the former
 * has to be strictly higher than the latter, because the ETX of a node is inductively defined as the quality of the
 * link to the parent plus the ETX of the parent itself, with the root node having ETX=0 => if the above check is not
 * passed, the forwarding engine resets the beacon interval and sets the PULL FLAG of the data packets in order to
 * force a topology update that will hopefully fix the loop; also it stops forwarding data packets for a LOOP interval,
 * which is the time needed to repair the LOOP.
 */

#include <limits.h>
#include "application.h"


/* FORWARD DECLARATIONS */

void cache_remove(unsigned char offset,node_state* state);
bool compare_packets(ctp_data_packet* a,ctp_data_packet* b);

/* FORWARDING POOL - start */

/*
 * FORWARDING POOL - GET ENTRY
 *
 * Get an entry from the pool; it's the one at the position indicated by the variable "forwarding_pool_index"
 *
 * @state: pointer to the object representing the current state of the node
 *
 * Returns a pointer to the entry of the pool, NULL if the pool is empty
 */

forwarding_queue_entry* forwarding_pool_get(node_state* state){

        /*
         * If teh pool is empty, return NULL
         */

        if(!state->forwarding_pool_count)
                return NULL;

        /*
         * The return value: the entry corresponding to element "index" of the pool
         */

        forwarding_queue_entry* entry=&state->forwarding_pool[state->forwarding_pool_index];

        /*
         * Set the "index" variable to the next available entry of the pool
         */

        state->forwarding_pool_index+=1;

        /*
         * Reduce by one the counter of entries in the pool
         */

        state->forwarding_pool_count-=1;

        /*
         * If "index" is now beyond the limit of the pool, set it to the first position
         */

        if(state->forwarding_pool_index==FORWARDING_POOL_DEPTH)
                state->forwarding_pool_index=0;

        /*
         * Return the entry
         */

        return entry;
}

/*
 * FORWARDING POOL - PUT ENTRY
 *
 * Add an entry to the pool in the first available position
 *
 * @entry: pointer to the "forwarding_queue_entry" representing the element to be put in the pool
 * @state: pointer to the object representing the current state of the node
 */

void forwarding_pool_put(forwarding_queue_entry* entry,node_state* state){

        /*
         * Index of the first free place where the entry will be stored
         */

        unsigned char index;

        /*
         * Check if the pool is full: an entry can be added only if not full
         */

        if(state->forwarding_pool_count<FORWARDING_POOL_DEPTH){

                /*
                 * Get the index of a free position in the pool where the entry can be stored
                 */

                index=state->forwarding_pool_count+state->forwarding_pool_index;

                /*
                 * If the index is beyond the limit of the pool, correct it
                 */

                if(index>=FORWARDING_POOL_DEPTH)
                        index-=FORWARDING_POOL_DEPTH;

                /*
                 * Put the given entry in the first free place
                 */

                state->forwarding_pool[index]=*entry;

                /*
                 * Increase by one the counter of elements in the pool
                 */

                state->forwarding_pool_count+=1;
        }

}

/* FORWARDING POOL - end */

/* FORWARDING QUEUE - start */

/*
 * FORWARDING QUEUE - ENQUEUE ELEMENT
 *
 * When a new element has to be enqueued, it's inserted at the position specified by the variable "tail", because the
 * queue has a FIFO logic. After the element has been inserted, the "tail" variable is incremented, so the the next
 * queued element will be inserted after the current element; also the counter of the elements in the queue is
 * incremented.
 *
 * @entry: pointer to the "forwarding_queue_entry" representing the element to be enqueued
 * @state: pointer to the object representing the current state of the node
 *
 * Returns true if the element is successfully enqueued, false otherwise
 */

bool forwarding_queue_enqueue(forwarding_queue_entry* entry,node_state* state){

        /*
         * Check if there's free space in the queue
         */

        if(state->forwarding_queue_count<FORWARDING_QUEUE_DEPTH){

                /*
                 * There's enough space in the queue for at least one new element => insert the new element at position
                 * determined by the "tail" variable
                 */

                state->forwarding_queue[state->forwarding_queue_tail]=entry;

                /*
                 * Update the counter for the number of elements in the queue
                 */

                state->forwarding_queue_count+=1;

                /*
                 * Update the position corresponding to the tail of the queue, where the next coming element will be
                 * inserted
                 */

                state->forwarding_queue_tail+=1;

                /*
                 * Check if the tail is now beyond the limit of the queue: if so, reset the position of the tail to 0.
                 * This is mandatory to implement the FIFO logic
                 */

                if(state->forwarding_queue_tail==FORWARDING_QUEUE_DEPTH)
                        state->forwarding_queue_tail=0;

                /*
                 * Packet enqueued => return true
                 */

                return true;
        }

        /*
         * The queue is full, so no entry can be initialized => return false
         */

        return false;
}

/*
 * FORWARDING QUEUE - DEQUEUE ELEMENT
 *
 * Every time an element of the queue is to be removed, the one corresponding to the head of the queue is chosen. The
 * counter of the elements in the queue is decreased, while the position of the head is incremented, so the element that
 * was added after the current one will be chosen next time this function will be invoked.
 *
 * @state: pointer to the object representing the current state of the node
 *
 * NOTE: this function has to be called after a packet has been successfully forwarded  in order to free space in the
 * output queue
 */

void forwarding_queue_dequeue(node_state* state){

        /*
         * Check if there's at least one element in the queue
         */

        if(state->forwarding_queue_count){

                /*
                 * There's at least one new element => set the position of the head to the next element in the queue
                 */

                state->forwarding_queue_head+=1;

                /*
                 * Decrease the counter for the number of elements in the queue
                 */

                state->forwarding_queue_count-=1;

                /*
                 * Check if the head is now beyond the limit of the queue: if so, reset the position of the head to 0.
                 * This is mandatory to implement the FIFO logic
                 */

                if(state->forwarding_queue_head==FORWARDING_QUEUE_DEPTH)
                        state->forwarding_queue_head=0;
        }

        /*
         * No element in the queue => do nothing
         */
}

/*
 * FORWARDING QUEUE - LOOKUP
 *
 * Returns true if the given data packet is in the output queue, i.e. it is waiting for other packets to be sent, false
 * otherwise
 *
 * @data_frame: the data frame of the packet to be looked up
 * @forwarding_queue: the forwarding queue of the current node
 * @count: number of queued packets
 */

bool forwarding_queue_lookup(ctp_data_packet_frame* data_frame,forwarding_queue_entry* forwarding_queue[],
                             unsigned char count){

        /*
         * Index used to iterate through the packets in the cache
         */

        unsigned char i;

        /*
         * Scan the output queue until an item matching the searched packet is found
         */

        for(i=0;i<count;i++){

                /*
                 * The data frame of the element of the output queue analyzed
                 */

                ctp_data_packet_frame current=forwarding_queue[i]->packet.data_packet_frame;

                /*
                 * If the current element matches the given packet return true
                 */

                if(data_frame->THL==current.THL &&
                   data_frame->origin==current.origin &&
                   data_frame->seqNo==current.seqNo)

                        return true;
        }

        /*
         * The searched packets has not been found => return false
         */

        return false;
}

/* FORWARDING QUEUE - end */

/* OUTPUT CACHE - start */

/*
 * OUTPUT CACHE - LOOKUP
 *
 * Returns the index of the given data packet if it's in the output cache, i.e. it has recently been sent, and the
 * counter of elements in the cache otherwise
 *
 * @data_frame: the data frame of the packet to be looked up
 * @state: pointer to the object representing the current state of the node
 *
 * NOTE: the index is returned as offset from the position of the least recently added element of the cache
 */

unsigned char cache_lookup(ctp_data_packet_frame* data_frame,node_state* state){

        /*
         * Variable used to iterate through the packets in the cache
         */

        unsigned char i;

        /*
         * Index of the current entry during the iteration
         */

        unsigned char index;

        /*
         * Scan the output cache until an item matching the searched packet is found
         */

        for(i=0;i<state->output_cache_count;i++){

                /*
                 * Get the index of the entry
                 */

                index=(state->output_cache_first+i)%CACHE_SIZE;

                /*
                 * The data frame of the element of the cache analyzed
                 */

                ctp_data_packet_frame current=state->output_cache[index].data_packet_frame;

                /*
                 * If the current element matches the given packet return true
                 */

                if(data_frame->THL==current.THL &&
                   data_frame->origin==current.origin &&
                   data_frame->seqNo==current.seqNo)

                        break;
        }

        /*
         * Return the index of the entry corresponding to given packet, the counter of elements in the cache otherwise
         */

        return i;
}

/*
 * OUTPUT CACHE - ENQUEUE
 *
 * Adds a new entry in the output cache.
 * If there's no space left for the packet, since the cache adopts a LRU logic, the least recently inserted packet is
 * removed from the cache to free space for the new packet to be inserted
 *
 * @data_frame: the data frame of the packet to be inserted
 * @state: pointer to the object representing the current state of the node
 */

void cache_enqueue(ctp_data_packet_frame* data_frame,node_state* state){

        /*
         * Index of the entry of the cache corresponding to the given packet
         */

        unsigned char i;

        /*
         * Pointer to the data frame of the element of the cache analyzed
         */

        ctp_data_packet_frame* new_data_frame;

        /*
         * Check whether the cache is full
         */

        if(state->output_cache_count==CACHE_SIZE){

                /*
                 * The output cache is full => remove the least recently inserted packet from it
                 * Check if the packet is already in the cache
                 */

                i=cache_lookup(data_frame,state);

                /*
                 * If the packet was not already in the cache, "i" is set to the number of packets cached => the first
                 * element (least recently inserted) is removed.
                 * If the packet was already in the cache, "i" is set to its offset w.r.t. the least recently added
                 * element => the packet is removed from the cache before being re-inserted
                 */

                cache_remove(i%state->output_cache_count,state);
        }

        /*
         * Get the data frame of the entry where the most recently accessed element will be put
         */

        new_data_frame=&state->output_cache[(state->output_cache_first+state->output_cache_count)%CACHE_SIZE].
                data_packet_frame;

        /*
         * Set the new entry
         */

        new_data_frame->THL=data_frame->THL;
        new_data_frame->origin=data_frame->origin;
        new_data_frame->seqNo=data_frame->seqNo;

        /*
         * Update the counter of elements in the cache
         */

        state->output_cache_count+=1;
}

/*
 * OUTPUT CACHE - REMOVE
 *
 * Remove the entry in the output cache at given offset with respect the index corresponding to "output_cache_first".
 * This function is called only when an element has to be inserted in the cache and this is full, so after an element is
 * removed, a new element is inserted
 *
 * @state: pointer to the object representing the current state of the node
 */

void cache_remove(unsigned char offset,node_state* state){

        /*
         * Variable used to iterate through existing entries
         */

        unsigned char i;

        /*
         * Check if the given offset is valid, namely it's less than the number of entries in the cache
         */

        if(offset<state->output_cache_count)

                /*
                 * Given index does not correspond to any entry of the cache
                 */

                return;

        /*
         * If the given offset is 0, the packet to be removed is the one that was least recently added, which is at
         * position indicated by the variable "output_cache_first" => the element that is about to be inserted is going
         * to replace this element.
         * It is necessary to shift "output_cache_first" by 1, so that the next element removed will always be the least
         * recently accessed one.
         */

        if(!offset) {
                state->output_cache_first+=1;
                state->output_cache_first = (state->output_cache_first) % CACHE_SIZE;
        }
        else{

                /*
                 * The element to be removed is not the least recently accessed one: this happens when an element that
                 * is already in the cache has to be inserted again, namely it is accessed again => it has to be moved
                 * to indicate that is the most recently accessed element of the cache; also the least recently accessed
                 * element has to be removed
                 *
                 * In order to do this, all the elements of the cache are shifted backward by one position, without
                 * changing the value of "output_cache_first" => the most recently accessed element will be inserted
                 * before the least recently accessed one, pointed by the "output_cache_first" variable
                 */

                for(i=offset;i<state->output_cache_count;i++){
                        memcpy(&state->output_cache[(offset+i)%CACHE_SIZE],&state->output_cache[(offset+i+1)%CACHE_SIZE]
                                ,sizeof(ctp_data_packet));
                }
        }

        /*
         * Decrease by one the counter of packets in the cache
         */

        state->output_cache_count-=1;
}

/* OUTPUT CACHE - end */

/*
 * START FORWARDING ENGINE
 *
 * This function is in charge of initializing the variables of the forwarding engine and to start a periodic timer that
 * sends data packets as it gets fired (if not the root node)
 * It is invoked when the node (logical process) is delivered the INIT event.
 *
 * @state: pointer to the object representing the current state of the node
 */

void start_forwarding_engine(node_state* state){

        /*
         * First initialize the forwarding pool
         */

        state->forwarding_pool_count=FORWARDING_POOL_DEPTH;
        state->forwarding_pool_index=0;

        /*
         * Then the forwarding queue
         */

        state->forwarding_queue_count=0;
        state->forwarding_queue_head=0;
        state->forwarding_queue_tail=0;

        /*
         * Then set the sequence number of the first data packet to be sent to 0
         */

        state->data_packet_seqNo=0;

        /*
         * Check if it's the root node: if not, schedule the sending of a data packet
         */

        if(!state->root)

                /*
                 * Start the periodic timer with interval SEND_PACKET_TIMER: every time is fired, the a data packet is
                 * created and sent.
                 * The simulator is in charge of re-setting the timer every time it is fired
                 */

                wait_until(state->me.ID,state->lvt+SEND_PACKET_TIMER,SEND_PACKET_TIMER_FIRED);
}

/*
 * SEND DATA PACKET
 *
 * This function is in charge of forwarding the first element of the output queue (the least recently added, because it
 * is a FIFO queue), if any.
 *
 * If the output queue contains at least one packet, the forwarding engine checks that a route exists towards the root
 * of the collection tree: if so, a few parameters are set for sending the packet and then it is sent. The forwarding
 * engine relies on the routing engine for a few pieces of information related to the path of the packet to be forwarded.
 *
 * This function removes packets from the output queue until one that is not a duplicated is found; the corresponding
 * entry of the forwarding queue is not dequeued until the packet gets acknowledged by the intended recipient
 *
 * Returns true if the function has to be invoked again. This happens when the head of the queue is a duplicate: in fact
 * it is removed from the queue, so a further call to the queue can be made to forward the next packet (unless it is a
 * duplicate on its turn).
 *
 * @state: pointer to the object representing the current state of the node
 */

bool send_data_packet(node_state* state) {

        /*
         * Value of the ETX of the current route
         */

        unsigned short etx;

        /*
         * Pointer to the entry of the forwarding queue that currently occupies the head position
         */

        forwarding_queue_entry* first_entry;

        /*
         * ID and coordinates of the recipient of the data packet, namely the the current parent => the forwarding
         * engine asks the routing engine about the identity of the current parent node
         */

        node parent;

        /*
         * Check if there at least one packet to forward; when the output queue is empty, its "counter" is set to 0
         */

        if (!state->forwarding_queue_count){

                /*
                 * Output queue is empty => return false because a further invocation will be of no help
                 */

                return false;
        }

        /*
         * The output queue is not empty.
         * Check if the node has a valid route => ask the routing engine the ETX corresponding to the current route of
         * the node
         */

        if ((!get_etx(&etx,state))) {

                /*
                 * The function "get_etx" returns false if the parent of the node is not valid => if this is the case,
                 * it means the route of the node is not valid => schedule a new forwarding attempt after an interval of
                 * time equal to NO_ROUTE_RETRY; during this time, hopefully the node has fixed its route.
                 */

                wait_until(state->me.ID,state->lvt+NO_ROUTE_OFFSET,SEND_PACKET_TIMER_FIRED);

                /*
                 * Return false, since an immediate further invocation will be of no help: it is necessary to wait at
                 * least an interval of time equal to NO_ROUTE_RETRY
                 */

                return false;
        }

        /*
         * Check whether the node is waiting for the acknowledgment of the last data packet sent: if so, hold on
         */

        if(state->state&ACK_PENDING)
                return false;

        /*
         * The node has a valid route (parent) => before sending the head packet, check that it's not duplicated.
         * Get a pointer the to entry corresponding to the head of the output queue
         */

        first_entry=state->forwarding_queue[state->forwarding_queue_head];

        /*
         * Perform the check on the packet corresponding to the selected entry of the queue
         */

        if(cache_lookup(&first_entry->packet.data_packet_frame,state)<state->output_cache_count){

                /*
                 * The data packet is already in the output cache => is a duplicate => remove the entry of the current
                 * packet from the output queue...
                 */

                forwarding_queue_dequeue(state);

                /*
                 * ...and give it back to forwarding pool
                 */

                forwarding_pool_put(first_entry,state);

                /*
                 * Now that the duplicated has been removed from the forwarding queue, return true because the new head
                 * of the queue may not be a duplicate
                 */

                return true;
        }

        /*
         * The packet is not a duplicate => it can be forwarded.
         * Set the ETX field of the data frame
         */

        first_entry->packet.data_packet_frame.ETX=etx;

        /*
         * Clear PULL flag from the packets to be forwarded
         */

        first_entry->packet.data_packet_frame.options&=~CTP_PULL;

        /*
         * Check if the node is congested: if so, set the flag in the packet, otherwise clear the flag
         */

        if(is_congested(state))
                first_entry->packet.data_packet_frame.options|=CTP_CONGESTED;
        else
                first_entry->packet.data_packet_frame.options&=~CTP_CONGESTED;

        /*
         * Get the ID and coordinates of the recipient (parent node) from the routing engine
         */

        parent=get_parent(state);

        /*
         * Check if the parent is valid, i.e. its coordinates are not set to INT_MAX
         */

        if(parent.coordinates.x==INT_MAX)
                printf("wrong parent for node %d\n",state->me.ID);

        /*
         * Set the "src" and "dst" fields of the physical overhead of the packet
         */

        first_entry->packet.phy_mac_overhead.src=state->me;
        first_entry->packet.phy_mac_overhead.dst=parent;

        /*
         * Forward the data packet to the specified destination => schedule a new unicast event whose recipient is the
         * parent node reported in the physical layer frame of the packet
         */

        unicast_message(&first_entry->packet,state->lvt,state->me.ID);

        /*
         * The node is now waiting for the last data packet sent to be acknowledged => set the corresponding flag
         */

        state->state|=ACK_PENDING;

        /*
         * The data packet has been sent => no need to re-execute this function
         */

        return false;
}

/*
 * CREATE DATA PACKET
 *
 * Create a well-formed data packet (see its definition in "application.h") out of a payload (the data that node has
 * collected and is willing to deliver to the root of the collection tree) and add it to the forwarding queue.
 * The payload here is simulated as a random value within a given range.
 * Packets in the forwarding queue are forwarded, one at a time, according to a FIFO logic => after having queued up
 * a new packet, forward the one that currently is at the head of the queue.
 * This function can't be invoked by the root node, because this only collects data from the collection tree
 *
 * NOTE a node sends one data packet at a time => after the last created data packet has been acknowledged, a new packet
 * can be created. In other words, there's a data packet created by the node at a time in the forwarding queue
 *
 * @state: pointer to the object representing the current state of the node
 */

void create_data_packet(node_state* state){

        /*
         * Pointer to the data frame of the next data packet to send
         */

        ctp_data_packet_frame *data_frame;

        /*
         * Check if the last data packet created has already been acknowledged: if not, wait before creating a new one
         */

        if(!(state->state&SENDING)) {

                /*
                 * Set the payload of the data packet to be sent
                 */

                state->data_packet.payload = RandomRange(MIN_PAYLOAD, MAX_PAYLOAD);

                /*
                 * Get the data frame from the data packet to be sent
                 */

                data_frame = &state->data_packet.data_packet_frame;

                /*
                 * Set the fields of the data frame related to forwarding.
                 * Start with origin, which has to be set to the ID of the current node
                 */

                data_frame->origin = state->me.ID;

                /*
                 * Then set the sequence number
                 */

                data_frame->seqNo = state->data_packet_seqNo;

                /*
                 * Update the value of the sequence number for the next data packet
                 */

                state->data_packet_seqNo += 1;

                /*
                 * Finally set the THL (Time Has Lived) field to 0, because the packet has been just created
                 */

                data_frame->THL = 0;

                /*
                 * Check if there's at least one free entry in output queue to send the new packet => if this, is the
                 * case, the variable "forwarding_queue_count" is less than the depth of the queue
                 */

                if (state->forwarding_queue_count < FORWARDING_QUEUE_DEPTH) {

                        /*
                         * The function that is in charge of actually sending the packet, works as follows;
                         * 1-gets the head entry in the forwarding queue
                         * 2-extracts a packet from it
                         * 3-sends it to the parent node.
                         *
                         * If the head of the queue is a duplicate packet, it is removed => in this case the function
                         * returns true, meaning that it has to be invoked again to send the next packet in the queue
                         * => the variable below is set to true if a new sending attempt has to be made
                         */

                        bool try_again;

                        /*
                         * There's free space in the forwarding queue => initialize the entry for the packet to be
                         * queued up. Initialize the dedicated pointer to the data packet the entry corresponds to
                         */

                        state->local_entry.packet = state->data_packet;

                        /*
                         * Set the number of transmission attempt to its maximum value: every time a transmission fails,
                         * this counter is decreased and when it's equal to 0 the packet is dropped
                         */

                        state->local_entry.retries = MAX_RETRIES;

                        /*
                         * Set the flag indicating that this node created the packet to be sent
                         */

                        state->local_entry.is_local = true;

                        /*
                         * Insert the entry for the packet to be sent in the forwarding queue: we have already seen that
                         * the queue is not full, so we don't further check the return value of the following call
                         */

                        forwarding_queue_enqueue(&state->local_entry, state);

                        /*
                         * Set the state of the node to SENDING, because the packet it created is now in the forwarding
                         * queue
                         */

                        state->state |= SENDING;

                        /*
                         * Now that the packet has been successfully queued up, it's time to send the data packet. Call
                         * the function the first time
                         */

                        try_again = send_data_packet(state);

                        /*
                         * If "try_again" is "true", it means that a new sending attempt is necessary => keep trying until a
                         * valid (not duplicate) packet is found in the output queue or this is empty (in either case,"try_again"
                         * is set to "false" and the loop ends)
                         */

                        while (try_again)
                                try_again = send_data_packet(state);
                }
        }
        else{
                /*
                 * The last packet created by this node has not been acknowledged yet => try to speed up the sending
                 * of the packets in the forwarding queue
                 */

                send_data_packet(state);
        }
}

/*
 * RECEIVE DATA PACKET
 *
 * When an event with type DATA_PACKET_RECEIVED is delivered to the node (logic process), this function is invoked to
 * process the received message; after the processing, the packet is forwarded to neighbor nodes, so that it will
 * hopefully reach the root node sooner or later.
 *
 * A check is made to detect if the message is duplicated => we check both the output queue and the cache with the most
 * recently forwarded packets, hence it is possible to detect duplicates also after they have been sent
 *
 * @message: the payload from the content of the event delivered to the node
 * @state: pointer to the object representing the current state of the node
 * @time: local value of virtual time
 */

void receive_data_packet(void* message,node_state* state,simtime_t time) {

        /*
         * Parse the buffer received to a data packet
         */

        ctp_data_packet* packet = (ctp_data_packet *) message;

        /*
         * Get the ID and coordinates of the recipient of the ack, i.e. sender of the message
         */

        node recipient=packet->phy_mac_overhead.src;

        /*
         * Get the coordinates of the current node, that is recipient of the message and the sender of the ack
         */

        node_coordinates sender_coordinates=packet->phy_mac_overhead.dst.coordinates;

        /*
         * First send an ack to the sender: let the simulator decides whether the ack will reach the destination,
         * depending on the distance and on the interferences
         */

        send_ack(sender_coordinates,recipient,time);

        /*
         * Increment the THL, because the packet is being forwarded by the current node
         */

        packet->data_packet_frame.THL+=1;

        /*
         * Now check if this is a duplicated one => first check if it has been already transmitted, looking for it in
         * the output cache
         */

        if (cache_lookup(&packet->data_packet_frame, state) < state->output_cache_count) {

                /*
                 * The received message has been recently sent, so this is a duplicate => drop it
                 */

                return;

        }

        /*
         * Then check if the packet has been already been inserted in the output queue, meaning that it will be soon
         * forwarded.
         * First check if the queue contains some element
         */

        if (state->forwarding_queue_count){
                if (forwarding_queue_lookup(&packet->data_packet_frame, state->forwarding_queue,
                                            state->forwarding_queue_count)) {

                        /*
                         * The received message is already in the output queue, so this is a duplicate => drop it
                         */

                        return;

                }
        }

        /*
         * The packet received is not a duplicate => check if the current node is the root of the collection tree
         */

        if(state->root) {

                /*
                 * The current node is the root of the collection tree => the packet reached its intended destination
                 * => schedule a new event in order to signal the reception. This translates into setting some variables
                 * that are read in order to decide whether the simulation has come to and end or not
                 */

                collected_data_packet(packet);
        }
        else{

                /*
                 * Forward the data packet frame of the packet received
                 */

                forward_data_packet(packet,state);
        }
}

/*
 * FORWARD DATA PACKET
 *
 * Forward the given data packet => this means getting an entry from the forwarding pool and adding it to the forwarding
 * queue; as soon as it occupies the head of the queue, it will be sent
 *
 * @packet: pointer to to the packet to be forwarded
 * @state: pointer to the object representing the current state of the node
 */

void forward_data_packet(ctp_data_packet* packet,node_state* state){

        /*
         * Check that the forwarding is not empty: if so, the packet can't be stored in the forwarding pool and it has
         * to be dropped
         */

        if(state->forwarding_pool_count){

                /*
                 * The entry corresponding to the packet to be forwarded
                 */

                forwarding_queue_entry* entry;

                /*
                 * Get the entry from the pool; can't be NULL because we have already checked if the poil is empty or
                 * not
                 */

                entry=forwarding_pool_get(state);

                /*
                 * Initialize the pointer of the entry to the packet received
                 */

                entry->packet=*packet;

                /*
                 * Set the number of retransmissions attempts to MAX_RETRIES: this will be decreased every time a
                 * retransmission fails; if it goes to 0, the corresponding packet is dropped
                 */

                entry->retries=MAX_RETRIES;

                /*
                 * Clear the flag to signal the fact that the packet is a forwarded one (not created by the node)
                 */

                entry->is_local=false;

                /*
                 * Try to add the entry to the forwarding queue
                 */

                if(forwarding_queue_enqueue(entry,state)){

                        /*
                         * The entry has been successfully enqueued.
                         * Now check if there's no routing loop, i.e. the ETX reported in the packet received is less
                         * than or equal to the ETX of this node => if so, if the packet was accepted, the packet may
                         * return to the sender, because this would be selected as parent by the current node => there
                         * would be a never-ending lopp => in order to avoid such loops, nodes drop packets whose ETX is
                         * less than or equal to their ETX.
                         * Ask the ETX of the current route to the ROUTING ENGINE
                         */

                        unsigned short my_etx;

                        /*
                         * Check whether the ROUTING ENGINE is aware of the ETX: if not, skip the check on the loop
                         */

                        if(get_etx(&my_etx,state)){

                                /*
                                 * Now that we know the ETX of the current node, get the one reported in the received
                                 * packet
                                 */

                                unsigned short packet_etx=packet->data_packet_frame.ETX;

                                /*
                                 * Compare the two values,
                                 */

                                if(packet_etx<=my_etx){

                                        /*
                                         * There's the risk that a routing loop exists => ask the ROUTING ENGINE to
                                         * update the route (by setting the maximum beacons frequency) and schedule a
                                         * new sending
                                         */

                                        reset_beacon_interval(state);
                                        wait_until(state->me.ID,state->lvt+LOOP_DETECTED_OFFSET,RETRANSMITT_DATA_PACKET);
                                        return;
                                }
                        }

                        /*
                         * If the node is waiting acknowledgment of the last sent data packet, hold on
                         */

                        if(state->state&ACK_PENDING)
                                return;

                        /*
                         * We get here if no loop has been detected or if it has been detected and it has been fixed and
                         * the node is not waiting for the acknowledgment at the moment
                         * => start sending packets in the forwarding queue, so the packet received will be forwarded
                         * sooner or later
                         */

                        send_data_packet(state);

                        /*
                         * Set the state of the node to ACK_PENDING
                         */

                        state->state|=ACK_PENDING;
                }

                /*
                 * The forwarding queue is full => return the entry to the message pool; the packet is dropped
                 */

                forwarding_pool_put(entry,state);
        }
}

/*
 * IS ACK RECEIVED?
 *
 * This function is invoked by the simulator, which impersonates the physical and data link layers, that tells the node
 * whether the recipient of the last data packet sent has acknowledged the packet itself or not.
 *
 * @is_packet_acknowledged: boolean variable that is set to true if the packets was acknowledged, false otherwise
 * @state: pointer to the object representing the current state of the node
 */

void receive_ack(bool is_packet_acknowledged,node_state* state){

        /*
         * Packet in the head position of the sending queue, i.e. the last data packet forwarded by the this node
         */

        forwarding_queue_entry* head_entry=state->forwarding_queue[state->forwarding_queue_head];

        /*
         * Check whether the last data packets has been acknowledged or not
         */

        if(!is_packet_acknowledged){

                /*
                 * Packet has not been acknowledged by the recipient => if the limit of re-transmissions has not been
                 * yet reached for this data packet, try to send the packet again after an interval of time equal to
                 * DATA_PACKET_RETRANSMISSION_OFFSET.
                 *
                 * Before doing this, inform the LINK ESTIMATOR about the fact that the recipient didn't acknowledge
                 * the data packet, since this piece of information is used by the LINK ESTIMATOR to re-calculate the
                 * outgoing link quality between the current node and the recipient => extract the ID of the latter
                 * from the last data packet sent
                 */

                check_if_ack_received(head_entry->packet.phy_mac_overhead.dst.ID,false,state->link_estimator_table);

                /*
                 * Since the outgoing link quality between the current node and the recipient has possibly changed, it
                 * may be the case that another neighbor is a better parent for this node => in order to check if this
                 * is the case and,if so, choose a new parent, invoke an update of the route to the ROUTING ENGINE
                 */

                update_route(state);

                /*
                 * Check whether the limit of re-transmissions has been reached
                 */

                if(head_entry->retries) {

                        /*
                         * It's still possible to retransmit the data packet => schedule a new event, to be processed
                         * after DATA_PACKET_RETRANSMISSION_OFFSET instants of time, that will trigger a new transmission
                         * attempt
                         * First, update the counter of transmission attempts for the packet
                         */

                        head_entry->retries -= 1;
                        wait_until(state->me.ID, state->lvt + DATA_PACKET_RETRANSMISSION_OFFSET,
                                   RETRANSMITT_DATA_PACKET);
                        return;
                }
                else{

                        /*
                         * This node has already tried to re-transmit the packet for a number of times bigger than
                         * MAX_RETRIES => it has to give up with this intention => first of all remove the packet from
                         * the forwarding queue, so that the next packet will be sent in the next forwarding phase
                         */

                        forwarding_queue_dequeue(state);

                        /*
                         * If the data packet was created by the node, clear the SENDING flag
                         */

                        if(head_entry->is_local)
                                state->state&=~SENDING;
                        else

                                /*
                                 * Otherwise, give the entry back to the pool
                                 */

                                forwarding_pool_put(head_entry,state);

                        /*
                         * We dropped the packet => no longer waiting for an ack by the recipient
                         */

                        state->state&=~ACK_PENDING;

                        /*
                         * Then start a new transmission phase, which will involve the next packet in the output queue
                         */

                        send_data_packet(state);
                }
        }
        else{

                /*
                 * Packet has been acknowledged by the recipient => remove the message from the output queue, so that
                 * next transmission phase will send the next packet in the output queue
                 */

                forwarding_queue_dequeue(state);

                /*
                 * Then inform the LINK ESTIMATOR about the fact that the recipient has acknowledged the data packet,
                 * since this piece of information is used by the LINK ESTIMATOR to re-calculate the outgoing link
                 * quality between the current node and the recipient => extract the ID of the latter from the last data
                 * packet sent
                 */

                check_if_ack_received(head_entry->packet.phy_mac_overhead.dst.ID,
                                      true,state->link_estimator_table);

                /*
                 * If the last packet sent was a forwarded one, insert in the output cache in order to avoid duplicates
                 */

                if(!head_entry->is_local) {
                        cache_enqueue(&head_entry->packet.data_packet_frame, state);

                        /*
                         * Return the entry of the last sent data packet to the forwarding pool
                         */

                        forwarding_pool_put(head_entry,state);
                }
                else{

                        /*
                         * The last packet created by the node has been successfully sent => clear the SENDING flag
                         */

                        state->state&=~SENDING;
                }

                /*
                 * The packet has been acknowledged => clear corresponding flag
                 */

                state->state&=~ACK_PENDING;

                /*
                 * Then start a new transmission phase, which will involve the next packet in the output queue
                 */

                send_data_packet(state);
        }
}

/*
 * IS THE NODE CONGESTED
 *
 * This function is invoked by the ROUTING ENGINE to get to know whether the node is congested, i.e. half of its
 * forwarding queue is full.
 * The ROUTING ENGINE asks about congestion before sending beacons to neighbors: if it is congested, it sets the flag
 * CONGESTION in the beacons sent => in this way, neighbors are aware that the node is congested and won't add further
 * workload to it by sending data packets
 *
 * @state: pointer to the object representing the current state of the node
 *
 * Returns true if more than half of the forwarding queue is full, false otherwise
 */

bool is_congested(node_state* state){

        /*
         * Get the counter of elements in the forwarding queue
         */

        unsigned char count=state->forwarding_queue_count;

        /*
         * Return true if more than half is full...
         */

        if(count>FORWARDING_QUEUE_DEPTH/2)
                return true;

        /*
         * ...false otherwise
         */

        return false;
}

/*
 * IS ACK RECEIVED
 *
 * This function is called when the maximum time for receiving an ack has elapsed: it checks whether the last data
 * packet sent has already been acknowledged or not. This is determined comparing the last data packet in the output
 * queue with the one attached to the event: if they coincide, this means that the packet has not been acknowledged,
 * because packets are removed from the output queue if the node receives their acknowledge before the timeout
 *
 * @state: pointer to the object representing the current state of the node
 * @packet: the packet to be acknowledged
 */

void is_ack_received(node_state* state,ctp_data_packet* packet){

        /*
         * First get the last packet sent by the node: it's the on that occupies the head of the forwarding queue
         */

        ctp_data_packet* last_packet=&state->forwarding_queue[state->forwarding_queue_head]->packet;

        /*
         * The boolean value that indicates whether the packet has been acknowledged or not
         */

        bool ack_received=true;

        /*
         * Check that the output queue is not empty, i.e. all the packets sent have already been acknowledged and so
         * they have already been removed from the output queue: if so, stop
         */

        if(!last_packet)
                return;

        /*
         * Compare the packet above with the one given as parameter to the function: if they coincide, the ack has
         * already been received, otherwise it has not been received
         */

        if(compare_packets(last_packet,packet))
                ack_received=false;

        /*
         * Tell the result to the FORWARDING ENGINE: it is in charge of deciding how to proceed
         */

        receive_ack(ack_received,state);
}

/*
 * COMPARE PACKETS
 *
 * Returns true if all their fields coincide, false otherwise
 *
 * @a:pointer to the first packet
 * @b:pointer to the other packet
 */

bool compare_packets(ctp_data_packet* a,ctp_data_packet* b){
        if(a->payload==b->payload && a->phy_mac_overhead.dst.ID==b->phy_mac_overhead.dst.ID&&
                a->phy_mac_overhead.src.ID==b->phy_mac_overhead.src.ID &&
                a->data_packet_frame.ETX==b->data_packet_frame.ETX &&
                a->data_packet_frame.origin==b->data_packet_frame.origin &&
                a->data_packet_frame.seqNo==b->data_packet_frame.seqNo&&
                a->data_packet_frame.THL==b->data_packet_frame.THL&&
                a->data_packet_frame.options==b->data_packet_frame.options)
                return true;

        return false;
}