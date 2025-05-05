#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/
#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 12      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */
#define TIMEOUT 16.0

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

/* structure for the sr */
static struct pkt window[SEQSPACE];
static bool acked[SEQSPACE];
static int base = 0;
static int nextseqnum = 0;

static struct pkt recv_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expected_seqnum = 0;

/* new A_output function for sr */
void A_output(struct msg message) {
  struct pkt pkt;
  int i;

  /* Check if the window is full or not*/
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE >= WINDOWSIZE) {
    printf("----A: New message arrives, send window is full\n");
    window_full++;
    return;
  }

  /* Create the packet */
  pkt.seqnum = nextseqnum;
  pkt.acknum = -1;
  for (i = 0; i < 20; ++i)
      pkt.payload[i] = message.data[i];
  pkt.checksum = pkt.seqnum + pkt.acknum;
  for (i = 0; i < 20; ++i)
      pkt.checksum += pkt.payload[i];

  /* Save and send */
  window[nextseqnum] = pkt;
  acked[nextseqnum] = false;
  printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
  printf("Sending packet %d to layer 3\n", pkt.seqnum);
  tolayer3(0, pkt);

  /* timer*/
  if (base == nextseqnum)
      starttimer(0, TIMEOUT);

  nextseqnum = (nextseqnum + 1) % SEQSPACE;
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet) {
  int acknum = packet.acknum;
  int i;
  int in_window;
  /* Check for corruption */
  int checksum = packet.seqnum + packet.acknum;
  for (i = 0; i < 20; i++)
      checksum += packet.payload[i];

  if (checksum != packet.checksum) {
      printf("----A: corrupted ACK received, ignoring\n");
      return;
  }
  in_window = (base <= acknum && acknum < nextseqnum) || (base > nextseqnum && (acknum >= base || acknum < nextseqnum));
  if (!in_window) {
      printf("----A: ACK %d is outside of current window, ignoring\n", acknum);
      return;
    }
  if (acked[acknum]) {
      printf("----A: duplicate ACK %d received, do nothing!\n", acknum);
      return;
  }

  /* Mark acknowledged */
  acked[acknum] = true;
  printf("----A: uncorrupted ACK %d is received\n", acknum);
  printf("----A: ACK %d is not a duplicate\n", acknum);
  new_ACKs++;

  while (acked[base]) {
      base = (base + 1) % SEQSPACE;
  }

  /* If window is empty, stop timer else restart */
  if (base == nextseqnum) {
      stoptimer(0);
  } else {
      stoptimer(0);
      starttimer(0, TIMEOUT);
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void) {
  printf("----A: time out, resend packets!\n");
  printf("----A: Resending packet %d\n", base);
  starttimer(0, TIMEOUT);

  tolayer3(0, window[base]);
  packets_resent++;
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  int i;
  base = 0;
  nextseqnum = 0;

  for (i = 0; i < SEQSPACE; i++) {
    acked[i] = false;
  }
}



/********* Receiver (B)  variables and procedures ************/


static int B_nextseqnum;   /* the sequence number for the next packets sent by B */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet) {
  int i;
  struct pkt ackpkt;
  int checksum = packet.seqnum + packet.acknum;
  
  for (i = 0; i < 20; i++) {
      checksum += packet.payload[i];
  }

  if (checksum != packet.checksum) {
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
      ackpkt.acknum = (expected_seqnum + SEQSPACE - 1) % SEQSPACE;
      ackpkt.seqnum = NOTINUSE;
      ackpkt.checksum = ackpkt.acknum + ackpkt.seqnum;
      for (i = 0; i < 20; i++) ackpkt.payload[i] = 0;
      tolayer3(B, ackpkt);
      return;
  }
  packets_received++;

  if (!received[packet.seqnum]) {
      recv_buffer[packet.seqnum] = packet;
      received[packet.seqnum] = true;
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
  } else {
      printf("----B: duplicate packet %d received, resend ACK!\n", packet.seqnum);
  }

  ackpkt.acknum = packet.seqnum;
  ackpkt.seqnum = NOTINUSE;
  for (i = 0; i < 20; i++) ackpkt.payload[i] = 0;
  ackpkt.checksum = ackpkt.acknum + ackpkt.seqnum;
  tolayer3(B, ackpkt);

  while (received[expected_seqnum]) {
     tolayer5(B, recv_buffer[expected_seqnum].payload);
     received[expected_seqnum] = false;
     expected_seqnum = (expected_seqnum + 1) % SEQSPACE;
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    int i;
    expected_seqnum = 0;
    B_nextseqnum = 1;
    for (i = 0; i < SEQSPACE; i++) {
        received[i] = false;
    }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
