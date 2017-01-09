#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ROOT-Sim.h>
#include "application.h"
#include "forwarding_engine.h"
#include <math.h>
#include <limits.h>

/* GLOBAL VARIABLES (shared among all logical processes) - start */

double discarded_packets=0;
double accepted_packets=0;


double collected_packets=0; // The number of packets successfully delivered by the root of the collection tree

/*
 * The vector containing a counter of packets received by the root of the collection tree from the other nodes
 */

unsigned int* collected_packets_list;
FILE* file; // Pointer to the file object associated to the configuration file

/*
 * The packets collected within an interval of 100 instants of time are printed; in particular, they are printed at
 * virtual time 100*time_factor, where time_factor is incremented after every printing
 */

unsigned char time_factor=1;

/*
 * ID of the node chosen as root of the collection tree => all the data packets will (hopefully) be collected by this
 * node.
 * If the ID of the node is not specified as parameter of the simulation, the default root is the node with ID=0
 */

unsigned int ctp_root=UINT_MAX;
unsigned char failed_nodes=0; // Number of nodes failed; this is one of the reasons for the simulation to stop

/*
 * FAILURE LAMBDA
 *
 * The lambda parameter of the exponential failure distribution: it depends on the devices used as node of the collection
 * tree and is equivalent to the failure rate or MTTF(Mean Time To Failure)
 */

double failure_lambda=0.0005;

/*
 * FAILURE PROBABILITY THRESHOLD
 *
 * The exponential failure distribution tells the probability that a failure occurs before a certain time =>
 * the following parameter determines which is the minimum probability for the node to be considered as failed by the
 * simulator
 */

double failure_threshold=0.9;

/*
 * COORDINATES OF NODES
 *
 * Dynamically allocated array containing the coordinates of each node in the network (length = n_proc_tot, the number
 * of logical processes in the simulation) indexed according to the ID; the coordinates of the nodes are read from a
 * configuration file decided by the user.
 *
 * The list of coordinates comes into play when a node sends BROADCAST messages. Since the Collection Tree Protocol is
 * a distributed algorithm, nodes initially know nothing about the number of nodes in the network, about their IDs and
 * coordinates => the only way to learn about them is sending broadcasting beacons: those nodes that are within the
 * area covered by the wireless link (neighbours) will learn about the position and ID of the node, and will store them
 * in the ROUTING TABLE. In this way, they will be able to send him unicast messages in the future.
 * When a node send beacons, it's not aware of the coordinates and IDs of the nodes in the network, so it has to ask
 * the simulator to deliver the messages to the neighbours. The simulator keeps track of the number of nodes in the WSN,
 * of their coordinates and IDs through the following array
 */

node_coordinates* nodes_coordinates_list;

/* GLOBAL VARIABLES (shared among all logical processes) - end */

/* FORWARD DECLARATIONS */

void parse_topology(const char* path);
void start_routing_engine(node_state* state);
bool is_message_received(node_coordinates a,node_coordinates b);
bool is_failed(simtime_t now);
double euclidean_distance(node_coordinates a,node_coordinates b);

/*
 * Application-level callback: this is the interface between the simulator and the model being simulated
 */

