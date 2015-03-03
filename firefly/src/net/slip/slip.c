/******************************************************************************
*  Nano-RK, a real-time operating system for sensor networks.
*  Copyright (C) 2007, Real-Time and Multimedia Lab, Carnegie Mellon University
*  All rights reserved.
*
*  This is the Open Source Version of Nano-RK included as part of a Dual
*  Licensing Model. If you are unsure which license to use please refer to:
*  http://www.nanork.org/nano-RK/wiki/Licensing
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, version 2.0 of the License.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*  Contributing Authors (specific to this file):
*  Anthony Rowe
*******************************************************************************/


#include "slip.h"
#include <nrk_events.h>
#include <nrk_error.h>
#include <nrk_cfg.h>


FILE *g_dv_in, *g_dv_out;
bool g_echo;
uint8_t g_delay;
int8_t _slip_started = NRK_ERROR;
nrk_sem_t *slip_tx_sem;

static  nrk_sig_t my_uart_rx_signal;
static  nrk_sig_mask_t sm;

int8_t slip_init (FILE * device_in, FILE * device_out, bool echo,
                  uint8_t delay)
{
  g_dv_in = device_in;
  g_dv_out = device_out;
  g_echo = echo;
  g_delay = delay;


#ifndef UART_PCP_CEILING
#define UART_PCP_CEILING       255
#endif
  slip_tx_sem = nrk_sem_create (1, UART_PCP_CEILING);
  if (slip_tx_sem == NRK_ERROR)
    nrk_kernel_error_add (NRK_SEMAPHORE_CREATE_ERROR, nrk_get_pid ());

  _slip_started = NRK_OK;
  return NRK_OK;
}

void put_byte (uint8_t c)
{
  if (g_delay > 0)
    nrk_spin_wait_us (g_delay * 1000);
  fputc (c, g_dv_out);
  if (g_echo) {
    // Not IMPLEMENTED
  }
}

uint8_t get_byte (void)
{
//  return getchar ();
  return fgetc (g_dv_in);
}

int8_t slip_tx (uint8_t * buf, uint8_t size)
{
  uint8_t i;
  int8_t v;
  uint8_t checksum;

// Make sure size is less than 128 so it doesn't act as a control
// message
  if (size > 128) {
    _nrk_errno_set (3);
    return NRK_ERROR;
  }

  v = nrk_sem_pend (slip_tx_sem);
  if (v == NRK_ERROR) {
    nrk_kprintf (PSTR ("SLIP TX ERROR:  Access to semaphore failed\r\n"));
    _nrk_errno_set (1);
    return NRK_ERROR;
  }

// Send end to flush any accumulated data
  put_byte (END);
// Send the start byte
  put_byte (START);
  put_byte (size);

  checksum = 0;

// Send payload and stuff bytes as needed
  for (i = 0; i < size; i++) {
    if (buf[i] == END )
	{
	// don't checksum values that do not appear in final buffer
      	put_byte (ESC);
      	put_byte (ESC_END);
    	checksum += END;
	}
    else if (buf[i] == ESC )
	{
	// don't checksum values that do not appear in final buffer
      	put_byte (ESC);
      	put_byte (ESC_ESC);
    	checksum += ESC;
	}
    else 
	{
	put_byte (buf[i]);
    	checksum += buf[i];
	}
  }

// Make sure checksum is less than 128 so it doesn't act as a control
// message
  checksum &= 0x7F;
  // Send the end byte
  put_byte (checksum);
  put_byte (END);
  v = nrk_sem_post (slip_tx_sem);
  if (v == NRK_ERROR) {
    nrk_kprintf (PSTR ("SLIP TX ERROR:  Release of semaphore failed\r\n"));
    _nrk_errno_set (2);
    return NRK_ERROR;
  }
  return NRK_OK;
}

int8_t slip_started ()
{
  return _slip_started;
}

int8_t slip_rx (uint8_t * buf, uint8_t max_len)
{
  uint8_t c;
  uint8_t index, last_c;
  uint8_t received, checksum, size;
  int8_t v;

my_uart_rx_signal=nrk_uart_rx_signal_get();
// Register your task to wakeup on RX Data
  if (my_uart_rx_signal == NRK_ERROR)
    nrk_kprintf (PSTR ("SLIP RX error: Get Signal\r\n"));
  
   v=nrk_signal_register (my_uart_rx_signal);
   if(v==NRK_ERROR) nrk_kprintf( PSTR( "SLIP RX error: nrk_signal_register\r\n" ));

  received = 0;
  if( nrk_uart_data_ready (NRK_DEFAULT_UART) == 0) sm = nrk_event_wait (SIG (my_uart_rx_signal));
// Wait until you receive the packet start (START) command
  while (1) {
    // Wait for UART signal
    while (nrk_uart_data_ready (NRK_DEFAULT_UART) != 0) {
      // Read Character
      //c = getchar ();
      c = get_byte();
      if (c == START)
        goto start;
    }
    if( nrk_uart_data_ready (NRK_DEFAULT_UART) == 0) sm = nrk_event_wait (SIG (my_uart_rx_signal));
      c = get_byte();
    //c = getchar ();
    if (c == START)
      break;
  }
  start:
  size = get_byte ();
  checksum = 0;
  while (1) {
    if( nrk_uart_data_ready (NRK_DEFAULT_UART) == 0) sm = nrk_event_wait (SIG (my_uart_rx_signal));
    while (nrk_uart_data_ready (NRK_DEFAULT_UART) != 0) {
      last_c = c;
      //c = getchar ();
      c = get_byte();

      // handle bytestuffing if necessary
      switch (c) {

        // if it's an END character then we're done with
        // the packet
      case END:
        // a minor optimization: if there is no
        // data in the packet, ignore it. This is
        // meant to avoid bothering IP with all
        // the empty packets generated by the
        // duplicate END characters which are in
        // turn sent to try to detect line noise.
        if (received) {
	
          checksum &= 0x7f;
          if (last_c == checksum)
            return received;
        }
	nrk_kprintf( PSTR( "Checksum failed: ") );
	printf( "%d %d %d\r\n",received, last_c, checksum );
        return NRK_ERROR;
        //return received;
	break;

        // if it's the same code as an ESC character, wait
        // and get another character and then figure out
        // what to store in the packet based on that.
      case ESC:
        last_c = c;
 	if( nrk_uart_data_ready (NRK_DEFAULT_UART)==0 )	sm = nrk_event_wait (SIG (my_uart_rx_signal));
        c = get_byte ();
        switch (c) {
        case ESC_ESC:
          c = ESC;
          break;
        case ESC_END:
          c = END;
          break;
	default:
	  nrk_kprintf( PSTR("Malformed ESC sequence\r\n" ));
	  // Return error if ESC before something other than ESC or END
	  return NRK_ERROR;
        }

        // here we fall into the default handler and let
        // it store the character for us
      default:
        if (received < max_len && received < size) {
          buf[received++] = c;
          checksum += c;
        }
      }
    }
  }

  return 0;
}
