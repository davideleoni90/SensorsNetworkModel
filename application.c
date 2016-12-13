#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "application.h"

unsigned char collected_packets; // The number of packets successfully delivered by the root of the collection tree

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
                         * Coordinates of the the node
                         */

                        node_coordinates coordinates;

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

                        /* GET PARAMETERS OF THE SIMULATION - end */

                        /* SET PARAMETERS OF THE SIMULATION - start */

                        /*
                         * Ask the simulator for the coordinates of this node
                         */

                        get_coordinates();

                        /* SET PARAMETERS OF THE SIMULATION - end */

                        /*
                         * Initialize the LINK ESTIMATOR => set the sequence number of the beacons to 0
                         */

                        state->beacon_sequence_number=0;

                        /*
                         * Initialize the ROUTING ENGINE
                         */



                        // Show current configuration, only once
                        if(me == 0) {
                                printf("CURRENT CONFIGURATION:\n");
                        }

                        // Start the simulation
                        timestamp = (simtime_t) (20 * Random());
                        ScheduleNewEvent(me, timestamp, START_CALL, NULL, 0);

                        // If needed, start the first fading recheck
//			if (state->fading_recheck) {
                        timestamp = (simtime_t) (FADING_RECHECK_FREQUENCY * Random());
                        ScheduleNewEvent(me, timestamp, FADING_RECHECK, NULL, 0);
//			}

                        break;

                case UPDATE_ROUTE_TIMER_FIRED:

                        /*
                         * It's time for the ROUTING ENGINE to update the route of the node => invoke the dedicated
                         * function.
                         */

                        update_route();

                        /*
                         * The time simulated through this event is periodic => schedule this event after the same amount
                         * of time, starting from now
                         */

                        wait_time(UPDATE_ROUTE_TIMER,UPDATE_ROUTE_TIMER_FIRED);
                        break;

                case SEND_BEACONS_TIMER_FIRED:

                        /*
                         * It's time for the ROUTING ENGINE to send a beacon to its neighbors => before doing this,
                         * update the route, so that information reported in the beacon will not be obsolete
                         */

                        update_route();

                        /*
                         * Now send the beacon
                         */

                        send_beacon();

                        /*
                         * The interval of the timer that schedules the sending of beacons is continuously changing, in
                         * such a way that beacons are sent with decreasing frequency => schedule an update of the
                         * timer, i.e. advance in the virtual time until the moment when the timer has to be updated
                         */

                        schedule_beacons_interval_update();
                        break;

                case SET_BEACONS_TIMER:

                        /*
                         * This event is processed when the interval associated to the timer for beacons has to be
                         * updated
                         */

                        double_beacons_send_interval();
                        break;

                default:
                        fprintf(stdout, "PCS: Unknown event type! (me = %d - event type = %d)\n", me, event_type);
                        abort();

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

        if(root){
                if(collected_packets>=COLLECTED_DATA_PACKETS_GOAL)
                        return true;
        }

        /*
         * The node is not the root or is the root but the number of data packets collected is not yet sufficient to
         * stop the simulation
         */

        return false;
}

/* SIMULATION API - start */

/*
 * WAIT TIME
 *
 * This function is used to simulate a timer: the logical process (node) has to wait for the given interval of time =>
 * it schedules an event that it itself will process after the given interval of time
 *
 * @interval: the interval of time the logical process has to wait for
 * @type: ID corresponding to the event => it is necessary for the logical process for deciding what to do next
 */

void wait_time(simtime_t interval,unsigned int type){

        /*
         * Schedule a new event after "interval" instants of virtual time; no parameters are provide with the event
         */

        ScheduleNewEvent(me.ID,timestamp+interval,type,NULL,0);
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
 * Read the configuration file in order to determine:
 *
 * 1-the number of nodes involved in the simulation of the sensors network
 * 2-the coordinates of the nodes
 *
 * @path: filename of the configuration file
 *
 * Returns the number of nodes parsed from the configuration file
 *
 * NOTE: THE CONFIGURATION FILE HAS TO BE IN THE SAME DIRECTORY AS THE MODEL BEING RUN
 */

unsigned int parse_configuration_file(const char* path){

        /*
         * File pointer object of the configuration file
         */

        FILE* file;

        /*
         * Get the file object in READ_ONLY mode
         */

        file=fopen(path,"r");

        /*
         * Check if the file has been successfully opened: if not, exit with error
         */
}

/*
 * GET COORDINATES
 *
 * Randomly assign coordinates to the node
 */

void get_coordinates(node_coordinates* coordinates){

}
/* SIMULATION FUNCTIONS - end */

