#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ROOT-Sim.h>
#include "application.h"
#include "routing_engine.h"

simtime_t timestamp; // Value of the local virtual clock
node me; // ID and coordinates of this node (logical process)
unsigned char collected_packets; // The number of packets successfully delivered by the root of the collection tree
bool root; // Boolean variable that is set to true if the node is designated root of the collection tree

void ProcessEvent(unsigned int me, simtime_t now, int event_type, void *event_content, unsigned int size, void *ptr) {

        /*
         * Update the virtual clock of this logical process (node)
         */

        timestamp=now;

        lp_state_type *state;
        state = (lp_state_type*)ptr;
        switch(event_type) {

                case INIT:

                        // Initialize the LP's state
                        state = (lp_state_type *)malloc(sizeof(lp_state_type));
                        if (state == NULL){
                                printf("Out of memory!\n");
                                exit(EXIT_FAILURE);
                        }

                        SetState(state);

                        bzero(state, sizeof(lp_state_type));
                        state->channel_counter = CHANNELS_PER_CELL;

                        // Read runtime parameters
                        if(IsParameterPresent(event_content, "pcs_statistics"))
                                pcs_statistics = true;

                        if(IsParameterPresent(event_content, "ta"))
                                state->ref_ta = state->ta = GetParameterDouble(event_content, "ta");
                        else
                                state->ref_ta = state->ta = TA;

                        if(IsParameterPresent(event_content, "ta_duration"))
                                state->ta_duration = GetParameterDouble(event_content, "ta_duration");
                        else
                                state->ta_duration = TA_DURATION;

                        if(IsParameterPresent(event_content, "ta_change"))
                                state->ta_change = GetParameterDouble(event_content, "ta_change");
                        else
                                state->ta_change = TA_CHANGE;

                        if(IsParameterPresent(event_content, "channels_per_cell"))
                                state->channels_per_cell = GetParameterInt(event_content, "channels_per_cell");
                        else
                                state->channels_per_cell = CHANNELS_PER_CELL;

                        if(IsParameterPresent(event_content, "complete_calls"))
                                complete_calls = GetParameterInt(event_content, "complete_calls");

                        state->fading_recheck = IsParameterPresent(event_content, "fading_recheck");
                        state->variable_ta = IsParameterPresent(event_content, "variable_ta");


                        // Show current configuration, only once
                        if(me == 0) {
                                printf("CURRENT CONFIGURATION:\ncomplete calls: %d\nTA: %f\nta_duration: %f\nta_change: %f\nchannels_per_cell: %d\nfading_recheck: %d\nvariable_ta: %d\n",
                                       complete_calls, state->ta, state->ta_duration, state->ta_change, state->channels_per_cell, state->fading_recheck, state->variable_ta);
                                fflush(stdout);
                        }

                        state->channel_counter = state->channels_per_cell;

                        // Setup channel state
                        state->channel_state = malloc(sizeof(unsigned int) * 2 * (CHANNELS_PER_CELL / BITS + 1));
                        for (w = 0; w < state->channel_counter / (sizeof(int) * 8) + 1; w++)
                                state->channel_state[w] = 0;

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

/*
 * SIMULATION API - start
 */

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

/*
 * SIMULATION API - start
 */

