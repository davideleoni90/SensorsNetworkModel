#ifndef SENSORSNETWORKMODELPROJECT_LINK_LAYER_H
#define SENSORSNETWORKMODELPROJECT_LINK_LAYER_H

#include "application.h"

/*
 * PARAMETERS OF THE CARRIER SENSE MULTIPLE ACCESS PROTOCOL (CSMA) - start
 */

enum{
        CSMA_SYMBOLS_PER_SEC=65536, // Number of symbols per second (baud rate)
        CSMA_BITS_PER_SYMBOL=4, // Number of bits per symbol
        CSMA_MIN_FREE_SAMPLES=1, // Number of times the node "sees" the channel free before it starts sending

        /*
         * Upper bound for the number of times the node "sees" the channel free before it starts sending
         */

        CSMA_MAX_FREE_SAMPLES=0,

        /*
         * Upper bound of the backoff range (in symbols). It is multiplied by the exponent base to the n-th power,where
         * n is the number of times the node has already backed off => after the first backoff the upper bound of the
         * range is CSMA_HIGH*CSMA_EXPONENT_BASE, after the second one it is
         * CSMA_HIGH*CSMA_EXPONENT_BASE*CSMA_EXPONENT_BASE
         */

        CSMA_HIGH=160,

        /*
         * Lower bound of the backoff range (in symbols). It is multiplied by the exponent base to the n-th power,where
         * n is the number of times the node has already backed off => after the first backoff the lower bound of the
         * range is CSMA_LOW*CSMA_EXPONENT_BASE, after the second one it's CSMA_LOW*CSMA_EXPONENT_BASE*CSMA_EXPONENT_BASE
         */

        CSMA_LOW=20,
        CSMA_INIT_HIGH=640, // Upper bound of the initial range for the backoff (in symbols)
        CSMA_INIT_LOW=20, // Lower bound of the initial range for the backoff (in symbols)

        /*
         * Time needed by the radio transceiver to switch from Transmission (TX) to Reception (RX) and vice-versa,
         * expressed in symbols (500 us ~= 32 symbols)
         */

        CSMA_RXTX_DELAY=11,

        /*
         * Base of the exponent used to calculate the backoff; if equal to 1, the range where the random value of the
         * backoff time is selected is fixed
         */

        CSMA_EXPONENT_BASE=1,

        /*
         * Number of symbols corresponding to the preamble that precedes every frame transmitted by the radio (in
         * accordance with the IEEE 802.15.4 standard). This comprises three parts:
         *
         * PREAMBLE SEQUENCE (4 bytes) | START OF FRAME DELIMITER (1 byte) | FRAME LENGTH (1 byte)
         *
         * The total length of the preamble is 6 bytes = 48 bits => since each symbol corresponds to 4 bits, the length
         * of the preamble in symbols is 48/4=12
         */

        CSMA_PREAMBLE_LENGTH=12,

        /*
         * In accordance with the IEEE 802.15.4 standard, an acknowledgement frame is transmitted by the receiver 12
         * symbol periods after the last symbol of the incoming frame. Its format includes a 6 bytes preamble and 5 bytes
         * MAC PROTOCOL DATA UNIT (MPDU), so the size of an acknowledgment is 11 bytes = 88 bits =>  since each symbol
         * corresponds to 4 bits, the length in symbols is 22.
         * Adding the 12 symbols delay, the total number of symbols for the reception of an ack is 34
         */

        CSMA_ACK_TIME=34
};

/*
 * PARAMETERS OF THE CARRIER SENSE MULTIPLE ACCESS PROTOCOL (CSMA) - end
 */

void start_frame_transmission(node_state* state);
bool send_frame(node_state* state,unsigned int recipient, unsigned char type);
void frame_transmitted(node_state* state);
void check_channel(node_state* state);
void init_link_layer(node_state* state);
#endif //SENSORSNETWORKMODELPROJECT_LINK_LAYER_H
