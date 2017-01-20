#include <math.h>
#include "physical_layer.h"
#include "link_layer.h"

/*
 * PHYSICAL LAYER MODEL
 *
 * This piece of code models the behaviour of the physical layer. This depends on two elements:
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

/* GLOBAL VARIABLES - start
 *
 * Default values of the parameters for the physical layer (check physical_layer.h for a description)
 */

double white_noise_mean=WHITE_NOISE_MEAN;
double channel_free_threshold=CHANNEL_FREE_THRESHOLD;

/* GLOBAL VARIABLES - end */

extern unsigned int csma_sensitivity;

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

extern FILE* file;

/*
 * PARSE SIMULATION PARAMETERS FOR THE PHYSICAL LAYER
 */

void parse_physical_layer_parameters(void* event_content){

        if(IsParameterPresent(event_content, "white_noise_mean"))
                white_noise_mean=GetParameterDouble(event_content,"white_noise_mean");
        if(IsParameterPresent(event_content, "channel_free_threshold"))
                channel_free_threshold=GetParameterDouble(event_content,"channel_free_threshold");
}

/*
 * INIT RADIO
 *
 * Initialize the variables in the state object that are related to the radio and to the physical layer
 *
 * @state: pointer to the object representing the current state of the node
 */

void init_physical_layer(node_state* state){

        /*
         * Set the pointer to the frame being sent to NULL
         */

        state->radio_outgoing=NULL;

        /*
         * Set the power of the pending incoming transmissions to NULL
         */

        state->pending_transmissions_power=0;

        /*
         * Set the list containing pendind transmissions to NULL
         */

        state->pending_transmissions=NULL;
}

/*
 * CREATE PENDING TRANSMISSION
 *
 * Create a new instance of type "pending_transmission" to keep track of a new pending transmission
 *
 * @state: pointer to the object representing the current state of the node
 * @type: byte telling whether the frame contains a beacon or a data packet
 * @frame: pointer to the frame carried by the signal
 * @power: power of the signal
 *
 * Returns the address of the new instance
 */

pending_transmission* create_pending_transmission(unsigned char type,void* frame, double power){

        /*
         * Allocate a new instance of type "pending_transmission"
         */

        pending_transmission* new_transmission=malloc(sizeof(pending_transmission));

        /*
         * Set to zero the allocated buffer
         */

        bzero(new_transmission,sizeof(pending_transmission));

        /*
         * Parse the content of the frame
         */

        if(type==CTP_BEACON){

                /*
                 * The frame contains a beacon => set the type
                 */

                new_transmission->frame_type=CTP_BEACON;
        }
        else{

                /*
                 * The frame contains a data packet => set the type
                 */

                new_transmission->frame_type=CTP_DATA_PACKET;
        }

        /*
         * Set the frame associated with the transmission
         */

        new_transmission->frame=frame;

        /*
         * The transmission has not been lost yet
         */

        new_transmission->lost=false;

        /*
         * Set the next pointer to NULL: this will be the last pending transmission in the list
         */

        new_transmission->next=NULL;

        /*
         * Set the power of the transmission
         */

        new_transmission->power=power;

        /*
         * Return the address
         */

        return new_transmission;
}

/*
 * ADD GAIN ENTRY
 *
 * Add a new element in the list associated to the given source node.
 *
 * @source: ID of the source node of the link
 * @sink: ID of the sink node of the link
 * @gain: gain of the link
 *
 * The function aborts the simulation if any of the parameters is not valid
 */