void ProcessEvent(unsigned int me, simtime_t now, int event_type, void *event_content, unsigned int size, void *ptr) {

        /*
         * Pointer to the object representing the state of this logical process (node)
         */

        node_state *state;

        /*
         * The structure representing this node
         */

        node this_node;

        /*
         * Index to iterate through nodes
         */

        int i;

        /*
         * Initialize the local pointer to the pointer provided by the simulator
         */

        state = (node_state*)ptr;

        /*
         * Check whether the state object has already been set: if so, update the the local virtual time
         */

        if(state)
                state->lvt=now;

        /*
         * Check if the node is just failed: if so, clear the RUNNING flag in the state field.
         * Skip the check at time 0, when the state has not been initialized yet.
         */

        if(now) {
                if (state->state & RUNNING) {
                        if (is_failed(now)) {

                                /*
                                 * Clear RUNNING FLAG in the state object
                                 */

                                state->state &= ~RUNNING;

                                /*
                                 * Increment by one the counter of failed nodes
                                 */

                                failed_nodes++;

                                /*
                                 * Notify the user about the failure
                                 */

                                printf("Node %d died at time %f\n", me, now);
                                fflush(stdout);
                        }
                }
        }

        /*
         * Depending on the event type, perform different tasks
         */

        switch(event_type) {

                case INIT:

                        /*
                         * NODE INITIALIZATION
                         *
                         * This is the default event signalled by the simulator to each logical process => it triggers
                         * the initialization of the node.
                         *
                         * In this phase, a new state object is dynamically allocated and its address is communicated to
                         * the simulator by mean of the API function "SetState" => in this way the simulator is aware of
                         * the memory address of the state object of processes, so it can transparently bring them back
                         * to a previous configuration in case of inconsistency problems.
                         *
                         * Before the simulation can start, it is necessary to parse the topology file provided by the
                         * user, containing the coordinates of all the nodes (and possibly also the ID of the node
                         * chosen as root of the collection tree) => since this implies dynamic memory allocation, only
                         * one node (the one with ID 0 or the one chosen by the user) performs this task, so the actual
                         * start of the other nodes has to be deferred
                         * => as soon as it has taken this step, an event (START_NODE) is broadcasted to all the other
                         * nodes: at this point they can properly start.
                         *
                         * The following steps have to be taken by all the nodes
                         */

                        /*
                         * Dynamically allocate the state object
                         */

                        state = (node_state *)malloc(sizeof(node_state));
                        if (state == NULL){
                                printf("Out of memory!\n");
                                exit(EXIT_FAILURE);
                        }

                        /*
                         * The state object has been successfully allocated => tell its address to the simulator
                         */

                        SetState(state);

                        /*
                         * Initialize the state structure
                         */

                        bzero(state, sizeof(node_state));

                        /*
                         * Set the RUNNING flag in the state object
                         */

                        state->state|=RUNNING;

                        /*
                         * Get the ID of the root node; if the user does not provide this parameter, the default root
                         * is node 0.
                         * The node chosen as root node sets the corresponding global variable to its own ID
                         */

                        if(IsParameterPresent(event_content, "root"))
                        {

                                /*
                                 * The ID of the node
                                 */

                                unsigned int root=GetParameterInt(event_content,"root");

                                /*
                                 * The user indicated the ID of the root node => check if it's valid and, if so, set
                                 * the corresponding global value
                                 */

                                if(root<n_prc_tot) {
                                        if(me==root)
                                                ctp_root = root;
                                }
                                else{

                                        /*
                                         * Abort because of invalid "root" parameter
                                         */

                                        printf("[FATAL ERROR] The given root ID is not valid: it has to be less that"
                                                       "the number of LPs\n");
                                        free((state));
                                        exit(EXIT_FAILURE);
                                }
                        }
                        else{

                                /*
                                 * The user did not choose any node as root => the node with ID 0 declares itself to
                                 * be the root
                                 */

                                if(!me)
                                        ctp_root=0;
                        }

                        /*
                         * All logical processes (except root node) stop here, waiting for the signal to start the node
                         */

                        if(me==ctp_root){

                                /* GET TOPOLOGY OF THE NETWORK (ONLY THE ROOT NODE) - start */

                                /*
                                 * Parse the file containing the topology of the network: if this is not specified,
                                 * return with error
                                 */

                                if(IsParameterPresent(event_content, "topology")) {
                                        parse_topology(GetParameterString(event_content, "topology"));
                                }
                                else{
                                        printf("[FATAL ERROR] The path a to file containing the topology of the network"
                                                       "is mandatory => "
                                                       "specify it after "
                                                       "the argument \"path\"\n");
                                        free((state));
                                        exit(EXIT_FAILURE);
                                }

                                /* GET NODES OF THE SIMULATION - end */

                                /*
                                 * Set the "root" flag in the state object
                                 */

                                state->root=true;

                                /*
                                 * Allocate the array for counters of packets received by the root from the nodes
                                 */

                                collected_packets_list=malloc(sizeof(unsigned int)*n_prc_tot);

                                /*
                                 * Initialize counters to 0
                                 */

                                bzero(collected_packets_list,sizeof(unsigned int)*n_prc_tot);

                                /*
                                 * All the parameters of the configuration have been parsed => tell all the processes
                                 * that the time to start the simulation has come
                                 */
                                for(i=0;i<n_prc_tot;i++)
                                        ScheduleNewEvent(i,now+(simtime_t)Random(),START_NODE,NULL,0);

                        }

                        break;

                case START_NODE:

                        /*
                         * START THE NODE
                         *
                         * This event comes after the INIT one =>
                         *
                         * 1 - the global array "nodes_coordinates_list" contains the coordinates of all the nodes,
                         * indexed according to their IDs
                         * 2 - the "ctp_root" is initialized to either the ID chosen by the user for the root node or
                         * or to 0 if the user did not provide any value for parameter "root"
                         *
                         * => every node stores its coordinates in its state object and then initializes its Collection
                         * Tree Protocol stack, which is mandatory for it to be able to communicate with the other
                         * nodes
                         */

                        /*
                         * First store ID and coordinates in the state
                         */

                        this_node.ID=me;
                        this_node.coordinates=nodes_coordinates_list[me];
                        state->me=this_node;

                        /* INIT CTP STACK - start */

                        /*
                         * If this is the root node, set the corresponding flag in the state object
                         */

                        if(me==ctp_root) {
                                state->root = true;
                        }

                        /*
                         * Initialize the LINK ESTIMATOR => set the sequence number of the beacons to 0 and initialize
                         * entries of the link estimator table
                         */

                        state->beacon_sequence_number=0;
                        init_link_estimator_table(state->link_estimator_table);

                        /*
                         * Initialize the ROUTING ENGINE
                         */

                        start_routing_engine(state);

                        /*
                         * Initialize the FORWARDING ENGINE
                         */

                        start_forwarding_engine(state);


                        /* INIT CTP STACK - end */

                        break;

                case UPDATE_ROUTE_TIMER_FIRED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * It's time for the ROUTING ENGINE to update the route of the node => invoke the dedicated
                                 * function.
                                 */

                                update_route(state);

                                /*
                                 * The time simulated through this event is periodic => schedule this event after the same amount
                                 * of time, starting from now
                                 */

                                wait_until(me, now + UPDATE_ROUTE_TIMER, UPDATE_ROUTE_TIMER_FIRED);
                        }
                        break;

                case SEND_BEACONS_TIMER_FIRED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * It's time for the ROUTING ENGINE to send a beacon to its neighbors => before doing this,
                                 * update the route, so that information repSEND_BEACONS_TIMER_FIREDorted in the beacon will not be obsolete
                                 */

                                update_route(state);

                                /*
                                 * Now send the beacon
                                 */

                                send_beacon(state);

                                /*
                                 * The interval of the timer that schedules the sending of beacons is continuously changing, in
                                 * such a way that beacons are sent with decreasing frequency => schedule an update of the
                                 * timer, i.e. advance in the virtual time until the moment when the timer has to be updated
                                 */

                                schedule_beacons_interval_update(state);
                        }
                        break;

                case SEND_PACKET_TIMER_FIRED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * If the node is waiting for a packet to be acknowledged, keep waiting, otherwise create a new
                                 * packet and send it to the root node
                                 */

                                if (!(state->state & ACK_PENDING)) {
                                        create_data_packet(state);
                                }

                                /*
                                 * The time simulated through this event is periodic => schedule this event after the same amount
                                 * of time, starting from now
                                 */

                                wait_until(me, now + SEND_PACKET_TIMER, SEND_PACKET_TIMER_FIRED);
                        }
                        break;

                case RETRANSMITT_DATA_PACKET:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * This event is delivered to the node in order for it to transmit again the last data packet
                                 * sent: this is due to the fact that the packet has been acknowledged by the recipient.
                                 */

                                send_data_packet(state);
                        }
                        break;

                case SET_BEACONS_TIMER:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * This event is processed when the interval associated to the timer for beacons has to be
                                 * updated
                                 */

                                double_beacons_send_interval(state);
                        }
                        break;

                case BEACON_RECEIVED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * A beacon has been received => possibly update the neighbor and the routing table; the
                                 * LINK ESTIMATOR first processes the beacon and then "forwards" it to the above ROUTING LAYER
                                 */

                                receive_routing_packet(event_content, state);
                        }
                        break;

                case DATA_PACKET_RECEIVED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * The node has received a data packet => let the FORWARDING ENGINE process it and send
                                 * an ack to the sender
                                 */

                                receive_data_packet((ctp_data_packet *) event_content, state,now);
                        }
                        break;

                case ACK_RECEIVED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * The recipient of the last data packet sent has received it and has replied with an ack,
                                 * which has been successfully delivered on its turn => signal the event to the forwarding
                                 * engine: this will remove the last packet from the head of the output queue and will also
                                 * inform the link estimator
                                 */

                                receive_ack(true, state);
                        }
                        break;

                case CHECK_ACK_RECEIVED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * When a node sends or forwards a data packet, this is not removed from the output
                                 * queue until an acknowledgment for the packet is received.
                                 * The node does not wait the acknowledgement forever, but just for a timeout time =>
                                 * this event is processed by a node, when the timeout time has elapsed, in order to
                                 * check whether the ack has been received for not.
                                 * The fact that an acknowledgment has been received or not is determined comparing the
                                 * last data packet in the output queue with the one attached to the event
                                 */

                                is_ack_received(state, (ctp_data_packet *) event_content);
                        }
                        break;

                default:
                        printf("Events not handled\n");

        }
}

