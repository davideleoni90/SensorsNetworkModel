#include <bits/mathcalls.h>
#include "wireless_links.h"
#include "link_estimator.h"
#include "application.h"

/*
 * WIRELESS LINKS MODEL
 *
 * This piece of code models the behaviour of wireless links. This depends on two elements:
 *
 * 1-the radio
 * 2-the surrounding environment (channel)
 *
 * Because of the limited GAIN of the radio featured by the nodes and because of the NOISE associated to the environment,
 * a packet sent from one node to another can't always be received => before a packet is delivered to another node, i.e.
 * it receives an event, the simulation has to check that the packet can actually be received by the recipient and this
 * module performs this check.
 * The interferences of the environment (noise) are simulated using an additive signal strength model: the power of the
 * signal through the channel is the sum of the gain associated to all the packets sent and not yet received in the
 * network => a channel is considered free only if the total power of the signal representing such a noise is below a
 * threshold.
 */

/*
 * GAIN ADJACENCY LIST
 *
 * Array of unsorted lists, one for each node: the list contains the gain of all the wireless links having the node as
 * source node.
 * This data structure is dynamically allocated before the simulation starts, reading the values for the gain from the
 * INPUT FILE.
 */

gain_entry** gains_list;

/*
 * NOISE FLOOR LIST
 *
 * Dynamically allocated array containing the representation of the noise for each node node in the network.
 * It has a static component (the noise floor), and a dynamic component, because of the thermal noise, referred to as
 * white gaussian noise. The latter is modelled as a gaussian random variable, having mean 0 and whose standard deviation
 * is read from the input file, together with the value of the noise floor.
 */

noise_entry* noise_list;

/*
 * PENDING TRANSMISSIONS POWER
 *
 * This variable holds the sum of the power of signals associated to all the packets that are currently being transmitted
 * as electromagnetic waves through the network. This value varies over time: as soon as a packet is received, its
 * signal disappears and the corresponding power is subtracted from the sum.
 * When a node checks whether the channel is free, it checks whether the power of the signal resulting from all the
 * ongoing transmission is below a threshold: if so, the channel is regarded as free, otherwise it is regarded as busy
 * and the node backs off
 */

double pending_transmissions_power=0;

extern FILE* file;

/*
 * ADD GAIN ENTRY
 *
 * Add a new element in the list associated to the given source node.
 *
 * @source: ID of the source node of the link
 * @sink: ID of the sink node of the link
 * @gain: gain of the link
 * @length: euclidean distance between the vertices of the link
 *
 * The function aborts the simulation if any of the parameters is not valid
 */

void add_gain_entry(unsigned int source, unsigned int sink, double gain, double length){

        /*
         * Pointer to the new instance of gain_entry
         */

        gain_entry* entry;

        /*
         * Check that the first two parameters correspond to valid node IDs
         */

        if(source<0 || sink <0 || source>=n_prc_tot || sink>=n_prc_tot){
                printf("[FATAL ERROR] Node IDs of the link are not valid\n");
                fclose(file);
                exit(EXIT_FAILURE);
        }

        /*
         * Allocate the instance
         */

        entry=malloc(sizeof(gain_entry));

        /*
         * Set the gain of the link
         */

        entry->gain=gain;

        /*
         * Set the sink node of the link
         */

        entry->sink=sink;

        /*
         * Set the length of the link
         */

        entry->distance=length;

        /*
         * Set the pointer to the next element to NULL
         */

        entry->next=NULL;

        /*
         * Check if this is the first element for the list of the source node
         */

        if(!gains_list[source]){

                /*
                 * It's the first element for the list of the source node => set the pointer in the array to this
                 * instance
                 */

                gains_list[source]=entry;

        }
        else{

                /*
                 * Add the new entry after the last element of the list, starting from the first element
                 */

                gain_entry* current=gains_list[source];
                while(current->next){

                        /*
                         * Go to next element
                         */

                        current=current->next;
                }

                /*
                 * Now current points to the last element of the list => connect it to the new entry
                 */

                current->next=entry;
        }
}

/*
 * ADD NOISE ENTRY
 *
 * Add an element with the noise floor and white noise of a node
 *
 * @node: ID of the node
 * @noise_floor: value of the noise floor for the node
 * @white_noise: value of the white noise for the node
 *
 * The function aborts the simulation if any of the parameters is not valid
 */

void add_noise_entry(unsigned int node, double noise_floor, double white_noise){

        /*
         * Pointer to the new noise_entry
         */

        noise_entry* entry;

        /*
         * Check that the first parameter corresponds to valid node ID
         */

        if(node<0 || node>=n_prc_tot){
                printf("[FATAL ERROR] Node IDs of the link are not valid\n");
                fclose(file);
                exit(EXIT_FAILURE);
        }

        /*
         * Initialize the entry of node i to the i-the entry of the noise_list array
         */

        entry=&noise_list[node];

        /*
         * Set the value of noise floor in the entry
         */

        entry->noise_floor=noise_floor;

        /*
         * Set the value of the white noise in the entry
         */

        entry->range=white_noise;
}

