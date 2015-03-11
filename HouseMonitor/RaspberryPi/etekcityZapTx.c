/*
 *  etekcityZapTx.c:
 *  Etekcity ZAP 5LX 433.92MHz 5 channel outlet controller
 *
 * Requires: wiringPi (http://wiringpi.com)
 * Copyright (c) 2015 http://shaunsbennett.com/piblog
 ***********************************************************************
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <wiringPi.h>

#define	BCM_PIN_OUT	24  // GPIO 24
#define	PIN_OUT	BCM_PIN_OUT
//
#define DELAY_PULSE 184
#define DELAY_MAX_WAIT 99
#define DELAY_SYNC 5000

#define ADDR_PREAMBLE 85
#define ADDR_SETON 3
#define ADDR_SETOFF 12

#define REPEAT_BROADCAST 6

#define CHECK_BIT(var,pos) (((var) & (1<<(pos)))>0)

char *usage = "Usage: etekcityZapTx all|address[1-5] on|off\n"
              "       etekcityZapTx -h";

#define ADDR_LIST_SIZE 5
int ADDR_LIST[] = {339,348,368,464,848};


void doWrite(unsigned int bit,unsigned int howLong)
{
    digitalWrite (PIN_OUT, bit) ;
    delayMicroseconds  (howLong) ;
}

void doBroadcastBit(unsigned int bit)
{
    int delayStart = DELAY_PULSE;
    int delayStop = DELAY_PULSE;
    if (bit == 0)
    {
        delayStop = DELAY_PULSE * 3;
    }
    else
    {
        delayStart = DELAY_PULSE * 3;
    }
    digitalWrite (PIN_OUT, HIGH) ;
    while (delayStart>0)
    {
        delayMicroseconds( (DELAY_MAX_WAIT > delayStart) ? delayStart : DELAY_MAX_WAIT  );
        delayStart-= DELAY_MAX_WAIT;
    }

    digitalWrite (PIN_OUT, LOW) ;
    while (delayStop>0)
    {
        delayMicroseconds( (DELAY_MAX_WAIT > delayStop) ? delayStop : DELAY_MAX_WAIT  );
        delayStop-= DELAY_MAX_WAIT;
    }
}

void doBroadcastValue(unsigned int var, unsigned int bits)
{
    int i;
    for (i=bits-1; i>=0 ; i--)
    {
        doBroadcastBit(CHECK_BIT(var,i));
    }
}

void doBroadcastMessage(unsigned int address, unsigned int var)
{
    doBroadcastValue(ADDR_PREAMBLE,8);
    doBroadcastValue(address, 12);
    doBroadcastValue( ( var > 0 ) ? ADDR_SETON : ADDR_SETOFF , 4);
    doBroadcastValue(0,1);
    delayMicroseconds(DELAY_SYNC);
}

int main (int argc, char *argv [])
{

    if (argc < 3)
    {
        fprintf (stderr, "%s\n", usage) ;
        return 1 ;
    }

    if (strcasecmp (argv [1], "-h") == 0)
    {
        printf ("%s\n",  usage) ;
        return 0 ;
    }

    if ( !((strcasecmp (argv [2], "on") == 0) || (strcasecmp (argv [2], "off") == 0)) )
    {
        printf ("%s\n",  usage) ;
        return 0 ;
    }
    int var = (strcasecmp (argv [2], "on") == 0) ? 1 : 0;

    int address;
    if((strcasecmp (argv [1], "all") == 0) )
    {
        argv[1] = "0";
    }
    if ( (sscanf (argv[1], "%i", &address)!=1) || address < 0 || address > 5 )
    {
        printf ("%s\n",  usage) ;
        return 0 ;
    }

	//setup as a non-root user provided the GPIO pins have been exported before-hand using the gpio program.
    char *command;
    asprintf (&command, "gpio export %d out",PIN_OUT);
    if (system(command) == -1)
    {
        fprintf (stderr, "Unable to setup wiringPi: %s\n", strerror (errno)) ;
        return 1 ;
    }
    free(command);

	//setup as a non-root user provided the GPIO pins have been exported before-hand.
    wiringPiSetupSys();

    int i,j;
    for(j=0; j < ADDR_LIST_SIZE; j++)
    {
        if(address==0 || address==j+1)
        {
            for (i=0; i<REPEAT_BROADCAST ; i++ )
            {
                doBroadcastMessage(ADDR_LIST[j],var);
            }
            printf ("etekcityZapTx=address:%d,command:%s\n", j+1, argv[2]) ;
        }
    }

    return 0 ;
}