/*
 * TERMINATE SIMULATION?
 *
 * By mean of this function, each logic process (node of the sensors network) tells the simulator whether, according to
 * him, the simulation should terminate, also considering the given state of the simulation; the latter actually stops
 * when all the logic processes return true.
 *
 * The aim of this model is to simulate the implementation of the Collection Tree Protocol (CTP) => we stop when the
 * root node has received a number of data packets greater than or equal to COLLECTED_DATA_PACKETS_GOAL from each node.
 * As a consequence, all the logic processes associated to nodes of the collection tree will always return true here,
 * while the logic process associated to the root will return true iff the number of data packets collected from each
 * node is greater than or equal to COLLECTED_DATA_PACKETS_GOAL.
 * In order to avoid that the simulation runs forever if one of the nodes does not send enough packets, we also set a
 * time limit by mean of the variable MAX_TIME: when the virtual time reaches this value, the simulation stops, no
 * matter how many packets have been collected from each node.
 *
 * The simulation also stops when the root node fails or if there's no other node alive but the root of the tree
 *
 */


bool OnGVT(unsigned int me, void*snapshot) {

        /*
         * Index variable used to iterate through nodes of the simulation
         */

        int i=0;

        /*
         * If the current node is the root of the collection tree, check that there are still some nodes alive and
         * also check the number of data packets received
         */

        if(((node_state*)snapshot)->root){

                /*
                 * First check if the root is still alive: if not, terminate the simulation
                 */

                if(!(((node_state*)snapshot)->state&RUNNING))
                        return true;

                /*
                 * Then check that there's at least one node running: if not, stop the simulation
                 */

                if(n_prc_tot-failed_nodes<=1)
                        return true;

                /*
                 * If the value of virtual time is beyond the limit, stop the simulation
                 */

                if(((int)(((node_state*)snapshot)->lvt)%MAX_TIME==0) &&( ((node_state*)snapshot)->lvt>1)){
                        printf("\n\nSimulation stopped because reached the limit of time:%f\n"
                                       "Packets collected by root:%f\n"
                                ,((node_state*)snapshot)->lvt,collected_packets);
                        for(i=0;i<n_prc_tot;i++) {
                                if(i==ctp_root)

                                        /*
                                         * Root node does not send packets, only collects them
                                         */

                                        continue;
                                printf("Packets from %d:%d\n",i,collected_packets_list[i]);
                        }
                        printf("discarded:%f\n",discarded_packets);
                        printf("accepted:%f\n",accepted_packets);
                        printf("ratio:%f\n",discarded_packets/accepted_packets);
                        fflush(stdout);
                        return true;
                }

                /*
                 * Print the number of packets received by the root from each other node; do this once every 100 instants
                 * of virtual time
                 */

                if(((int)(((node_state*)snapshot)->lvt)%(100*time_factor)==0) &&( ((node_state*)snapshot)->lvt>1)){
                        printf("\n\nChecking packets collected at time %f\n",((node_state*)snapshot)->lvt);
                        for(i=0;i<n_prc_tot;i++) {
                                if(i==ctp_root)

                                        /*
                                         * Root node does not send packets, only collects them
                                         */

                                        continue;
                                printf("Packets from %d:%d\n",i,collected_packets_list[i]);
                        }

                        /*
                         * Increment the time factor, otherwise the information about packets collected so far would be
                         * printed more than once every 100 instants of virtual time (since the simulation time is of
                         * type "double")
                         */

                        time_factor++;
                        printf("\n");
                }

                /*
                 * If more than COLLECTED_DATA_PACKETS_GOAL have been received by each node (except the root itself)
                 * stop the simulation
                 */

                for(i=0;i<n_prc_tot;i++) {
                        if(i==ctp_root)

                                /*
                                 * Root node does not send packets, only collects them
                                 */

                                continue;
                        if (collected_packets_list[i] <COLLECTED_DATA_PACKETS_GOAL) {
                                fflush(stdout);
                                return false;
                        }
                }
                printf("\n\nSimulation stopped because at least %d packets have been collected from each node\n"
                               "Time:%f\nPackets collected by root:%f\n"
                        ,COLLECTED_DATA_PACKETS_GOAL,((node_state*)snapshot)->lvt,collected_packets);

                /*
                 * Print packets collected from each node
                 */

                for(i=0;i<n_prc_tot;i++) {

                        if(i==ctp_root)

                                /*
                                 * Root node does not send packets, only collects them
                                 */

                                continue;
                        printf("Packets from %d:%d\n",i,collected_packets_list[i]);
                }
                printf("discarded:%f\n",discarded_packets);
                printf("accepted:%f\n",accepted_packets);
                printf("ratio:%f\n",discarded_packets/accepted_packets);
                fflush(stdout);
                return true;
        }
        else{
                /*
                 * Other nodes are always ok with stopping simulation
                 */

                return true;
        }
}


