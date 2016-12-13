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

#include "forwarding_engine.h"
#include "routing_engine.h"
#include "link_estimator.h"
#include "application.h"

/*
 * FORWARDING POOL - start
 *
 * When a data packet has to be forwarded, the node extracts one entry from this pool, initializes it to the data of
 * the data packet received and finally stores the entry in the forwarding queue.
 *
 * The pool is nothing more than fixed-size array, whose elements are of type "forwarding_queue_entry": in fact packets
 * to be forwarded are stored in the same output queue as packets created by the node itself and as soon as they reach
 * the head of the queue they are sent.
 *
 * An entry is taken from the pool using the "get" method and one is given back to the pool using the "put" method: the
 * entries are taken in order, according to their position, and are released in order.
 *
 * Two variables help handling the pool:
 *
 * 1-forwarding_pool_free
 * 2-forwarding_pool_index
 */

forwarding_queue_entry forwarding_pool[FORWARDING_POOL_DEPTH];
unsigned char forwarding_pool_count=FORWARDING_POOL_DEPTH; // Number of elements in the pool
unsigned char forwarding_pool_index=0; // Index of the array where the next entry put will be collocated

/*
 * FORWARDING POOL - GET ENTRY
 *
 * Get an entry from the pool; it's the one at the position indicated by the variable "forwarding_pool_index"
 */

forwarding_queue_entry forwarding_pool_get(){

        /*
         * An entry can be obtained only if the pool is not empty => check the counter
         */

        if(forwarding_pool_count){

                /*
                 * The return value: the entry corresponding to element "index" of the pool
                 */

                forwarding_queue_entry entry=forwarding_pool[forwarding_pool_index];

                /*
                 * Set the "index" variable to the next available entry of the pool
                 */

                forwarding_pool_index++;

                /*
                 * Reduce by one the counter of entries in the pool
                 */

                forwarding_pool_count--;

                /*
                 * If "index" is now beyond the limit of the pool, set it to the first position
                 */

                if(forwarding_pool_index==FORWARDING_POOL_DEPTH)
                        forwarding_pool_index=0;

                /*
                 * Return the entry
                 */

                return entry;
        }
        else
                /*
                 * Empty pool
                 */

                return NULL;
}

/*
 * FORWARDING POOL - PUT ENTRY
 *
 * Add an entry to the pool in the first available position
 */

void forwarding_pool_put(forwarding_queue_entry entry){

        /*
         * Index of the first free plac where the entry will be stored
         */

        unsigned char index;

        /*
         * Check if the pool is full: an entry can be added only if not full
         */

        if(forwarding_pool_count<FORWARDING_POOL_DEPTH){

                /*
                 * Get the index of a free position in the pool where the entry can be stored
                 */

                index=forwarding_pool_count+forwarding_pool_index;

                /*
                 * If the index is beyond the limit of the pool, correct it
                 */

                if(index>=FORWARDING_POOL_DEPTH)
                        index-=FORWARDING_POOL_DEPTH;

                /*
                 * Put the given entry in the first free place
                 */

                forwarding_pool[index]=entry;

                /*
                 * Increase by one the counter of elements in the pool
                 */

                forwarding_pool_count++;
        }

}

/*
 * FORWARDING POOL - end
 */

/*
 * DATA PACKET
 *
 * Next data packet to be sent: it carries the actual payload the node wants to be delivered to the root of the tree
 */

ctp_data_packet data_packet;

/*
 * LOCAL FORWARDING QUEUE ENTRY
 *
 * The entry of the forwarding queue associated to the current node when this has some data to be sent
 */

forwarding_queue_entry local_entry;

unsigned char self; // ID of the current node
bool is_root; // Boolean variable which is set if the current node is the root of the collection tree
unsigned char seqNo=0; // Sequence number of the packet to be sent (initially 0)
unsigned char state=0; // Combination of flags indicating the state of the FORWARDING ENGINE; at first is 0

/*
 * FORWARDING QUEUE - start
 *
 * An array of elements of type "forwarding_queue_entry" represents the output queue of the node; either packets created
 * by the node and packets to be forwarded are stored here.
 *
 * Three variables are necessary to implement the logic of a FIFO queue using such an array:
 *
 * 1-forwarding_queue_count
 * 2-forwarding_queue_head
 * 3-forwarding_queue_tail
 *
 * They are all set to 0 at first. Entries are not explicitly cleared when elements are dequeued, nor the head of the
 * queue is always represented by the first element of the array => the actual positions of the elements in the array
 * don't correspond to their logical position within the queue.
 *
 * As a consequence, it is necessary to check the value of "forwarding_queue_count" in order to determine whether the
 * queue is full or not.
 *
 * A packet is enqueued before being sent: when it reaches the head of the queue it is forwarded; at some point it is
 * then dequeued
 */