void add_gain_entry(unsigned int source, unsigned int sink, double gain){

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
 * COMPARE PENDING TRANSMISSIONS
 *
 * Given the pointers to two pending transmissions, this helper function compares their relevant fields and returns
 * true if they all coincide, false otherwise
 *
 * @a: pointer to a pending transmission
 * @b: pointer to the other pending transmission
 */

bool compare_pending_transmissions(pending_transmission*a,pending_transmission*b){
        if(a->frame==b->frame && a->power==b->power && a->frame_type==b->frame_type)
                return true;
        return false;
}

/*
 * SEND ACK
 *
 * Function dedicated to the sending of the acknowledgement for a frame in case this contains a data packet. The ack
 * can be sent by the node only if it's not busy sending another frame.
 *
 * @state: pointer to the object representing the current state of the node
 * @packet: pointer to the frame containing the data packet
 */

void send_ack(node_state* state,ctp_data_packet* packet){

        /*
         * If there's no other transmission going on, the packet is acknowledged
         */

        if(state->radio_outgoing!=NULL) {

                /*
                 * The radio transceiver is not busy => get the sender of the packet and send it an event to inform
                 * about the reception of the ack
                 */

                unsigned int sender=packet->link_frame.src;
                ScheduleNewEvent(sender,state->lvt,ACK_RECEIVED,packet,sizeof(ctp_data_packet));

        }
}

/*
 * NEW PENDING TRANSMISSION
 *
 * Helper function for the events of type BEACON_TRANSMISSION_STARTED and DATA_PACKET_TRANSMISSION STARTED.
 * It is in charge of creating a new entry for the new pending transmission in the dedicated list of the node state;
 * also it has to check whether the transmission will be received by the node or not, depending on the actual strength
 * of the interferences created by other signals
 *
 * @state: pointer to the object representing the current state of the node
 * @gain: strength of the new transmission (in dBm)
 * @type: byte telling whether the frame contains a beacon or a data packet
 * @frame: pointer to the frame carried by the signal
 * @duration: duration of the new transmission
 */

void new_pending_transmission(node_state* state, double gain, unsigned char type,void* frame,double duration){

        /*
         * Pointer to the new instance of type "pending_transmission" created for the new transmission
         */

        pending_transmission* new_pending_transmission=NULL;

        /*
         * Pointer to the last element in the list of pending transmissions
         */

        pending_transmission* last_transmission=NULL;

        /*
         * Counter of pending transmissions
         */

        unsigned int pending_transmissions_count=0;

        /*
         * Source of the frame
         */

        unsigned int source;

        /*
         * Check if the node is running: if not, it will not receive the frame transmitted
         */

        if(state->state&RUNNING) {

                /*
                 * Then get the strength of the signal affecting the channel perceived by the node at
                 * the moment
                 */

                double channel_strength=compute_signal_strength(state);

                /*
                 * Check if the gain of the link is enough strong for the frame to be delivered,
                 * considering the actual strength of the signal sensed by the node: if not, the frame
                 * is dropped
                 */

                if(channel_strength+csma_sensitivity<gain){

                        /*
                         * The signal carrying the frame has enough power for the frame to be received
                         * by the node.
                         * Check if the node is in the busy receiving or transmitting another frame:
                         * if so, drop the current frame
                         */

                        if(!(state->radio_state&RADIO_RECEIVING) &&
                           !(state->radio_state&RADIO_TRANSMITTING)){

                                /*
                                 * The radio is not busy receiving another frame => the current frame
                                 * is received => set the state of the radio to RADIO_RECEIVING
                                 */

                                state->radio_state|=RADIO_RECEIVING;
                        }

                }
        }

        /*
         * Go through the list of pending transmissions and check whether the current transmission will cause the node
         * missing some of them because they are too weak w.r.t to the new one
         */

        pending_transmission* current=state->pending_transmissions;
        while(current!=NULL){

                /*
                 * If the difference between the power of the current transmission and the power of the
                 * new one is below the threshold, the former is lost
                 */

                if(current->power-csma_sensitivity<gain)
                        current->lost=true;

                /*
                 * Go to next element of the list and store the pointer to the last NOT NULL element
                 */

                last_transmission=current;
                current=current->next;

                /*
                 * Update the counter of pending transmissions
                 */

                pending_transmissions_count++;
        }

        /*
         * Increment the counter of the strength of the signal sensed by the node (convert from dBm to
         * mW)
         */

        state->pending_transmissions_power+=pow(10.0, gain / 10.0);

        /*
         * Create an entry for the the new pending transmissions
         */

        new_pending_transmission=create_pending_transmission(type,frame,gain);

        if(type==CTP_BEACON){
                source=((ctp_routing_packet*)(new_pending_transmission->frame))->link_frame.src;
                printf("Source of the beacon in the transmission object:%d",source);
                fflush(stdout);
        }

        /*
         * Check if there are other pending transmissions
         */

        if(pending_transmissions_count){

                /*
                 * There are other pending transmissions => add the new transmission at the end of the list and get
                 * the address
                 */

                last_transmission->next=new_pending_transmission;

        }
        else{

                /*
                 * This is the only pending transmission => set the pointer of the list of pending transmission to its
                 * address
                 */

                state->pending_transmissions=new_pending_transmission;
        }

        /*
         * Schedule a new event corresponding to the moment when the transmission will be finished
         */

        ScheduleNewEvent(state->me,state->lvt+duration,TRANSMISSION_FINISHED,new_pending_transmission,
                         sizeof(pending_transmission));
}

/*
 * TRANSMISSION FINISHED
 *
 * Helper function for the event TRANSMISSION_FINISHED
 * It is in charge of removing the element of the finished transmission from the list of pending transmissions: if the
 * transmission has been successfully received by the node, it starts processing the associated frame
 *
 * @state: pointer to the object representing the current state of the node
 * @finished_transmission: pointer to the finished transmission
 */

void transmission_finished(node_state* state,pending_transmission* finished_transmission){

        /*
         * Pointer to the element of the list that points to the transmission that is now finished
         */

        pending_transmission* predecessor=NULL;

        /*
         * Type of the content of the frame transmitted
         */

        unsigned char type;

        /*
         * Source of the transmission
         */

        unsigned int source;

        /*
         * In time between the beginning and the end of this transmission, new frames may have been sent to the node and
         * so there may be further pending transmissions whose power is not strong enough compared to the current
         * transmission => they will be missed by the node
         */

        /*
         * Pointer to the current transmission being checked
         */

        pending_transmission* current_transmission=state->pending_transmissions;

        /*
         * Go through all the pending transmissions
         */

        while(current_transmission!=NULL){

                /*
                 * Check if the transmission analyzed is the predecessor
                 */

                if(current_transmission->next &&
                        compare_pending_transmissions(current_transmission->next,finished_transmission)) {

                        /*
                         * Found the predecessor
                         */

                        predecessor=current_transmission;

                }

                /*
                 * Check if the transmission analyzed will be missed by the node because not enough strong w.r.t. the
                 * transmission that is finishing
                 */

                if(!compare_pending_transmissions(current_transmission,finished_transmission)){
                        if(current_transmission->power-csma_sensitivity<finished_transmission->power){
                                current_transmission->lost=true;
                        }
                }

                /*
                 * Go to next element of the list
                 */

                current_transmission=current_transmission->next;
        }

        /*
         * Remove the finished transmission from the list
         */

        if(predecessor){
                predecessor->next=finished_transmission->next;
        }
        else{

                /*
                 * If it is the first element of the list, update the pointer to the list
                 */

                state->pending_transmissions=finished_transmission->next;
        }

        /*
         * Remove the power associated to transmission from the strength of the global signal sensed by the node
         */

        state->pending_transmissions_power-=pow(10.0, finished_transmission->power / 10.0);

        /*
         * It may be the case that while the transmission was ongoing, even  stronger transmission have come and so this
         * transmission will be missed by the node too
         */

        if(finished_transmission->power-csma_sensitivity<compute_signal_strength(state))
                finished_transmission->lost=true;

        /*
         * If the frame has been received by the node, it has to be processed
         */

        if(!finished_transmission->lost){

                /*
                 * The frame has been received => get its type
                 */

                type=finished_transmission->frame_type;

                /*
                 * Inform the LINK LAYER or the FORWARDING ENGINE about the reception
                 */

                if(type==CTP_BEACON){
                        source=((ctp_routing_packet*)finished_transmission->frame)->link_frame.src;
                        printf("Node %d received beacon from %d\n",state->me,source);
                        fflush(stdout);
                }

                frame_received(state,finished_transmission->frame,type);

                /*
                 * If the frame contains a data packet, the sender is waiting for an acknowledgment by the intended
                 * recipient => if this node is the recipient and it is not busy transmitting another frame, it has to
                 * send the acknowledgment to the recipient.
                 * First get the recipient, as indicated in the link layer frame
                 */

                if(type==CTP_DATA_PACKET) {

                        /*
                         * The frame contains a data packet => extract the link layer frame from the packet
                         */

                        link_layer_frame *link_frame = &((ctp_data_packet *) finished_transmission->frame)->link_frame;

                        /*
                         * Get the intended recipient
                         */

                        unsigned int recipient = link_frame->sink;

                        /*
                         * If this is the recipient and is not busy transmitting, send an acknowledgment
                         */

                        if(recipient==state->me && !state->link_layer_transmitting)
                                send_ack(state,(ctp_data_packet*)finished_transmission->frame);
                }
        }

        /*
         * The radio is no longer receiving => clear the corresponding flag
         */

        state->radio_state&=~RADIO_RECEIVING;

        /*
         * Reset the pointer to the outgoing frame
         */

        state->radio_outgoing=NULL;

        /*
         * Finally remove the element associated to the pending transmission
         */

        //free(finished_transmission);

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
 * The wireless channel is shared by all the nodes of the network, so the signals carrying the frames will interfere
 * one another => a signal travelling through a link is received by the sink node only if its strength is greater than
 * the strength of the noise resulting from the sum of all the other signals plus the noise affecting the sink node
 * itself. Moreover, if a signal B is sent to same sink node as another signal A, with A weaker than B, the sink node
 * will only receive the signal B => we have to keep track of all the signals being transmitted to each node: if two or
 * more signals overlaps in time, only the strongest one will be received by the node. That's why this function only
 * informs the recipient node that the transmission of a frame has started at time x and will finish at some time in the
 * future y, when the frame will be delivered => the recipient node has to keep track of the ongoing transmissions that
 * occur in the interval between x and y because they may overwrite the former transmission
 *
 * @state: pointer to the object representing the current state of the node
 * @type: byte telling whether the frame contains a beacon or a data packet
 */

void transmit_frame(node_state* state,unsigned char type){

        /*
         * Get the pointer to the frame to be sent: either the beacon of the node or the packet in the head of the
         * output queue
         */

        if(type==CTP_BEACON)
                state->radio_outgoing = &state->routing_packet;
        else {
                state->radio_outgoing = &state->forwarding_queue[state->forwarding_queue_head]->packet;
        }

        /*
         * Get the first element in the list of the links of the sender
         */

        gain_entry* gain_entry=gains_list[state->me];

        /*
         * Transmit the frame to all the nodes connected to the sender: the description of each link is an element of
         * the list.
         */

        while(gain_entry){

                /*
                 * Get the gain of the link
                 */

                double gain=gain_entry->gain;

                /*
                 * Get the sink node of the link
                 */

                unsigned int sink=gain_entry->sink;

                /*
                 * Set the value of the gain in the link-layer header of the frame: this is required by the simulation
                 * to determine whether the packet will be received by the recipient node or not.
                 * First parse the frame being transmitted to the data structure corresponding to the type
                 */

                if(type==CTP_BEACON){

                        /*
                         * This frame contains a beacon
                         */

                        ((ctp_routing_packet*)state->radio_outgoing)->link_frame.gain=gain;
                        printf("Src of beacon:%d\n",((ctp_routing_packet*)state->radio_outgoing)->link_frame.src);
                        printf("Sink of beacon:%d\n",((ctp_routing_packet*)state->radio_outgoing)->link_frame.sink);
                        fflush(stdout);

                        /*
                         * Schedule a new event destined to the sink node of the link, containing the frame being
                         * transmitted
                         */

                        ScheduleNewEvent(sink,state->lvt,BEACON_TRANSMISSION_STARTED,state->radio_outgoing,
                                         sizeof(ctp_routing_packet));

                }
                else{

                        /*
                         * This frame contains a data packet
                         */

                        ((ctp_data_packet*)state->radio_outgoing)->link_frame.gain=gain;

                        /*
                         * Schedule a new event destined to the sink node of the link, containing the frame being
                         * transmitted
                         */

                        ScheduleNewEvent(sink,state->lvt,DATA_PACKET_TRANSMISSION_STARTED,state->radio_outgoing,
                                         sizeof(ctp_data_packet));
                }

                /*
                 * Go to next entry
                 */

                gain_entry=gain_entry->next;
        }
}

/*
 * GET CURRENT NOISE
 *
 * This function simulates the variability of the noise affecting a node by returning a random value from the uniform
 * distribution [white_noise_mean-noise_range,white_noise_mean+noise_range] and adding to the value pf the noise floor
 */

double get_current_noise(unsigned int node){

        /*
         * Get the mean value of the dynamic component of the noise
         */

        double mean=white_noise_mean;

        /*
         * Get a random value to be added to the mean value of the dynamic component of the noise
         */

        double rand=RandomRange(0,2000000);
        rand/=1000000.0;
        rand-=1.0;
        rand*=noise_list[node].range;

        /*
         * Return the sum of the mean value of the of the dynamic component of the noise plus the random value from the
         * uniform distribution
         */

        return mean+rand+noise_list[node].noise_floor;
}


/*
 * COMPUTE STRENGTH OF THE SIGNAL SENSED BY THE NODE
 *
 * This function returns the power of the signal affecting the channel calculated by a node: it's the sum of the power
 * of the noise from the environment plus the power of the signals associated to all the packets that are being sent
 * to the node when the calculation is done.
 *
 * @state: pointer to the object representing the current state of the node
 */

double compute_signal_strength(node_state* state){

        /*
         * Get the current value of the noise affecting the node
         */

        double noise=get_current_noise(state->me);

        /*
         * Transform the value above from dBm to mW (milli Watt)
         * This is necessary to perform the sum of the power associated to signals as an algebraic sum
         *
         * 1mW=10^(1dbm/10)
         */

        double strength=pow(10.0,noise/10.0);

        /*
         * Add the sum of the power of all the transmissions sensed by the node (this value is in mW)
         */

        strength+=state->pending_transmissions_power;

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

        double signal_strength=compute_signal_strength(state);

        /*
         * Return true if the strength is less than the threshold, false otherwise
         */

        if(signal_strength<channel_free_threshold)
                return true;
        return false;
}