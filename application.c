#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ROOT-Sim.h>
#include "application.h"
#include "forwarding_engine.h"
#include "wireless_links.h"
#include "link_layer.h"
#include <math.h>
#include <limits.h>

/* GLOBAL VARIABLES (shared among all logical processes) - start */

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

extern gain_entry* gains_list;
extern noise_entry* noise_list;

/* GLOBAL VARIABLES (shared among all logical processes) - end */

/* FORWARD DECLARATIONS */

void read_input_file(const char* path);
void start_routing_engine(node_state* state);
bool is_failed(simtime_t now);
void new_pending_transmission(node_state* state, double gain, unsigned char type,void* frame,double duration);
void transmission_finished(node_state* state,pending_transmission* finished_transmission);

/*
 * Application-level callback: this is the interface between the simulator and the model being simulated
 */

void ProcessEvent(unsigned int me, simtime_t now, int event_type, void *event_content, unsigned int size, void *ptr) {

        /*
         * Pointer to the object representing the state of this logical process (node)
         */

        node_state *state;

        /*
         * Index to iterate through nodes
         */

        unsigned int i;

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
                         * Before the simulation can start, it is necessary to read the input file provided by the
                         * user, containing the description of all the links of the node, the IDs of the nodes and their
                         * level of local noise => since this implies dynamic memory allocation, only one node (the one
                         * with ID 0 or the one chosen by the user) performs this task, so the actual start of the other
                         * nodes has to be deferred
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

                                unsigned int root=(unsigned int)GetParameterInt(event_content,"root");

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

                                /* READ INPUT FILE (ONLY THE ROOT NODE) - start */

                                /*
                                 * Parse the input file containing all the links of the network, including their gains,
                                 * and the noise affecting all the nodes: if the path to the file is not given, return
                                 * with error
                                 */

                                if(IsParameterPresent(event_content, "input")) {
                                        read_input_file(GetParameterString(event_content, "input"));
                                }
                                else{
                                        printf("[FATAL ERROR] The path a to file containing the topology of the network"
                                                       "is mandatory => "
                                                       "specify it after "
                                                       "the argument \"path\"\n");
                                        free((state));
                                        exit(EXIT_FAILURE);
                                }

                                /* READ INPUT FILE (ONLY THE ROOT NODE) - end */

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
                         * First store ID in the state
                         */

                        state->me=me;

                        /* INIT PHYSICAL LAYER - start */

                        init_physical_layer(state);

                        /* INIT PHYSICAL LAYER - end */

                        /* INIT LINK LAYER - start */

                        init_link_layer(state);

                        /* INIT LINK LAYER - end */

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

                /*
                 *
                 *
                 * EVENTS SENT BY THE ROUTING ENGINE - start
                 *
                 *
                 */

                case UPDATE_ROUTE_TIMER_FIRED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * It's time for the ROUTING ENGINE to update the route of the node => invoke the
                                 * dedicated function.
                                 */

                                update_route(state);

                                /*
                                 * The time simulated through this event is periodic => schedule this event after the
                                 * same amount of time, starting from now
                                 */

                                wait_until(me, now + ((UPDATE_ROUTE_TIMER/1000)*TICKS_PER_SEC),
                                           UPDATE_ROUTE_TIMER_FIRED);
                        }
                        break;

                case SEND_BEACONS_TIMER_FIRED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * It's time for the ROUTING ENGINE to send a beacon to its neighbors => before doing
                                 * this, update the route, so that information reported in the beacon will not be
                                 * obsolete
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

                /*
                 *
                 *
                 * EVENTS SENT BY THE ROUTING ENGINE - end
                 *
                 *
                 */

                /*
                 *
                 *
                 * EVENTS SENT BY THE FORWARDING ENGINE - start
                 *
                 *
                 */

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

                /*
                 *
                 *
                 * EVENTS SENT BY THE FORWARDING ENGINE - end
                 *
                 *
                 */

                /*
                 *
                 * EVENTS SENT BY THE LINK LAYER - start
                 *
                 *
                 */

                case CHECK_CHANNEL_FREE:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * This event is delivered to a node by itself when it has to check whether the channel
                                 * is free after the backoff time.
                                 * The handler of this event has to perform this check and then, depending on how the
                                 * parameters of the CSMA protocol have been set and whether the channel is free or not,
                                 * either it starts to transmit the frame over the link or schedules a new check after
                                 * another backoff time
                                 */

                                check_channel(state);
                        }
                        break;

                case START_FRAME_TRANSMISSION:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * This event is delivered to a node by itself when it has to start transmitting a frame
                                 */

                                start_frame_transmission(state);
                        }
                        break;

                case FRAME_TRANSMITTED:

                        /*
                         * If the node is not running, do nothing
                         */

                        if(state->state&RUNNING) {

                                /*
                                 * This event is delivered to a node by itself when the transmission of a frame is over:
                                 * if the frame contained a data packet, the node has to check whether this hase been
                                 * acknowledged
                                 */

                                frame_transmitted(state);
                        }
                        break;

                /*
                 *
                 * EVENTS SENT BY THE LINK LAYER - end
                 *
                 *
                 */

                /*
                 *
                 * EVENTS SENT BY THE PHYSICAL LAYER - start
                 *
                 *
                 */

                case BEACON_TRANSMISSION_STARTED:

                        /*
                         * There's a new incoming frame destined to the current node => determine if the node is
                         * capable of receiving the frame. Even though this is not the case, keep track of the
                         * associated transmission because it's sensed by the radio transceiver of the node and it may
                         * interfere with other frames transmissions.
                         */

                        new_pending_transmission(state,((ctp_routing_packet*)event_content)->link_frame.gain,
                                                 CTP_BEACON,event_content,
                                                 ((ctp_routing_packet*)event_content)->link_frame.duration);

                        break;

                case TRANSMISSION_FINISHED:

                        /*
                         * The transmission of the beacon has come to an end => the associated signal is no longer
                         * perceived by the node. If the signal was strong enough, the node received the frame and
                         * starts processing it.
                         */

                        transmission_finished(state,(pending_transmission*)event_content);
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

                        /*
                         *
                         * EVENTS SENT BY THE PHYSICAL LAYER - end
                         *
                         *
                         */

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