/*
 * GET NODES
 *
 * Returns the number of lists in gains_list: this is used to check that the input file contains the description of at
 * least one link for each node
 */

unsigned int get_nodes(){
        unsigned int nodes_counter=0;
        unsigned int index;
        for(index=0;index<n_prc_tot;index++){
                if(gains_list[index])
                        nodes_counter++;
        }
        return nodes_counter;
}

/*
 * TRANSMIT FRAME
 *
 * This function simulates the physical transmission of a link-layer frame to another node through a wireless link.
 * In the real world it is broadcasted to all the nodes in the network and the sender then waits for the acknowledgment
 * only from the recipient: here we are only interested in the acknowledgement, because this is connected to evaluation
 * of the link quality as part of the Collection Tree Protocol => only the recipient node receives an event from the
 * simulator
 *
 * @state: pointer to the object representing the current state of the node
 * @recipient: ID of the recipient node
 * @packet: pointer to the packet to be transmitted
 * @delivery_time: virtual time when the frame will be received by the nodes
 */

void transmit_frame(node_state* state,ctp_data_packet* packet,simtime_t delivery_time){

        /*
         * Get the list of links corresponding to the sender
         */

        gain_entry* node_links=gains_list[state->me];

        /*
         * Get the recipient node from the link layer header
         */

        unsigned int recipient=packet->link_frame.sink;

        /*
         * Set the state of the radio to busy
         */

        state->radio_state=RADIO_SENDING;

        /*
         * Get the gain entry corresponding to the link connecting the sender to the recipient
         */

        gain_entry* gain_entry=&node_links[recipient];

        /*
         * Get the gain of the link
         */

        double gain=gain_entry->gain;

        /*
         * Check that the link is up: if not, drop the packet
         */

        if(!gain_entry->up)
                return;

        /*
         * Set the value of the gain in the link-layer header of the frame: this is required by the simulation to
         * determine whether the packet will be received by the recipient node or not
         */

        packet->link_frame.gain=gain;

        /*
         * Schedule a new event of type "DATA_PACKET_RECEIVED" destined to the sink node.
         * The content of the event is the packet being sent by the node
         */

        ScheduleNewEvent(recipient,delivery_time,DATA_PACKET_DELIVERED,packet,sizeof(ctp_data_packet));
}

/*
 * GET CURRENT NOISE
 *
 * This function simulates the variability of the noise affecting a node by returning a random value from the uniform
 * distribution [white_noise_mean-noise_range,white_noise_mean+noise_range]
 */

double get_current_noise(unsigned int node){

        /*
         * Get the mean value of the dynamic component of the noise
         */

        double mean=WHITE_NOISE_MEAN;

        /*
         * Get a random value to be added to the mean value of the dynamic component of the noise
         */

        double rand=(int)Random()%2000000;
        rand/=1000000.0;
        rand-=1.0;
        rand*=noise_list[node].range;

        /*
         * Return the sum of the mean value of the of the dynamic component of the noise plus the randm value from the
         * uniform distribution
         */

        return mean+rand;
}


/*
 * COMPUTE STRENGTH OF THE SIGNAL IN THE CHANNEL
 *
 * Evaluate the power of the signal affecting the channel perceived by a node: it's the sum of the power of the noise
 * from the environment and of the power of the signals associated to all the packets being sent through the network at
 * the moment when the calculation is done.
 */

double compute_signal_strength(unsigned int node){

        /*
         * Get the current value of the noise affecting the recipient
         */

        double noise=get_current_noise(node);

        /*
         * Transform the value above from dBm to mW (milli Watt)
         * This is necessary to perform the sum of the power associated to signals as an algebraic sum
         *
         * 1mW=10^(1dbm/10)
         */

        double strength=pow(10.0,noise/10.0);

        /*
         * Add the sum of the power of all the transmissions going on right now in the network
         */

        strength+=pow(10.0,pending_transmissions_power/10.0);

        /*
         * Transforms the value back to dBm because all the other parameters related to the strength of signals are
         * given in dBm
         */

        return 10.0*log(strength)/log(10.0);
}

/*
 * IS CHANNEL FREE
 *
 * Function invoked by the link layer to determine whether the channel is free and a frame can hence be transmitted
 * through it.
 * A channel is declared to be free if the strength of the signal traversing it is below the threshold corresponding
 * to variable CHANNEL_FREE_THRESHOLD
 *
 * @state: pointer to the object representing the current state of the node
 *
 * Returns true if the channel is to be considered free, false otherwise
 */

bool is_channel_free(node_state* state){

        /*
         * Get the strength of the signal occupying the channel at the moment
         */

        double signal_strength=compute_signal_strength(state->me.ID);

        /*
         * Return true if the strength is less than the threshold, false otherwise
         */

        return signal_strength<CHANNEL_FREE_THRESHOLD;
}