/* SIMULATION API - start */

/*
 * WAIT TIME
 *
 * This function is used to simulate a timer: the logical process (node) has to wait for the given interval of time =>
 * it schedules an event that it itself will process after the given interval of time
 *
 * @me: ID of the node
 * @timestamp: virtual clock time when the timer will be fired
 * @type: ID corresponding to the event => it is necessary for the logical process for deciding what to do next
 */

void wait_until(unsigned int me,simtime_t timestamp,unsigned int type){

        /*
         * Schedule a new event after "interval" instants of virtual time; no parameters are provide with the event
         */

        ScheduleNewEvent(me,timestamp,type,NULL,0);
}

/*
 * BROADCAST EVENT
 *
 * Function invoked by the node, in particular, by its LINK ESTIMATOR layer, when it has to send a message to all the
 * other nodes in the sensors network, i.e. when the node has to send a beacon to its neighbors.
 *
 * @beacon: the message to be broadcasted
 * @time: virtual time when the message should be delivered
 */

void broadcast_message(ctp_routing_packet* beacon,simtime_t time) {

        /*
         * Index used to iterate through nodes of the network
         */

        unsigned char i;

        /*
         * Get the sender node: it's identity is reported in the physical overhead of the given packet
         */

        node src = beacon->phy_mac_overhead.src;

        /*
         * For each node (logical process) calculate the euclidean distance from the sender and, depending on this,
         * check whether it receives the message or not.
         */

        for (i = 0; i < n_prc_tot; i++) {

                /*
                 * Coordinates of the current node
                 */

                node_coordinates recipient_coordinates;

                /*
                 * State of the current node
                 */

                unsigned int recipient_state;

                /*
                 * Skip the sender of the message
                 */

                if (i == src.ID)
                        continue;

                /*
                 * Get coordinates of the current node from the dedicated list
                 */

                recipient_coordinates = nodes_coordinates_list[i];

                /*
                 * If the message can be received by the neighbor, according to the simulator, it will process an event
                 * of type "BEACON_RECEIVED" (unless it fails before)
                 */


                if (is_message_received(src.coordinates, recipient_coordinates)) {
                        ScheduleNewEvent(i, time + MESSAGE_DELIVERY_TIME, BEACON_RECEIVED, beacon,
                                         sizeof(ctp_routing_packet));
                }
        }
}