/* SIMULATION FUNCTIONS - start */

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
 * READ INPUT FILE
 *
 * Read the input file containing the list of the links between nodes, including the gain of each wireless link; also
 * the file has to provide the noise floor and white gaussian noise affecting each node.
 * For each wireless link there has to be a line with this syntax:
 *
 * "gain"\t source_node_id\t sink_node_id\t link_gain\n
 *
 * For each node in the network there has to be a line with this syntax:
 *
 * "noise"\t node_id\t noise_floor\t gaussian_white_noise\n
 *
 * Such file can be easily generated using the "LinkLayerModel" from the TOSSIM simulator.
 * Being n the number of LPs of the simulation, this function reads at most n(n-1) lines starting with the word "gain"
 * and up to n lines starting with the word "noise". The file has to provide the description of at least one link for
 * each of the n nodes in the simulation, while the description of the noise is not mandatory: if the value of the noise
 * for a node is missing, a default value is used.
 *
 * @path: filename of the input file
 *
 * NOTE: THE FILE HAS TO BE IN THE SAME DIRECTORY AS THE MODEL BEING RUN
 */

void read_input_file(const char* path){

        /*
         * Number of current line read from the file
         */

        unsigned int lines=0;

        /*
         * Parameters required only by the function "getline": if "lineptr" is set to NULL before the call and "len" is
         * set to 0, getline allocates a buffer for storing the line
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
         * The path is valid => allocate a new array of pointers to "gain_entry" with a number of elements equal to the
         * number of LPs: each element of the array contains the list of the links of the node, including the value of
         * their associated gains
         */

        gains_list=malloc(sizeof(gain_entry*)*n_prc_tot);

        /*
         * Initialize pointers to null
         */

        for(int i=0;i<n_prc_tot;i++)
                gains_list[i]=NULL;

        /*
         * Allocates an array containing an instance of type "noise_entry" for each node: this will be initialized to
         * the values of noise read from the input file; in case no value is specified for a node, the default value of
         * noise is used
         */

        noise_list=malloc(sizeof(noise_entry)*n_prc_tot);

        /*
         * Now read the file line by line and store the values for gain and noise in the corresponding lists
         */

        while(getline(&lineptr,&len,file)!=-1){

                /*
                 * Array of tokens after the first word in a line. In case of line describing the noise, they are:
                 *
                 * 1-ID of the node
                 * 2-node's noise floor
                 * 3-node's white gaussian noise
                 *
                 * In case of line describing a link, they are:
                 *
                 * 1-ID of the source node of the link
                 * 2-ID of the sink node of the link
                 * 3-gain of the link
                 * 4-length of the link
                 */

                double tokens[3];

                /*
                 * Index variable for the remaining tokens
                 */

                unsigned char index=0;

                /*
                 * Get the first word in the line: it has to be either "gain" or "noise"
                 */

                char* line_type=strtok(lineptr,"\t");

                /*
                 * Check if the first word of the line is valid: if not, abort simulation
                 */

                if((!strcmp(line_type,"gain"))&&(!strcmp(line_type,"noise"))){
                        printf("[FATAL ERROR] Line %i of the file is not well formed\n"
                                       "It has to start with either \"noise\" or \"gain\"\n", lines);
                        fclose(file);
                        exit(EXIT_FAILURE);
                }

                /*
                 * Get further three tokens
                 */

                while(index<3){

                        /*
                         * Get the next token
                         */

                        char* token=strtok(lineptr,"\t");

                        /*
                         * Check if valid: if not, abort simulation
                         */

                        if(!token){
                                printf("[FATAL ERROR] Line %i of the file is not well formed\n", lines);
                                fclose(file);
                                exit(EXIT_FAILURE);
                        }

                        /*
                         * Store token
                         */

                        sscanf(token,"%lf",&tokens[index]);

                        /*
                         * Increment the index
                         */

                        index++;
                }

                /*
                 * Depending on the type of line, fill different lists
                 */

                if(strcmp(line_type,"gain")){

                        /*
                         * The length of the link to be parsed
                         */

                        double length;

                        /*
                         * Get the last token (length of the link)
                         */

                        char* token=strtok(lineptr,"\t");

                        /*
                         * Check if the last token is valid too
                         */

                        if(!token){
                                printf("[FATAL ERROR] Line %i of the file is not well formed\n", lines);
                                fclose(file);
                                exit(EXIT_FAILURE);
                        }

                        /*
                         * Convert the token into a double
                         */

                        sscanf(token,"%lf",&length);

                        /*
                         * The current line describes the gain of a link
                         */

                        add_gain_entry((unsigned  int)tokens[0],(unsigned  int)tokens[1],tokens[2],length);
                }
                else{

                        /*
                         * The current line describes the noise of a node
                         */

                        add_noise_entry((unsigned  int)tokens[0],tokens[1],tokens[2]);
                }

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
         * Check that there's at least one link for each node by computing the difference between the number of elements
         * in the gain list and the variable n_proc_tot: if not zero, abort simulation
         */

        if(n_prc_tot-get_nodes()) {
                printf("[FATAL ERROR] Missing link for %d node(s) in the file\n",
                       n_prc_tot-get_nodes());
                exit(EXIT_FAILURE);
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

