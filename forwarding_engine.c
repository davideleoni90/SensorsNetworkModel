/*
 * FORWARDING ENGINE
 *
 * Its main task is forwarding data packets received from neighbors as well as sending packets created by the node
 * itself. Moreover, it is in charge of detect duplicate packets and routing loops. Finally, it snoops data packets
 * directed to other nodes in order to detect a congestion status.
 *
 * The forwarding engine has a FIFO queue of fixed depth at its disposal to store packets before forwarding them: here
 * packets are both those coming from other neighbors and those created by the node itself.
 * TODO we allow only one data packet at a time
 *
 * Before actually sending a packet, it checks whether the parent is congested: if so, it holds on until congestion is
 * over or a new parent is selected.
 *
 * The forwarding engine waits for an acknowledgment for each packet sent => if this is not received within a certain
 * timeout, it tries to retransmit the packet for a limited number of times: if no acknowledgement is ever received, the
 * packet is discarded.
 *
 * It also deals with congestion of the node, namely when the packets queue is full and thus no new packets can be
 * forwarded => as soon as the input queue is half full, the forwarding engine sets the CONGESTION FLAG in the forwarded
 * data packets; also the ROUTING ENGINE is notified about the the congestion, hence it sets the CONGESTION FLAG in the
 * routing packets => in this way,the load is uniformly distributed among the nodes of the collection tree. Congestion
 * of a node can be quickly determined not only reading the CONGESTION FLAG from the packets, but also SNOOPING PACKETS
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

/*
 * DATA FRAME
 *
 * Pointer to the routing frame of the next routing packet to be sent
 */

//ctp_data_frame* data_frame;

/*
 * DATA PACKET
 *
 * Next data packet to be sent: it carries the actual payload the node wants to be delivered to the root of the tree
 */

ctp_data_packet data_packet;

unsigned char self; // ID of the current node
bool is_root; // Boolean variable which is set if the current node is the root of the collection tree
unsigned char seqNo=0; // Sequence number of the packet to be sent (initially 0)

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
                 * determined by the "tail" variable
                 */

                forwarding_queue[forwarding_queue_tail]=*entry;

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
 */

ctp_data_packet output_cache[CACHE_SIZE];

unsigned char output_cache_count; // Number of sent data packets cached
unsigned char output_cache_first; // Index of the entry in the cache that was least recently added