/*
 * UNICAST MESSAGE
 *
 * Function invoked by the node, in particular, by its FORWARDING ENGINE, when it has to a data packet to its parent or
 * an acknowledgment to one of its children
 *
 * @packet: the message to be sent (NULL in case of ack)
 * @time: virtual time of the node
 * @me: ID of the sender
 */

void unicast_message(ctp_data_packet* packet,simtime_t time, unsigned int me) {

        /*
         * Coordinates of the parent node
         */

        node_coordinates recipient_coordinates;

        /*
         * Get the sender node: it's identity is reported in the physical overhead of the given packet
         */

        node src = packet->phy_mac_overhead.src;

        /*
         * Get the recipient node: it's identity is reported in the physical overhead of the given packet
         */

        node dst = packet->phy_mac_overhead.dst;

        /*
         * Get coordinates of the current node from the dedicated list
         */

        recipient_coordinates = dst.coordinates;

        /*
         * If the message can be received by the parent according to the simulator, it will be delivered an event of type
         * "DATA_PACKET_RECEIVED"
         */

        if (is_message_received(src.coordinates,recipient_coordinates)) {
                ScheduleNewEvent(dst.ID, time + MESSAGE_DELIVERY_TIME, DATA_PACKET_RECEIVED, packet,
                                 sizeof(ctp_data_packet));
        }

        /*
         * Start a timer that will be fired after the maximum time for receiving an acknowledgment: at that moment, the
         * packet might have been already acknowledged or not, and this information is crucial to evaluate the outgoing
         * quality of the link to the parent. In order to check whether the ack has been received before the timeout,
         * the node compares the last packet in the output queue with the one provided with the event below: if they
         * are different, the packet has already been acknowledged before the timeout, otherwise the packet has not
         * been acknowledged.
         */

        ScheduleNewEvent(me,time+ACK_TIMEOUT_OFFSET,CHECK_ACK_RECEIVED,packet,sizeof(ctp_data_packet));
}

