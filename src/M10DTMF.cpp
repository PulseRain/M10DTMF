/*
###############################################################################
# Copyright (c) 2017, PulseRain Technology LLC 
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License (LGPL) as 
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but 
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
# or FITNESS FOR A PARTICULAR PURPOSE.  
# See the GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
*/

#include "M10DTMF.h"
#include "M10CODEC.h"


#define SAMPLE_BUFFER_SIZE 256
#define SAMPLE_BUFFER_SIZE_MASK 0xFF

#define DTMF_DETECTION_THRESHOLD 12
#define DTMF_SUM_SHIFT 7

#define DTMF_SMALLEST_SAMPLE (-32767)
#define DTMF_BIGGEST_SAMPLE 32767

#define DTMF_SCALE_FACTOR 16384
#define DTMF_SCALE_SHIFT 14

#define DTMF_SQUELCH 256
#define DTMF_ATTENUATION 16384

#define DTMF_NO_DETECTION_THRESHOLD 1


// sample buffer and scratch buffer
static uint16_t wav_samp_buffer [SAMPLE_BUFFER_SIZE];
static int16_t scratch_buffer[SAMPLE_BUFFER_SIZE];
static uint8_t scratch_buffer_counter = 0;

static uint8_t wav_buf_read_pointer = 0;
static uint8_t wav_buf_write_pointer = 0;

static int32_t s[16][3];
static int32_t X_square[16];

static uint8_t got_detection;
static uint8_t no_detection;

static int32_t max_X_square;
static uint8_t max_index;
static int32_t sum_of_the_rest_X_square;

static uint8_t row_hit_count;
static uint8_t column_hit_count;
static uint8_t row_index;
static uint8_t column_index;

static uint8_t i, j, shift;  
static int16_t tmp, small , big, dc_offset, gain;
static uint8_t key_index;
static int32_t x;
static int32_t t;

static int16_t coef [16] = { // 2 * cos (2 * pi * k / N)

    28106,  // 697 Hz
    26791,  // 770 Hz
    25833,  // 852 Hz
    24279,  // 941 Hz
    
    
    14733,  // 697 Hz * 2
    11793,  // 770 Hz * 2
    7180,   // 852 Hz * 2
    3212,   // 941 Hz * 2


    18868,  // 1209 Hz
    16151,  // 1336 Hz
    13279,  // 1477 Hz
    9512,   // 1633 Hz

    -10279, // 1209 Hz * 2
    -16846, // 1336 Hz * 2
    -22595, // 1477 Hz * 2
    -27684  // 1633 Hz * 2
};

static uint8_t key_map[16] = {0x1, 0x2, 0x3, 0xA, 
                              0x4, 0x5, 0x6, 0xB,
                              0x7, 0x8, 0x9, 0xC,
                              0xE, 0x0, 0xF, 0xD};

                              
                              
                              
                              
                              
                              

//----------------------------------------------------------------------------
// codec_isr_handler()
//
// Parameters:
//      None
//
// Return Value:
//      None
//
// Remarks:
//      CODEC ISR for sample collection
//----------------------------------------------------------------------------

static void codec_isr_handler()
{
    uint8_t high, low;
    uint8_t wav_buf_write_pointer_next;
    uint16_t sample;

    //== wav_buf_write_pointer_next = (wav_buf_write_pointer + 1) & SAMPLE_BUFFER_SIZE_MASK;
    wav_buf_write_pointer_next = wav_buf_write_pointer + 1;

    if (wav_buf_read_pointer == wav_buf_write_pointer_next) { // buffer full
        return;
    }

    high = CODEC_READ_DATA_HIGH;
    low  = CODEC_READ_DATA_LOW;

    sample = ((uint16_t)high << 8) + low;
  
    wav_samp_buffer[wav_buf_write_pointer++] = sample;

    //== wav_buf_write_pointer = (wav_buf_write_pointer + 1) & SAMPLE_BUFFER_SIZE_MASK;
  
} // End of codec_isr_handler()

                              
                              
//----------------------------------------------------------------------------
// search_for_max()
//
// Parameters:
//   start_index: 0 or 8, the start index of row frequency or column frequency
//   end_index  : 7 or 15, the end index of row frequency or column frequency
//
// Return Value:
//      None
//
// Remarks:
//      function to search for the frequency that has the highest power
//----------------------------------------------------------------------------
  
static void search_for_max(uint8_t start_index, uint8_t end_index) 
{
  
    max_X_square = X_square[start_index];
    sum_of_the_rest_X_square = max_X_square;
    max_index = start_index;
  
    for (i = start_index + 1; i <= end_index; ++i) {
        if (max_X_square < X_square[i]) {
            max_X_square = X_square[i];
            max_index = i;
        }

        sum_of_the_rest_X_square += X_square[i];
    } // End of for loop

    sum_of_the_rest_X_square -= max_X_square;
  
} // End of find_max()
  
  
//----------------------------------------------------------------------------
// dtmf_begin()
//
// Parameters:
//      None
//
// Return Value:
//      None
//
// Remarks:
//      call this function to initialize the CODEC and ISR
//----------------------------------------------------------------------------

static void dtmf_begin()
{
    attachIsrHandler(CODEC_INT_INDEX, codec_isr_handler);
    CODEC.begin();
} // End of dtmf_begin();


//----------------------------------------------------------------------------
// dtmf_reinit()
//
// Parameters:
//      None
//
// Return Value:
//      None
//
// Remarks:
//      function to reset the sample buffer pointer, and fill the sample
//      buffer with zero
//----------------------------------------------------------------------------

