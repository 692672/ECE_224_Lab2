/****************************************************************************
*  Copyright (C) 2008-2012 by Michael Fischer.
*
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the author nor the names of its contributors may
*     be used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
*  THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
*  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
*  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
*  SUCH DAMAGE.
*
****************************************************************************
*  History:
*
*  07.11.2008  mifi  First Version, based on FatFs example.
*  11.02.2012  mifi  Tested with EIR.
*  23.08.2012  mifi  Tested with an Altera DE1.
****************************************************************************/
#define __MAIN_C__

/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <time.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

#include "ui.h"



/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/
#define TRUE 1
#define FALSE 0

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Prototypes                                                     */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/
static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/***************************************************************************/
/*  TimerFunction                                                          */
/*                                                                         */
/*  This timer function will provide a 10ms timer and                      */
/*  call ffs_DiskIOTimerproc.                                              */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

/***************************************************************************/
/*  IoInit                                                                 */
/*                                                                         */
/*  Init the hardware like GPIO, UART, and more...                         */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */


// Macros to clear lcd
#define ESC 27
#define CLEAR_LCD_STRING "[2J"

#define MAX_NUM_SONGS 20

// Constant variables
const uint32_t bytesPerLoop = 1024;
const uint32_t forwardStep_bytesPerLoop = 16;
const uint32_t backStepSize = 1U << 14;
const uint32_t forwardStepSize = 1U << 12; // smaller than backStepSize


uint32_t num_wav_files = 0;
volatile int edge_capture; // Used for button ISRs

char fileName[MAX_NUM_SONGS][20];
unsigned long fileSize[MAX_NUM_SONGS];

int songPointer = 0;

int isPlaying = 0; // State machine thing
int doubleSpeed = 0; // if 0 play at regular speed
int forwardState = 0;
int backwardState = 0;
int playPauseState = 0;
int j;

char* lcdString[40]; // Printing to LCD


uint32_t songBytesRemaining = 0;
uint32_t bytesRead = 0;
uint8_t res = 0;

alt_up_audio_dev * audio_dev_ptr; // made this global
// End of globals


static
FRESULT scan_files(char *path)
{
    DIR dirs;
    FRESULT res;
    uint8_t i;
    char *fn;

    if ((res = f_opendir(&dirs, path)) == FR_OK) {
        i = (uint8_t)strlen(path);
        while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0]) {
            if (_FS_RPATH && Finfo.fname[0] == '.')
                continue;
#if _USE_LFN
            fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
            fn = Finfo.fname;
#endif
            if (Finfo.fattrib & AM_DIR) {
                acc_dirs++;
                *(path + i) = '/';
                strcpy(path + i + 1, fn);
                res = scan_files(path);
                *(path + i) = '\0';
                if (res != FR_OK)
                    break;
            } else {
                //      xprintf("%s/%s\n", path, fn);
                acc_files++;
                acc_size += Finfo.fsize;
            }
        }
    }

    return res;
}


static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}

static
void display_help(void)
{
    xputs("dd <phy_drv#> [<sector>] - Dump sector\n"
          "di <phy_drv#> - Initialize disk\n"
          "ds <phy_drv#> - Show disk status\n"
          "bd <addr> - Dump R/W buffer\n"
          "be <addr> [<data>] ... - Edit R/W buffer\n"
          "br <phy_drv#> <sector> [<n>] - Read disk into R/W buffer\n"
          "bf <n> - Fill working buffer\n"
          "fi <log drv#> - Force initialize the logical drive\n"
          "fs [<path>] - Show logical drive status\n"
          "fl [<path>] - Directory listing\n"
          "fo <mode> <file> - Open a file\n"
          "fc - Close a file\n"
          "fe - Seek file pointer\n"
          "fr <len> - Read file\n"
          "fd <len> - Read and dump file from current fp\n"
#if _USE_MKFS != 0
          "fm <log drv#> <partition rule> <cluster size> - Create file system\n"
#endif
          "fz [<len>] - Get/Set transfer unit for fr command\n"
#if 0
          "t [<year> <mon> <mday> <hour> <min> <sec>] Time read/set\n"
#endif
          "h view help (this)\n");
}