/*
 * SEND ACKNOWLEDGMENT
 *
 * After a node has successfully received a data packet, it replies with an acknowledgment: this may or may not be
 * received, depending on the interferences affecting the network. In the former case, the recipient of the ack is
 * delivered an event of type "ACK_RECEIVED"
 *
 * @sender_coordinates: coordinates of the node that is sending the ack
 * @recipient: the structure representing the node that is waiting for the ack
 * @time: virtual time of the node
 */

void send_ack(node_coordinates sender_coordinates,node recipient,simtime_t time){

        /*
         * If the ack is received, according to the simulator, schedule a new event of type "ACK_RECEIVED" for the
         * recipient
         */

        if (is_message_received(sender_coordinates,recipient.coordinates)) {
                ScheduleNewEvent(recipient.ID, time + MESSAGE_DELIVERY_TIME, ACK_RECEIVED, NULL,0);
        }
}

/* SIMULATION API - end */

/* SIMULATION FUNCTIONS - start */

/*
 * PARSE FILE CONTAINING THE TOPOLOGY OF THE NETWORK
 *
 * Read the file in order to determine the coordinates of the nodes in the sensors network; then store them in the
 * array "nodes_coordinates_list"
 *
 * NOTE: this function is executed only by the root node (either the one chosen by the user with the parameter "root" or
 * the default root)
 *
 * @path: filename of the file containing the topology of the network
 *
 * NOTE: THE FILE HAS TO BE IN THE SAME DIRECTORY AS THE MODEL BEING RUN
 */