void dtmf_reinit()
{
  uint8_t wav_buf_write_pointer_next;

  noInterrupts();
  
    wav_buf_write_pointer_next = wav_buf_write_pointer + 1;

    if (wav_buf_read_pointer == wav_buf_write_pointer_next) { // buffer full
        i = 0;
        do {
            wav_samp_buffer[i++] = 0;          
        } while(i);
        
        wav_buf_read_pointer = 0;
        wav_buf_write_pointer = 0;
        scratch_buffer_counter = 0;
    }
      
  interrupts();
} // dtmf_reinit()


//----------------------------------------------------------------------------
// dtmf_decoder()
//
// Parameters:
//      None
//
// Return Value:
//      non-negative value: the key detected. hex value from 0 - 15, 
//                         where 14 stands for '*', and 15 stands for '#' 
//                     -1 : sample collected, but no valid key was detected
//                     -2 : no sample collected
//
// Remarks:
//      main function to decode DTMF
//----------------------------------------------------------------------------

static int8_t dtmf_decoder() 
{ 

    while (wav_buf_write_pointer != wav_buf_read_pointer) {

        noInterrupts();
            scratch_buffer[scratch_buffer_counter++] = wav_samp_buffer[wav_buf_read_pointer++];
        interrupts();
       
    
        if (scratch_buffer_counter == 0) {

            
            //=================================================================================
            //  init()
            //=================================================================================
            
                for (i = 0; i < 16; ++i) {
                    for (j = 0; j < 3; ++j) {
                        s[i][j] = 0;  
                    } // End of for loop j
                } // End of for loop i
            
            //=================================================================================
            //  remove_dc()
            //=================================================================================
                
                small = DTMF_BIGGEST_SAMPLE;
                big = DTMF_SMALLEST_SAMPLE;
              
                i = 0;
                do {
                    tmp = scratch_buffer[i];
              
                    if (small > tmp) {
                        small = tmp;
                    }
              
                    if (big < tmp) {
                        big = tmp;
                    }
              
                    ++i;
              
                } while (i);
                  
                dc_offset = (small + big) >> 1;
                
                gain = 1;
                
                if ((big < DTMF_SQUELCH) || (small > (-DTMF_SQUELCH))) {
                    gain = 0;
                } 
              
                
                if ((big > DTMF_ATTENUATION)  || (small < (-DTMF_ATTENUATION))) {
                    shift = 2;
                } else {
                    shift = 0;
                }
              
                i = 0;
                do {
                    scratch_buffer[i] -= dc_offset;
                    scratch_buffer[i] *= gain;
                    scratch_buffer[i] >>= shift;
              
                    ++i;
                } while(i);

            //=================================================================================
            //  Goertzel()
            //=================================================================================
            
                  i = 0;
                  do {
                    
                      x = (int32_t)scratch_buffer[i];
                      
                      for (j = 0; j < 16; ++j) {
                          t = (int32_t)((int32_t)coef[j] * s[j][1] + DTMF_SCALE_FACTOR / 2 ) >> DTMF_SCALE_SHIFT; 
                          s[j][2] = x  + t  - s[j][0] ;
                
                          s[j][0] = s[j][1];
                          s[j][1] = s[j][2];
                
                      } // End of for loop
                
                      ++i;
                  } while (i); // End of for loop i
                   
                  for (i = 0; i < 16; ++i) {
                      s[i][1] >>= DTMF_SUM_SHIFT;
                      s[i][0] >>= DTMF_SUM_SHIFT;
                  }
                
                  for (i = 0; i < 16; ++i) {
                    t = ((int32_t)coef[i] *  s[i][1] + DTMF_SCALE_FACTOR / 2) >> DTMF_SCALE_SHIFT;
                      
                    X_square[i] = s[i][1] * s[i][1] + s[i][0] * s[i][0] - t *  s[i][0];
                  } // End of for loop i
                
                  for (i = 4; i <= 7; ++i) {
                      X_square[i] >>= 6;
                  } // End of for loop i
                
                  for (i = 12; i <= 15; ++i) {
                      X_square[i] >>= 6;
                  } // End of for loop i
                
                  row_hit_count = 0;
                  column_hit_count = 0;

                  //-----------------------------------------------------------------------------------------
                  // Find the best hit tone
                  //-----------------------------------------------------------------------------------------
                      search_for_max (0, 7);
                    
                      if ((max_X_square > (sum_of_the_rest_X_square * DTMF_DETECTION_THRESHOLD)) && (max_index < 4)) {
                          ++row_hit_count;
                          row_index = max_index;
                      }
                      
                      search_for_max (8, 15);
                    
                      if ((max_X_square > (sum_of_the_rest_X_square * DTMF_DETECTION_THRESHOLD)) && (max_index < 12)) {
                          ++column_hit_count;
                          column_index = max_index - 8;
                      }

      
                      if (row_hit_count && column_hit_count) {
                          key_index = row_index * 4 + column_index;
                    
                          no_detection = 0;
                          row_hit_count = 0;
                          column_hit_count = 0;
                   
                          if (got_detection == 0) {
                              got_detection = 1;
                              return key_map[key_index];
                          }

                      } else {
                
                          ++no_detection;  
                    
                          if (no_detection > DTMF_NO_DETECTION_THRESHOLD) {
                              got_detection = 0;
                          }
                      }

            return -1;                  
                      
        } // End of if - (scratch_buffer_counter == 0)

    } // End of while loop
    
    return -2;
    
} // End of dtmf_decoder()



const M10DTMF_STRUCT DTMF = {
    dtmf_begin,   // begin
    dtmf_reinit,  // reinit
    dtmf_decoder  // decode
};