/*
 * OUTPUT CACHE - LOOKUP
 *
 * Returns the index of the given data packet is it's in the output cache, i.e. it has recently been sent, the counter
 * of elements in the cache otherwise
 *
 * @data_frame: the data frame of the packet to be looked up
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
 * If there's no space left for the packet, since the cache adopts a LRU logics, the least recently inserted packet is
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
                 * element (least recently inserted) is removed
                 */

                cache_remove(i%output_cache_count);
        }

        /*
         * Scan the outuput cache until an item matching the searched packet is found
         */

        for(i=0;i<CACHE_SIZE;i++){

                /*
                 * The data frame of the element of the cache analyzed
                 */

                ctp_data_packet_frame current=output_cache[i].data_packet_frame;

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
 * OUTPUT CACHE - REMOVE
 *
 * Remove the entry in the output cache at given offset with respect the index corresponding to "output_cache_first"
 */

void cache_remove(unsigned char offset){

        /*
         * Variable used to iterate through existing entries
         */

        unsigned char i;

        /*
         * Check if the given offset is valid, namely it's less than the number of entries in the cache
         */

        if(i<output_cache_count)

                /*
                 * Given index does not correspond to any entry of the cache
                 */

                return;

        /*
         * If the given offset is 0, the packet to be removed is exactly the one at position indicated by the variable
         * "output_cache_first" => it is necessary to shift by 1 this position
         */

        if(!index)
                output_cache_first=(++output_cache_first)%CACHE_SIZE;
        else{

                /*
                 * O
                 */

        }
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
 * FORWARD DATA PACKET
 *
 * This function is in charge of forwarding the first element of the output queue (the least recent added, because it
 * is a FIFO queue), if any..
 *
 * If the output queue contains at least one packet, the forwarding engine checks that a route exists towards the root
 * of the collection tree: if so, a few parameters are set for sending the packet and then it is sent. The forwarding
 * engine relies on the routing engine for a few pieces of information related to the path of the packet to be forwarded.
 *
 * This function removes packets from the output queue until one that is not a duplicated is found: this is forwarded
 * and the corresponding entry is not dequeued until it is  acknowledged
 *
 * Returns true if the function has to be invoked again. This happens when the head of the queue is a duplicate: in fact
 * it is removed from the queue, so a further call to the queue can be made to forward the next packet (unless it is a
 * duplicate on its turn).
 */

bool forward_data_packet() {

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

                //TODO schedule new event RETRY_FORWARDING

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
                 * The packet is a duplicate => remove the corresponding entry from the output queue
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
         * Clear CONGESTION and PULL flags from the packets to be forwarded
         * TODO check CONGESTION
         */

        first_entry->data_packet->data_packet_frame.options&=~CTP_CONGESTED;
        first_entry->data_packet->data_packet_frame.options&=~CTP_PULL;

        /*
         * Get the ID and coordinates of the recipient (parent node) from the routing engine
         */

        parent=get_parent();

        /*
         * Forward the data packet to the specified destination => schedule a new unicast event whose recipient is the
         * parent node reported in the physical layer frame of the packet
         */

        unicast_event(&first_entry->data_packet,SEND_DATA_PACKET);

        /*
         * Schedule a new event after time equal to the DATA_PACKET_ACK_OFFSET, destined to the current node, in order to
         * simulate that the data packet may or may not be acknowledged by the link layer => this is crucial because
         * this is how the link estimator evaluates the link quality to the recipient
         */

        self_wait_event(DATA_PACKET_ACK_OFFSET,&parent,DATA_PACKET_ACK_INTERVAL);
}

/*
 * SEND DATA PACKET
 *
 * Create a well-formed data packet (see its definition in "application.h") out of a payload (the data node has collected
 * and is willing to deliver to the root of the collection tree) and add it to the forwarding queue.
 * The payload here is simulated as a random value within a given range.
 * Packets in the forwarding queue are forwarded, one at a time, according to a FIFO logic => after having queued up
 * the packet, forward the packet at the head of the queue.
 * This function can't be invoked by the root node, because this only collects data from the collection tree
 *
 * Returns true if the data packet is successfully sent, false otherwise
 */

bool send_data_packet(){

        /*
         * Pointer to the data frame of the next data packet to send
         */

        ctp_data_packet_frame* data_frame;

        /*
         * Entry of the output queue associated to the next packet to be sent
         */

        forwarding_queue_entry entry;

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

                entry.data_packet = &data_packet;

                /*
                 * Set the number of transmission attempt to its maximum value: every time a transmission fails, this
                 * counter is decreased and when it's equal to 0 the packet is dropped
                 */

                entry.retries = MAX_RETRIES;

                /*
                 * Set the flag indicating that this node created the packet to be sent
                 */

                entry.is_local=true;

                /*
                 * Insert the entry for the packet to be sent in the forwarding queue: we have already seen that the
                 * queue is not full, so we don't further check the return value of the following call
                 */

                forwarding_queue_enqueue(&entry);

                /*
                 * Now that the packet has been successfully queued up, forward the head packet
                 */

                forward_data_packet();

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
 * When an event with type DATA_PACKET is delivered to the node (logic process), this function is invoked to process
 * the received message.
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

                //TODO schedule event to signal root reception
        }
        else{

                //TODO forward the packet
        }

        /*
         * The message has been successfully received, so return true
         */

        return true;
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

                ack_received(head_entry->data_packet->phy_mac_overhead.dst.ID,false);

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

                        self_wait_event(DATA_PACKET_RETRANSMISSION_OFFSET,NULL,DATA_PACKET_RETRANSMISSION_TIMER);
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

        ack_received(head_entry->data_packet->phy_mac_overhead.dst.ID,true);

        /*
         * Finally, insert the current packet in the output cache => in such a way, we avoid forwarding duplicate packets
         */

        cache_queue
}