void parse_topology(const char* path){

        /*
         * Number of current line in the configuration file; this is also used to check that the file contains the
         * coordinates for all the nodes of the simulation
         */

        unsigned int lines=0;

        /*
         * Parameters required only by the function "getline": if "lineptr" is set to NULL before the call and "n" is
         * set to 0, getline allocates a buffer for storing the line; "read" is the length of line
         */

        size_t len=0;
        char * lineptr=NULL;

        /*
         * Get the file object in READ_ONLY mode
         */

        file=fopen(path,"r");

        /*
         * Check if the file has been successfully opened: if not, exit with error
         */

        if(!file){
                printf("[FATAL ERROR] Provided path doesn't correspond to any file or it cannot be "
                               "accessed\n");
                exit(EXIT_FAILURE);
        }

        /*
         * The path is valid => allocate a new array of "public_node_state" with a number of elements equal to the
         * number of LPs
         */

        nodes_coordinates_list=malloc(sizeof(node_coordinates)*n_prc_tot);

        /*
         * Now read the file line by line and store the coordinates at line "i" at index "i" of the list; also
         * initialize the state of each element
         */

        while(getline(&lineptr,&len,file)!=-1){

                /*
                 * Structure representing the coordinates of the current node (logical process)
                 */

                node_coordinates node_coordinates;

                /*
                 * String representation of the x coordinate of a node
                 */

                char* x_coordinate;
                /*
                 * String representation of the y coordinate of a node
                 */

                char* y_coordinate;

                /*
                 * Check whether the number of lines read so far is greater than the number logical processes specified
                 * by the user => if so, stop parsing
                 */

                if(lines==n_prc_tot)
                        break;

                /*
                 * Parse the x coordinate
                 */

                x_coordinate=strtok(lineptr,",");

                /*
                 * Check if not null: if so, exit with error
                 */

                if(!x_coordinate) {
                        printf("[FATAL ERROR] Line %i of the file with the topology is not well formed\n", lines);
                        free(nodes_coordinates_list);
                        free(lineptr);
                        fclose(file);
                        exit(EXIT_FAILURE);
                }

                /*
                 * Translate x coordinate to int
                 */

                node_coordinates.x=atoi(x_coordinate);

                /*
                 * Parse the y coordinate
                 */

                y_coordinate=strtok(NULL,",");

                /*
                 * Check if not null: if so, exit with error
                 */

                if(!y_coordinate) {
                        printf("[FATAL ERROR] Line %i of the file with the topology is not well formed\n", lines);
                        free(nodes_coordinates_list);
                        free(lineptr);
                        fclose(file);
                        exit(EXIT_FAILURE);
                }

                /*
                 * Translate x coordinate to int
                 */

                node_coordinates.y=atoi(y_coordinate);

                /*
                 * The coordinates were correctly specified => store them in the list
                 */

                nodes_coordinates_list[lines]=node_coordinates;

                /*
                 * Increment the counter for lines read
                 */

                lines++;
        }

        /*
         * Close the file stream
         */

        fclose(file);

        /*
         * Remove the buffer allocated for the last line read
         */

        if(lineptr)
                free(lineptr);

        /*
         * If the number of lines in the configuration file is less than the number of logical processes, exit with
         * error
         */

        if(lines<n_prc_tot) {
                printf("[FATAL ERROR] Missing coordinates for %d node(s) in the file with the topology\n",
                       n_prc_tot-lines);
                free(nodes_coordinates_list);
                exit(EXIT_FAILURE);
        }
}

/*
 * CAN RECEIVE MESSAGE?
 *
 * This function determines, given the coordinates of two nodes, whether or not a message sent by one of the two is
 * received by the other one, and vice versa.
 *
 * In fact this models takes into account the fact that radio transceiver of sensor nodes has a limited coverage =>
 * a broadcast message will be received only by those nodes whose distance from the sender is within a certain bound.
 *
 * This simulation model adopts the QUASI UNIT DISK GRAPH to represent this fact.
 * Nodes can receive messages from their "neighbor nodes" => two nodes A and B are neighbors if the euclidean distance
 * between them is less than or equal to "r" (simulation parameter).
 * If their distance is less than or equal to "p" (simulation parameter), with p in the interval (0,r], messages sent
 * by A to B and by B to A are certainly delivered.
 * If their distance is in the range (p,r], messages sent by A to B and by B to A may or may not be delivered
 * => for two such nodes this function may return different values every time it is executed; anyway, the closer the two
 * nodes, the more likely that messages are delivered.
 *
 * @a: coordinates of the sender
 * @b: coordinates of the recipient
 *
 * Returns true if a message sent by one of the two nodes can be received by the other one, false otherwise
 */

