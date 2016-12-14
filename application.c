#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ROOT-Sim.h>
#include "application.h"

/* GLOBAL VARIABLES (shared among all logical processes) - start */

unsigned char collected_packets=0; // The number of packets successfully delivered by the root of the collection tree
FILE* file; // Pointer to the file object associated to the configuration file
void parse_configuration_file(const char* path);

/*
 * Pointer to the dynamically allocated array containing the list of the the coordinates of the nodes in the network
 * (length = n_proc_tot, the number of logical processes in the simulation) indexed according to the ID of the node;
 * the coordinates of the nodes are read from a configuration file decided by the user.
 * Within the INIT event, each logical process parses the configuration file to fill the list of nodes.
 * TODO check if this can be made more efficiently
 */

node_coordinates* nodes_list;

/* GLOBAL VARIABLES (shared among all logical processes) - end */

void ProcessEvent(unsigned int me, simtime_t now, int event_type, void *event_content, unsigned int size, void *ptr) {

        /*
         * Pointer to the object representing the state of this logical process (node)
         */

        node_state *state;

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
         * Depending on the event type, perform different tasks
         */

        switch(event_type) {

                case INIT:


                        /*
                         * NODE INITIALIZATION
                         *
                         * This is the default event signalled by the simulator to each logical process => it triggers
                         * the initialization phase.
                         *
                         * In this phase, the parameters of the simulation should be parsed but, most importantly, a new
                         * state object is dynamically allocated and its address is communicated to the simulator by
                         * mean of the API function "SetState" => in this way the simulator is aware of the memory
                         * address of the state object of processes, so it can transparently bring it back to a previous
                         * configuration in case of inconsistency problems.
                         *
                         * Besides this operations, which are common to any model, this specific model requires the
                         * initialization of the Collection Tree Protocol (CTP) stack.
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

                        /* GET PARAMETERS OF THE SIMULATION - start */

                        /*
                         * Parse the configuration file: if this is not specified, return with error
                         */

                        if(IsParameterPresent(event_content, "path"))
                                parse_configuration_file(GetParameterString(event_content,"path"));
                        else{
                                printf("[FATAL ERROR] The path a to configuration file is mandatory => specify it after "
                                               "the argument \"path\"\n");
                                free((state));
                                exit(EXIT_FAILURE);
                        }

                        /* GET PARAMETERS OF THE SIMULATION - end */

                        /* SET CTP STACK - start */

                        /*
                         * Initialize the LINK ESTIMATOR => set the sequence number of the beacons to 0
                         */

                        state->beacon_sequence_number=0;

                        /*
                         * Initialize the ROUTING ENGINE
                         */



                        /* SET CTP STACK - start */

                        // Start the simulation
                        //timestamp = (simtime_t) (20 * Random());
                        //ScheduleNewEvent(me, timestamp, START_CALL, NULL, 0);

                        // If needed, start the first fading recheck
//			if (state->fading_recheck) {
                        //timestamp = (simtime_t) (FADING_RECHECK_FREQUENCY * Random());
                        ScheduleNewEvent(me, now+1, 0, NULL, 0);
//			}



                        break;

                case UPDATE_ROUTE_TIMER_FIRED:

                        /*
                         * It's time for the ROUTING ENGINE to update the route of the node => invoke the dedicated
                         * function.
                         */

                        update_route(state);

                        /*
                         * The time simulated through this event is periodic => schedule this event after the same amount
                         * of time, starting from now
                         */

                        //wait_time(UPDATE_ROUTE_TIMER,UPDATE_ROUTE_TIMER_FIRED);
                        break;

                case SEND_BEACONS_TIMER_FIRED:

                        /*
                         * It's time for the ROUTING ENGINE to send a beacon to its neighbors => before doing this,
                         * update the route, so that information reported in the beacon will not be obsolete
                         */

                        //update_route();

                        /*
                         * Now send the beacon
                         */

                        //send_beacon();

                        /*
                         * The interval of the timer that schedules the sending of beacons is continuously changing, in
                         * such a way that beacons are sent with decreasing frequency => schedule an update of the
                         * timer, i.e. advance in the virtual time until the moment when the timer has to be updated
                         */

                        //schedule_beacons_interval_update();
                        break;

                case SET_BEACONS_TIMER:

                        /*
                         * This event is processed when the interval associated to the timer for beacons has to be
                         * updated
                         */

                        //double_beacons_send_interval();
                        break;

                default:
                        //fprintf(stdout, "PCS: Unknown event type! (me = %d - event type = %d)\n", me, event_type);
                        //abort();
                        collected_packets++;

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
 * root node has received a number of data packets greater than or equal to COLLECTED_DATA_PACKETS_GOAL.
 * As a consequence, all the logic processes associated to nodes of the collection tree will always return true here,
 * while the logic process associated to the root will return true iff the number of data packets collected is greater
 * than or equal to COLLECTED_DATA_PACKETS_GOAL
 *
 */


bool OnGVT(unsigned int me, void*snapshot) {

        /*
         * If the current node is the root of the collection tree, check the number of data packets received
         */

        /*if(root){
                if(collected_packets>=COLLECTED_DATA_PACKETS_GOAL)
                        return true;
        }*/

        /*
         * The node is not the root or is the root but the number of data packets collected is not yet sufficient to
         * stop the simulation
         */
        return true;
}

/* SIMULATION API - start */

/*
 * WAIT TIME
 *
 * This function is used to simulate a timer: the logical process (node) has to wait for the given interval of time =>
 * it schedules an event that it itself will process after the given interval of time
 *
 * @me: ID ofe the node
 * @timestamp: virtual clock time when the timer will be fired
 * @type: ID corresponding to the event => it is necessary for the logical process for deciding what to do next
 */

void wait_time(unsigned int me,simtime_t timestamp,unsigned int type){

        /*
         * Schedule a new event after "interval" instants of virtual time; no parameters are provide with the event
         */

        ScheduleNewEvent(me,timestamp,type,NULL,0);
}

/*
 * DATA PACKET COLLECTED
 *
 * This function is called by the root of the collection tree when it receives a data packet => update the counter of the
 * message successfully collected.
 *
 * When the number of packet successfully collected reaches the planned value, the simulation stops
 */

/* SIMULATION API - end */

/* SIMULATION FUNCTIONS - start */

/*
 * PARSE CONFIGURATION FILE
 *
 * Read the configuration file in order to determine the coordinates of the nodes in the sensors netwoek and store them
 * in the dedicated list of coordinates
 *
 * NOTE: this function is executed by each logical process
 * TODO check if we can it be done only once in a thread-safe manner
 *
 * @path: filename of the configuration file
 *
 * NOTE: THE CONFIGURATION FILE HAS TO BE IN THE SAME DIRECTORY AS THE MODEL BEING RUN
 */

void parse_configuration_file(const char* path){

        /*
         * Number of current line in the configuration file; this is also used to check that the configuration file
         * contains the coordinates for all the nodes of the simulation
         */

        unsigned int lines=0;

        /*
         * Parameters required only by the function "getline": if "lineptr" is set to NULL before the call and "n" is
         * set to 0, getline allocates a buffer for storing the line; "read" is the length of line
         */

        ssize_t len=0;
        char * lineptr=NULL;

        /*
         * Get the configuration file object in READ_ONLY mode
         */

        file=fopen(path,"r");

        /*
         * Check if the file has been successfully opened: if not, exit with error
         */

        if(!file){
                printf("[FATAL ERROR] Provided path doesn't correspond to any configuration file or it cannot be accessed\n");
                exit(EXIT_FAILURE);
        }

        /*
         * The path is valid => allocate a new array of "node_coordinates" with a number of elements equal to the number
         * of logical processes; we use reallocation instead of allocation in order not to waste memory possibly
         * allocated by other logical processes other than the current one
         */

        nodes_list=realloc(nodes_list,sizeof(node_coordinates)*n_prc_tot);

        /*
         * Now read the file line by line and store the coordinates at line "i"at index "i" of the list
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
                 * DEBUG PRINT
                 */

                //printf("Line %d: %s\n",lines,lineptr);

                /*
                 * Parse the x coordinate
                 */

                x_coordinate=strtok(lineptr,",");

                /*
                 * Check if not null: if so, exit with error
                 */

                if(!x_coordinate) {
                        printf("[FATAL ERROR] Line %i of the configuration file is not well formed\n", lines);
                        free(nodes_list);
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
                        printf("[FATAL ERROR] Line %i of the configuration file is not well formed\n", lines);
                        free(nodes_list);
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

                nodes_list[lines]=node_coordinates;

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
                printf("[FATAL ERROR] Missing coordinates for %d node(s) in the configuration file\n",n_prc_tot-lines);
                free(nodes_list);
                exit(EXIT_FAILURE);
        }
}

/* SIMULATION FUNCTIONS - end */