int isWav(char* fileName) {
	char* c = fileName;
	int stringLength = 0;
	while (*c != '\0') { // Don't forget to deference!
		c++;
		stringLength++;
	}
	// Just check the last char
	if (fileName[stringLength-1] == 'V' || fileName[stringLength-1] == 'v') {
		return TRUE;
	} else {
		return FALSE;
	}
}

void songIndex() {
	long p1;
	uint32_t s1, s2;
	int count = 0;
	int res;
	char * ptr;

	// this is bad
	*ptr = '\n';

	res = f_opendir(&Dir, ptr);
	if (res) {
		put_rc(res);
		return;
	}
	p1 = s1 = s2 = 0;
	for (;;) {
		res = f_readdir(&Dir, &Finfo);
		if ((res != FR_OK) || !Finfo.fname[0])
			break;
		if (Finfo.fattrib & AM_DIR) {
			s2++;
		} else {
			s1++;
			p1 += Finfo.fsize;
		}
		if ( isWav( &(Finfo.fname[0]) ) ) {
			strcpy(&fileName[count],&(Finfo.fname[0]));
			fileSize[count] = Finfo.fsize;
			xprintf("count %d, %s, file size: %d\n", count, &fileName[count], fileSize[count]);
			count++;
		}
	}
	num_wav_files = count;
}

void displayLCD(char* string) {
  FILE *lcd;
  lcd = fopen("/dev/lcd_display", "w");
  if (lcd != NULL ) {
    fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);
	fprintf(lcd, string);
  }
  fclose( lcd );
  return;
}

void playBufferBytes() {
	uint32_t i = 0, leftWord = 0, rightWord = 0;
	for (i = 0; i < bytesRead; i+= (4 + doubleSpeed*4) ) {
		leftWord = 0;
		rightWord = 0;

		rightWord |= Buff[i+3];
		rightWord <<= 8;
		rightWord |= Buff[i+2];

		leftWord |= Buff[i+1];
		leftWord <<= 8;
		leftWord |= Buff[i+0];

		while (alt_up_audio_write_fifo_space(audio_dev_ptr, ALT_UP_AUDIO_RIGHT) < 1); // returns in units of words
		while(isPlaying == 0); // If in the pause mode, then just idle
		alt_up_audio_write_fifo (audio_dev_ptr, &leftWord, 1, ALT_UP_AUDIO_LEFT);
		alt_up_audio_write_fifo (audio_dev_ptr, &rightWord, 1, ALT_UP_AUDIO_RIGHT);
	}
}

// Assume fclose has been called
void recover() {
    put_rc(f_mount((uint8_t) 0, &Fatfs[0]));
	put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
	songBytesRemaining = fileSize[songPointer];
}