bool is_message_received(node_coordinates a,node_coordinates b){

        /*
         * Get the euclidean distance between the nodes
         */

        double distance=euclidean_distance(a,b);

        /*
         * Check that the distance is not null: if so, it means that two nodes with different IDs have the same
         * coordinates => there's an error in the configuration file => exit with error
         */

        if(!distance){
                printf("[FATAL ERROR] Two different nodes have the same coordinates => fix the coordinates in "
                               "the configuration file\n");
                printf("distance is:%f\n",distance);
                fflush(stdout);
                exit(EXIT_FAILURE);
        }

        /*
         * Check the distance and decide how to proceed
         */

        if(distance>NEIGHBORS_MAX_DISTANCE) {

                /*
                 * Nodes are not neighbors => no way a message can be received => return false
                 */

                return false;

        }
        else{

                /*
                 * Nodes are neighbors => if the distance is less than NEIGHBORS_SAFE_DISTANCE, the message is delivered
                 * to the recipient
                 */

                if(distance<=NEIGHBORS_SAFE_DISTANCE)
                        return true;
                else{

                        /*
                         * Choose a random number in the range [0,NEIGHBORS_MAX_DISTANCE-NEIGHBORS_SAFE_DISTANCE]
                         * and add it to the current distance
                         */

                        distance+=RandomRange(0,NEIGHBORS_MAX_DISTANCE-NEIGHBORS_SAFE_DISTANCE);

                        /*
                         * If the distance is not beyond NEIGHBORS_MAX_DISTANCE the message is delivered and true is
                         * returned; otherwise false is returned
                         *
                         * NOTE: closer neighbors have higher likelihood to receive the message
                         */

                        fflush(stdout);
                        if(distance<NEIGHBORS_MAX_DISTANCE) {
                                accepted_packets++;
                                return true;
                        }
                        else {
                                discarded_packets++;
                                return false;
                        }
                }

        }
}

/*
 * CHECK IF FAILED
 *
 * Nodes can fail, so they are associated with an exponential failure distribution: this tells at every instant of time,
 * the probability that a failure occurred and it has the form 1-e^-(lambda*t); as time goes by, failure probability
 * increases.
 * This function is needed by the simulation to decide whether the node is running or not at any instant of virtual
 * time: it evaluates the failure probability, adds a random bias and returns false (node failed) if the result is
 * bigger than or equal to the threshold "failure_threshold",true (node still running) otherwise.
 * The random bias is introduced in order to avoid that all the nodes fail at the same time, which is not realistic and
 * would not properly simulate failure of devices
 *
 * @now: actual value of the virtual clock
 */


bool is_failed(simtime_t now){

        /*
         * Probability of failure according to the failure distribution
         */

        double probability;

        /*
         * Make the failure event a little bit random, simulating the fact that nodes don't usually fail exactly when
         * they are supposed to
         */

        double bias;

        /*
         * Depending on the sign of the bias applied to the probability, a node may fail earlier or later than what it
         * was supposed to
         */

        int bias_sign;

        /*
         * Skip the check at time 0, before the node are started
         */

        if(!now)
                return false;

        /*
         * Evaluate the probability that a failure has occurred at time "now".
         */

        probability=1-exp(-(now*failure_lambda));

        /*
         * Get a random bias in the range [-0.2,0.2]
         */

        bias=fmod(Random(),0.2);

        /*
         * Get the sign of the bias
         */

        bias_sign=RandomRange(-1,1);

        /*
         * Avoid a null bias
         */

        while(!bias_sign)
                bias_sign=RandomRange(-1,1);

        /*
         * Apply sign to the bias
         */

        bias*=bias_sign;

        /*
         * Add a random bias
         */

        probability+=bias;

        /*
         * If the probability is beyond the failure threshold, return true, otherwise return false
         */

        if(probability>=failure_threshold) {
                return true;
        }
        return false;
}

/*
 * EUCLIDEAN DISTANCE
 *
 * Return the euclidean distance between given nodes
 */

double euclidean_distance(node_coordinates a,node_coordinates b){

        /*
         * Difference between x coordinates of the nodes
         */

        int x_difference=a.x-b.x;

        /*
         * Difference between y coordinates of the nodes
         */

        int y_difference=a.y-b.y;

        /*
         * Return euclidean distance
         */

        return sqrt(x_difference*x_difference+y_difference*y_difference);
}

/*
 * ROOT RECEIVED PACKET
 *
 * When the root node receives a packet, the counter corresponding to the ID of the sender is incremented
 *
 * @packet: pointer to the packet received by the root
 */

void collected_data_packet(ctp_data_packet* packet){
        collected_packets++;
        if(packet->data_packet_frame.origin)
                collected_packets_list[packet->data_packet_frame.origin]+=1;
}

/* SIMULATION FUNCTIONS - end */

