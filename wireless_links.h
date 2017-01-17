#ifndef SENSORSNETWORKMODELPROJECT_WIRELESS_LINKS_H
#define SENSORSNETWORKMODELPROJECT_WIRELESS_LINKS_H

#include <stdbool.h>
#include "application.h"

/*
 * WIRELESS LINKS CONSTANTS
 */

enum{
        WHITE_NOISE_MEAN=0, // The white noise has a gaussian distribution with the mean value given by this parameter

        /*
         * If the strength of the signal perceived is below this threshold, the channel is considered free; the value
         * of this constant is the same used for the CC2420 radio
         */

        CHANNEL_FREE_THRESHOLD=-95.0

};

void init_physical_layer(node_state* state);
pending_transmission* add_pending_transmission(node_state* state, unsigned char type,void* frame, double power,
                                               pending_transmission* last);
void add_gain_entry(unsigned int source, unsigned int sink, double gain, double length);
void add_noise_entry(unsigned int node, double noise_floor, double white_noise);
unsigned int get_nodes();
double compute_signal_strength(node_state* state);
bool is_channel_free(node_state* state);
void transmit_frame(node_state* state,unsigned char type);
void transmission_finished(node_state* state,pending_transmission* finished_transmission);
#endif //SENSORSNETWORKMODELPROJECT_WIRELESS_LINKS_H