forwarding_queue_entry forwarding_queue[FORWARDING_QUEUE_DEPTH];

unsigned char forwarding_queue_count=0; // The counter of the elements in the forwarding queue
unsigned char forwarding_queue_head=0; // The index of the first element in the queue (least recently added)
unsigned char forwarding_queue_tail=0; // The index of the last element in the queue (most recently added)

/*
 * FORWARDING QUEUE - ENQUEUE ELEMENT
 *
 * When a new element has to be enqueued, it's inserted at the position specified by the variable "tail", because the
 * queue has a FIFO logic. After the element has been inserted, the "tail" variable is incremented, so the the next
 * queued element will be inserted after the current element; also the counter of the elements in the queue is
 * incremented.
 *
 * @entry: pointer to the "forwarding_queue_entry" representing the element to be enqueued
 *
 * Returns true if the element is successfully enqueued, false otherwise
 */

bool forwarding_queue_enqueue(forwarding_queue_entry* entry){

        /*
         * Check if there's free space in the queue
         */

        if(forwarding_queue_count<FORWARDING_QUEUE_DEPTH){

                /*
                 * There's enough space in the queue for at least one new element => insert the new element at position
                 * determined by the "tail" variable => initialize the entry in the queue to the values of the given
                 * entry
                 */

                forwarding_queue[forwarding_queue_tail].data_packet=entry->data_packet;
                forwarding_queue[forwarding_queue_tail].is_local=entry->is_local;
                forwarding_queue[forwarding_queue_tail].retries=entry->retries;

                /*
                 * Update the counter for the number of elements in the queue
                 */

                forwarding_queue_count++;

                /*
                 * Update the position corresponding to the tail of the queue, where the next coming element will be
                 * inserted
                 */

                forwarding_queue_tail++;

                /*
                 * Check if the tail is now beyond the limit of the queue: if so, reset the position of the tail to 0.
                 * This is mandatory to implement the FIFO logic
                 */

                if(forwarding_queue_tail==FORWARDING_QUEUE_DEPTH)
                        forwarding_queue_tail=0;
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
 * NOTE: this function has to be called after a packet has been successfully forwarded  in order to free space in the
 * output queue
 */

void forwarding_queue_dequeue(){

        /*
         * Check if there's at least one element in the queue
         */

        if(forwarding_queue_count){

                /*
                 * There's at least one new element => set the position of the head to the next element in the queue
                 */

                forwarding_queue_head++;

                /*
                 * Decrease the counter for the number of elements in the queue
                 */

                forwarding_queue_count--;

                /*
                 * Check if the head is now beyond the limit of the queue: if so, reset the position of the head to 0.
                 * This is mandatory to implement the FIFO logic
                 */

                if(forwarding_queue_head==FORWARDING_QUEUE_DEPTH)
                        forwarding_queue_head=0;
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
 */

bool forwarding_queue_lookup(ctp_data_packet_frame* data_frame){

        /*
         * Index used to iterate through the packets in the cache
         */

        unsigned char i;

        /*
         * Scan the output queue until an item matching the searched packet is found
         */

        for(i=0;i<FORWARDING_QUEUE_DEPTH;i++){

                /*
                 * The data frame of the element of the output queue analyzed
                 */

                ctp_data_packet_frame current=forwarding_queue[i].data_packet->data_packet_frame;

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

/*
 * FORWARDING QUEUE - end
 */

/*
 * OUTPUT CACHE
 *
 * An array of elements of data packets represents the output LRU (Least Recently Used )cache of the node, where are
 * stored the most recently packets sent by the node => it's used to avoid forwarding the same packet twice and to drop
 * duplicates when they are received.
 *
 * Two variables are necessary to implement the logic of a LRU cache:
 *
 * 1-output_cache_count
 * 2-output_cache_first
 *
 * They are both set to 0 at first. Entries are not explicitly cleared when elements are removed, nor the least recent
 * entry of the cache is always represented by the first element of the array => the actual positions of the elements
 * in the array don't correspond to their logical position within the cache.
 *
 * As a consequence, it is necessary to check the value of "output_cache_count" in order to determine whether the cache
 * is full or not.
 *
 * When a packet is inserted in the cache, it means it has been used => if the same packet is already in the cache, it
 * is moved in order to indicate that it has been recently accessed, otherwise the element that was least recently
 * used is removed
 */

ctp_data_packet output_cache[CACHE_SIZE];

unsigned char output_cache_count; // Number of sent data packets cached
unsigned char output_cache_first; // Index of the entry in the cache that was least recently added

/*
 * OUTPUT CACHE - LOOKUP
 *
 * Returns the index of the given data packet if it's in the output cache, i.e. it has recently been sent, and the
 * counter of elements in the cache otherwise
 *
 * @data_frame: the data frame of the packet to be looked up
 *
 * NOTE: the index is returned as offset from the position of the least recently added element of the cache
 */

unsigned char cache_lookup(ctp_data_packet_frame* data_frame){

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

        for(i=0;i<output_cache_count;i++){

                /*
                 * Get the index of the entry
                 */

                index=(output_cache_first+i)%CACHE_SIZE;

                /*
                 * The data frame of the element of the cache analyzed
                 */

                ctp_data_packet_frame current=output_cache[index].data_packet_frame;

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
 */

void cache_enqueue(ctp_data_packet_frame* data_frame){

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

        if(output_cache_count==CACHE_SIZE){

                /*
                 * The output cache is full => remove the least recently inserted packet from it
                 * Check if the packet is already in the cache
                 */

                i=cache_lookup(data_frame);

                /*
                 * If the packet was not already in the cache, "i" is set to the number of packets cached => the first
                 * element (least recently inserted) is removed.
                 * If the packet was already in the cache, "i" is set to its offset w.r.t. the least recently added
                 * element => the packet is removed from the cache before being re-inserted
                 */

                cache_remove(i%output_cache_count);
        }

        /*
         * Get the data frame of the entry where the most recently accessed element will be put
         */

        new_data_frame=&output_cache[(output_cache_first+output_cache_count)%CACHE_SIZE].data_packet_frame;

        /*
         * Set the new entry
         */

        new_data_frame->THL=data_frame->THL;
        new_data_frame->origin=data_frame->origin;
        new_data_frame->seqNo=data_frame->seqNo;

        /*
         * Update the counter of elements in the cache
         */

        output_cache_count++;
}

/*
 * OUTPUT CACHE - REMOVE
 *
 * Remove the entry in the output cache at given offset with respect the index corresponding to "output_cache_first".
 * This function is called only when an element has to be inserted in the cache and this is full, so after an element is
 * removed, a new element is inserted
 */

void cache_remove(unsigned char offset){

        /*
         * Variable used to iterate through existing entries
         */

        unsigned char i;

        /*
         * Check if the given offset is valid, namely it's less than the number of entries in the cache
         */

        if(offset<output_cache_count)

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

        if(!offset)
                output_cache_first=(++output_cache_first)%CACHE_SIZE;
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

                for(i=offset;i<output_cache_count;i++){
                        memcpy(&output_cache[(offset+i)%CACHE_SIZE],&output_cache[(offset+i+1)%CACHE_SIZE],sizeof(ctp_data_packet));
                }
        }

        /*
         * Decrease by one the counter of packets in the cache
         */

        output_cache_count--;
}

/*
 * INITIALIZE FORWARDING ENGINE
 *
 * This function is in charge of initializing the variables of the forwarding engine
 * TODO complete description
 * It is invoked when the node (logical process) is delivered the INIT event.
 *
 * @self: ID of this node (logical process)
 */

void init_forwarding_engine(unsigned int ID){

        /*
         * Store the ID of this node
         */

        self=ID;

        /*
         * Check if the current node is designed to be the root of the collection tree in this simulation; set the
         * flag "is_root" as consequence
         */

        if(self==CTP_ROOT)
                is_root=true;
        else
                is_root=false;
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
 */

bool send_data_packet() {

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

        if (!forwarding_queue_count) {

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

        if ((!get_etx(&etx))) {

                /*
                 * The function "get_etx" returns false if the parent of the node is not valid => if this is the case,
                 * it means the route of the node is not valid => schedule a new forwarding attempt after an interval of
                 * time equal to NO_ROUTE_RETRY; during this time, hopefully the node has fixed its route.
                 */

                wait_time(NO_ROUTE_INTERVAL,RETRANSMITT_DATA_PACKET);

                /*
                 * Return false, since an immediate further invocation will be of no help: it is necessary to wait at
                 * least an interval of time equal to NO_ROUTE_RETRY
                 */

                return false;
        }

        /*
         * The node has a valid route (parent) => before forwarding the head packet, check that it's not duplicated.
         * Get a pointer the to entry corresponding to the head of the output queue
         */

        first_entry=&forwarding_queue[forwarding_queue_head];

        /*
         * Perform the check on the packet corresponding to the selected entry of the queue
         */

        if(cache_lookup(&first_entry->data_packet->data_packet_frame)<output_cache_count){

                /*
                 * The data packet is in the output cache => is a duplicate, namely it has already been sent by the
                 * node => remove the entry of the current packet from the output queue
                 */

                forwarding_queue_dequeue();

                //TODO check if pools are necessary

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

        first_entry->data_packet->data_packet_frame.ETX=etx;

        /*
         * Clear PULL flag from the packets to be forwarded
         */

        first_entry->data_packet->data_packet_frame.options&=~CTP_PULL;

        /*
         * Get the ID and coordinates of the recipient (parent node) from the routing engine
         */

        parent=get_parent();

        /*
         * Forward the data packet to the specified destination => schedule a new unicast event whose recipient is the
         * parent node reported in the physical layer frame of the packet
         */

        unicast_event(&first_entry->data_packet,DATA_PACKET_RECEIVED);

        /*
         * Schedule a new event after time equal to the ACK_TIMEOUT, destined to the current node, in order to
         * simulate that the data packet may or may not be acknowledged by the link layer of the recipient.
         * This is crucial because the link estimator evaluates the link quality to the recipient also on the basis of
         * acks received
         */

        wait_time(DATA_PACKET_ACK_OFFSET,CHECK_ACK_RECEIVED);

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
 * Returns true if the data packet is successfully enqueued in the forwarding queue,i.e this is not full,false otherwise
 */

bool create_data_packet(){

        /*
         * Pointer to the data frame of the next data packet to send
         */

        ctp_data_packet_frame* data_frame;

        /*
         * Set the payload of the data packet to be sent
         */

        data_packet.payload=RandomRange(MIN_PAYLOAD,MAX_PAYLOAD);

        /*
         * Get the data frame from the data packet to be sent
         */

        data_frame=&data_packet.data_packet_frame;

        /*
         * Set the fields of the data frame related to forwarding.
         * Start with origin, which has to be set to the ID of the current node
         */

        data_frame->origin=self;

        /*
         * Then set the sequence number
         */

        data_frame->seqNo=seqNo;

        /*
         * Finally set the THL (Time Has Lived) field to 0, because the packet has been just created
         */

        data_frame->THL=0;

        /*
         * Check if there's at least one free entry in output queue to send the new packet => if this, is the case, the
         * variable "forwarding_queue_count" is less than the depth of the queue
         */

        if(forwarding_queue_count<FORWARDING_QUEUE_DEPTH) {

                /*
                 * There's free space in the forwarding queue => initialize the entry for the packet to be queued up.
                 * Initialize the dedicated pointer to the data packet the entry corresponds to
                 */

                local_entry.data_packet = &data_packet;

                /*
                 * Set the number of transmission attempt to its maximum value: every time a transmission fails, this
                 * counter is decreased and when it's equal to 0 the packet is dropped
                 */

                local_entry.retries = MAX_RETRIES;

                /*
                 * Set the flag indicating that this node created the packet to be sent
                 */

                local_entry.is_local=true;

                /*
                 * Insert the entry for the packet to be sent in the forwarding queue: we have already seen that the
                 * queue is not full, so we don't further check the return value of the following call
                 */

                forwarding_queue_enqueue(&local_entry);

                /*
                 * Now that the packet has been successfully queued up, it's time to send the data packet
                 */

                send_data_packet();

                /*
                 * Packet successfully added to the forwarding queue => return true
                 */

                return true;
        }

        /*
         * The output queue is full, so the packet can't be forwarded => return false
         */

        return false;
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
 *
 * Returns true if the packet is accepted by the node, false if it is dropped because it is duplicated
 */

bool receive_data_packet(void* message) {

        /*
         * Pointer to the entry of the output queue associated to a packet that is going to be sent
         */

        forwarding_queue_entry* entry;

        /*
         * THL of the data packet received
         */

        unsigned char thl;

        /*
         * Parse the buffer received to a data packet
         */

        ctp_data_packet packet=*((ctp_data_packet*)message);

        /*
         * Read the THL from the forwarding frame of the data packet
         */

        thl=packet.data_packet_frame.THL;

        /*
         * Increment the THL, beacause the packet is being forwarded by the current node
         */

        ++thl;

        /*
         * Update the dedicated field of the forwarding frame with the new value of thl
         */

        packet.data_packet_frame.THL=thl;

        /*
         * Now check if this is a duplicated one => first check if it has been already transmitted, looking for it in
         * the output cache
         */

        if(cache_lookup(&packet.data_packet_frame)<output_cache_count){

                /*
                 * The received message has been recently sent, so this is a duplicate => drop it
                 */

                return false;

        }

        /*
         * Then check if the packet has been already been inserted in the output queue, meaning that it will be soon
         * forwarded
         */

        if(forwarding_queue_lookup(&packet.data_packet_frame)){

                /*
                 * The received message is already in the output queue, so this is a duplicate => drop it
                 */

                return false;

        }

        /*
         * The packet received is not a duplicate => check if the current node is the root of the collection tree
         */

        if(is_root) {

                /*
                 * The current node is the root of the collection tree => the packet reached its intended destination
                 * => schedule a new event in order to signal the reception. This translates into setting some variables
                 * that are read in order to decide whether the simulation has come to and end or not
                 */

                collected_data_packet(&packet);
        }
        else{

                /*
                 * Forward the data packet received
                 */

                forward_data_packet(&data_packet);
        }

        /*
         * The message has been successfully received, so return true
         */

        return true;
}

/*
 * FORWARD DATA PACKET
 *
 * Forward the given data packet => this means getting an entry from the forwarding pool and addin it to the forwarding
 * queue
 *
 * @packet: pointer to to the packet to be forwarded
 */

bool forward_data_packet(ctp_data_packet* packet){

        /*
         * Check that the forwarding is not empty, otherwise the packet can't be forwarded
         */

        if(forwarding_pool_count){

                /*
                 * The entry corresponding to the packet to be forwarded
                 */

                forwarding_queue_entry entry;

                /*
                 * Get the entry from the pool
                 */

                entry=forwarding_pool_get();

        }
}

/*
 * IS ACK RECEIVED?
 *
 * This function is invoked by the simulator, which impersonates the physical and data link layers, that tells the node
 * whether the recipient of the last data packet sent has acknowledged the packet itself or not.
 *
 * @is_packet_acknowledged: boolean variable that is set to true if the packets was acknowledged, false otherwise
 */

void receive_ack(bool is_packet_acknowledged){

        /*
         * Packet in the head position of the sending queue, i.e. the last data packet forwarded by the this node
         */

        forwarding_queue_entry* head_entry=&forwarding_queue[forwarding_queue_head];

        /*
         * The ID of the recipient of the last data packet sent
         */

        unsigned int recipient;

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

                check_if_ack_received(head_entry->data_packet->phy_mac_overhead.dst.ID,false);

                /*
                 * Since the outgoing link quality between the current node and the recipient has possibly changed, it
                 * may be the case that another neighbor is a better parent for this node => in order to check if this
                 * is the case and,if so, choose a new parent, invoke an update of the route to the ROUTING ENGINE
                 */

                update_route();

                /*
                 * Check whether the limit of re-transmissions has been reached, after having decreased th corresponding
                 * value in the entry
                 */

                if(--head_entry->retries)

                        /*
                         * It's still possible to retransmit the data packet => schedule a new event, to be processed
                         * after DATA_PACKET_RETRANSMISSION_OFFSET instants of time, that will trigger a new tranmission
                         * attempt
                         */

                        wait_time(DATA_PACKET_RETRANSMISSION_OFFSET,NULL,DATA_PACKET_RETRANSMISSION_TIMER);
                else{

                        /*
                         * This node has already tried to re-transmit the packet for a number of times bigger than
                         * MAX_RETRIES => it has to give up with this intention => first of all remove the packet from
                         * the forwarding queue, so that the next packet will be sent in the next forwarding phase
                         */

                        forwarding_queue_dequeue();

                        /*
                         * Then start a new transmission phase, which will involve the next packet in the output queue
                         */

                        forward_data_packet();
                }
        }

        /*
         * Packet has not been acknowledged by the recipient => first remove it from the output queue, so that next
         * transmission phase will send the next packet in the output queue
         */

        forwarding_queue_dequeue();

        /*
         * Then inform the LINK ESTIMATOR about the fact that the recipient has acknowledged the data packet, since
         * this piece of information is used by the LINK ESTIMATOR to re-calculate the outgoing link quality between the
         * current node and the recipient => extract the ID of the latter from the last data packet sent
         */

        check_if_ack_received(head_entry->data_packet->phy_mac_overhead.dst.ID,true);

        /*
         * Finally, insert the current packet in the output cache => in such a way, we avoid forwarding duplicate packets
         */

        cache_queue
}