#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ROOT-Sim.h>
#include "application.h"

simtime_t timestamp; // Value of the local virtual clock

void ProcessEvent(unsigned int me, simtime_t now, int event_type, void *event_content, unsigned int size, void *ptr) {

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


                default:
                        fprintf(stdout, "PCS: Unknown event type! (me = %d - event type = %d)\n", me, event_type);
                        abort();

        }
}


bool OnGVT(unsigned int me, lp_state_type *snapshot) {

        return true;
}