#if 1
static void handle_button_interrupts(void* context, alt_u32 id) {

	usleep(10000); // this is required

	// Hacky solution: Since the switch block will not be entered when Button 0 is released, toggle here.
	if (doubleSpeed == 1) {
		doubleSpeed = 0;
	}

	// This switch block will not be entered on rising edges (button release)
	switch(IORD(BUTTON_PIO_BASE,0)) {
		case 0x7: // Backwards
			if (isPlaying) { // Seek backwards
				printf("Seek backwards\n");

				while (IORD(BUTTON_PIO_BASE,0) != 0xf) { // break when all buttons are released

					uint32_t forwardPlayBytesRemaining = forwardStepSize;
					uint32_t f_ptr;
					if (File1.fptr < backStepSize) {
						f_ptr = 0;
					} else {
						f_ptr = File1.fptr - backStepSize;
					}
					res = f_lseek(&File1, f_ptr);
					if (res != FR_OK) {
						printf("Error with f_lseek()\n");
						put_rc(f_close(&File1));
						// Try to recover
						recover();
						return;
					}

					while ( forwardPlayBytesRemaining > 0) {
						if ( forwardPlayBytesRemaining >= bytesPerLoop) {
							forwardPlayBytesRemaining -= bytesPerLoop;
						} else {
							forwardPlayBytesRemaining = 0;
						}

						res = f_read(&File1, &Buff, bytesPerLoop, &bytesRead); // size in bytes to be read
						if (res != FR_OK) {
							printf("Error with f_read()\n");
							f_close(&File1);
							// try to recover
							recover();
							return;
						}
						playBufferBytes();
					} // End of forward play loop
				} // End of button poll loop

			} else { // Back track
#if 0
				if(backwardState == 0){
					backwardState = backwardState+1;
				}
				for(j=0;j<1000;j++);
				if(backwardState == 2){
					return;
				}
#endif
				put_rc(f_close(&File1));
				songPointer--;
				if(songPointer == -1){
					songPointer = num_wav_files-1;
				}
				put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
				songBytesRemaining = fileSize[songPointer];
				sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
				displayLCD(lcdString);
			}
			backwardState=0;
			break;
		case 0xb: // Stop
			printf("Stop\n");
			put_rc(f_lseek(&File1, 0));
			if (isPlaying) {
				isPlaying = 0;
			}
			break;
		case 0xd: // Play/Pause
#if 0
			if(playPauseState == 0){
				playPauseState = playPauseState+1;
			}
			for(j=0;j<1000;j++);
			if(playPauseState == 2){
				return;
			}
#endif
			if (isPlaying == 0) {
				printf("Play\n");
				isPlaying = 1;
			} else {
				printf("Pause\n");
				isPlaying = 0;
			}
			playPauseState=0;
			break;
		case 0xe: // Forwards
			if (isPlaying) {
				// toggle doubleSpeed
				if (doubleSpeed == 1) {
					doubleSpeed = 0;
				} else {
					doubleSpeed = 1;
				}
			} else { // Not playing
#if 0
				if(forwardState == 0){
					forwardState = forwardState+1;
				}
				for(j=0;j<1000;j++);
				if(forwardState == 2){
					return;
				}
#endif
				put_rc(f_close(&File1));
				songPointer++;
				if(songPointer == num_wav_files){
					songPointer = 0;
				}
				put_rc(f_open(&File1, fileName[songPointer], 1));
				songBytesRemaining = fileSize[songPointer];
				sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
				displayLCD(lcdString);
			}
			forwardState=0;
			break;
		default:
			break;
	}
	//Reset the Button's edge capture register.
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0);
}


static void init_button_pio() {
  // Enable all 4 button interrupts
  IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0xf);
  // Reset the edge capture register
  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x0);
  // Register ISR
  alt_irq_register( BUTTON_PIO_IRQ, (void*) &edge_capture, handle_button_interrupts);
}
#endif
/***************************************************************************/
/*  main                                                                   */
/***************************************************************************/
int main(void) {

	// Main local variables
	int i = 0;
	unsigned int leftWord = 0;
	unsigned int rightWord = 0;
	// End of main local variables

    // open the Audio port
    audio_dev_ptr = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev_ptr == NULL) {
    	alt_printf ("Error: could not open audio device \n");
    } else {
    	alt_printf ("Opened audio device \n");
    }
    IoInit();

    xputs(PSTR("FatFs module test monitor\n"));
    xputs(_USE_LFN ? "LFN Enabled" : "LFN Disabled");
    xprintf(", Code page: %u\n", _CODE_PAGE);

    display_help();

    // Our initializations

    // di 0
    xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) 0));

    // fi 0
    put_rc(f_mount((uint8_t) 0, &Fatfs[0]));

	songIndex(); // must happen before displaying lcd
	sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
	displayLCD(lcdString);

    // Initilize interrupts
    init_button_pio();

    // Open the first file or else pressing play will error
	put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
	songBytesRemaining = fileSize[songPointer];

	// End of our initializations

    while (1) { // Main while loop
		while ( songBytesRemaining > 0) { // song while loop

			if (songBytesRemaining >= bytesPerLoop) {
				songBytesRemaining -= bytesPerLoop;
			} else {
				songBytesRemaining = 0;
			}
			res = f_read(&File1, &Buff, bytesPerLoop, &bytesRead); // size in bytes to be read
			if (res != FR_OK) {
				put_rc(res);
				f_close(&File1);

				// Try to recover: seems to work well

				f_open(&File1, fileName[songPointer], 1);
				f_lseek(&File1, fileSize[songPointer] - songBytesRemaining);
				break;
			}
			playBufferBytes();

		} // End of song while loop

		// When the song finished playing, reload it, and pause
		isPlaying = 0;
		printf(".wav file finished\n");
		put_rc(f_close(&File1));
		put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
		songBytesRemaining = fileSize[songPointer];
    } // End of main while loop

    return (0);
}
/*** EOF ***/
