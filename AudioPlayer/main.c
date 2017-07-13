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
#if _USE_LFN
char Lfname[512];
#endif

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */

// Our shit
// Macros to clear lcd
#define ESC 27
#define CLEAR_LCD_STRING "[2J"
#define CMD_LINE 0 // enable command line prompt

#define numSongs 13 // hard coded
volatile int edge_capture; // Used for button ISRs
char fileName[numSongs][20];

unsigned long fileSize[numSongs];
int songPointer = 0;

int isPlaying = 0; // State machine thing
int doubleSpeed = 0; // if 0 play at regular speed

int previousIsPlaying = 0;
uint32_t previousClust = 0;
char* lcdString[40]; // Printing to LCD
long int musicWordCounter;

uint32_t songByteCount = 0; // used by the backseek

const int bytesPerLoop = 1024;
int previousButton0 = 1; // active low, so default 1

// tbd
const int backStepSize = 1 << 12;
const int forwardStepSize = 1 << 11; // smaller than backStepSize

alt_up_audio_dev * audio_dev; // made this global
// end of our shit

/* TODO list
Fast forward button should be held to enable double speed
Returning from fast forward distorts sound for some reason?
Implement backtracking
Make buttons more robust..
*/

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

void songIndex() {
	long p1;
	uint32_t s1, s2;
	int count = 0;
	int res;
	char * ptr;
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
			xprintf("count %d, %s, %d\n", count, &fileName[count], fileSize[count]);
			count++;
		}

	}
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

#if 1
static void handle_button_interrupts(void* context, alt_u32 id) {
	// We just want the ISR stuff for the interrupt feature
	// Not using the edge register..use data reg instead lemao
	uint8_t res = 0;

	// Not sure if required
	usleep(100000);

	// Since the switch block will not be entered when Button 0 is released, toggle here.
	if (doubleSpeed == 1) {
		doubleSpeed = 0;
	}

	// This switch block will not be entered on rising edges (button release)
	switch(IORD(BUTTON_PIO_BASE,0)) {
		case 0x7: // backwards

			if (isPlaying) { // back seek
				printf("Backseek button pressed\n");
				while (IORD(BUTTON_PIO_BASE,0) != 0xf) { // break when all buttons are released

					int byteCounter = forwardStepSize;

					uint8_t ret;
					int32_t bytePtr = f_tell(&File1) - backStepSize;
					if (bytePtr < 0) {
						bytePtr = 0;
						printf("yeeeeeeeeee\n");
					}
					ret = f_lseek(&File1, bytePtr);
					if (ret != FR_OK) {
						printf("This is ret = %d",ret);
						printf("Error with f_lseek(), bytePtr: %d\n", bytePtr);
						put_rc(ret); // error occurrence

						// hacky scrap because we were getting
						// errors for backtracking too much?????+
						// handle the error here
						// wait for buttons to be released
						while(IORD(BUTTON_PIO_BASE,0) != 0xf);
						ret = f_open(&File1, fileName[songPointer], 1); // always 1
						songByteCount = fileSize[songPointer];
						break;
					}

					while ( byteCounter > 0) {
						if ((uint32_t) byteCounter >= bytesPerLoop) {
							byteCounter -= bytesPerLoop;
						} else {
							byteCounter = 0;
						}
						// play shit
						ret = f_read(&File1, &Buff, bytesPerLoop, &bytesPerLoop); // size in bytes to be read
						if (res != FR_OK) {
							printf("Error with f_read()\n");
							put_rc(res); // error occurrence
							break;
						}
						songByteCount -= bytesPerLoop;

						int i = 0;
						for (i = 0; i < bytesPerLoop; i+= 4) {
							uint32_t leftWord = 0;
							uint32_t rightWord = 0;

							leftWord |= Buff[i+1];
							leftWord <<= 8;
							leftWord |= Buff[i+0];

							rightWord |= Buff[i+3];
							rightWord <<= 8;
							rightWord |= Buff[i+2];

							// play shit
							while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT) < 1); // returns in units of words
							while(isPlaying == 0);
							// musicWordCounter++;
							alt_up_audio_write_fifo (audio_dev, &leftWord, 1, ALT_UP_AUDIO_LEFT);
							alt_up_audio_write_fifo (audio_dev, &rightWord, 1, ALT_UP_AUDIO_RIGHT);
						} // end of for

					} // end of playing loop
					usleep(10000); // 10 ms
				} // end of button poll while

			} else {
				songPointer--;
				if(songPointer == -1){
					songPointer = numSongs-1;
				}
				//printf("songPointer: %d\n",songPointer);
				put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
				songByteCount = fileSize[songPointer];
				sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
				displayLCD(lcdString);

				// not sure if this wait is still required...
				// while(IORD(BUTTON_PIO_BASE,0) != 0xf);
			}
			break;
		case 0xb: // stop
			printf("stop\n");
			put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
			songByteCount = fileSize[songPointer];
			if (isPlaying) { // if playing, then pause it
				isPlaying = 0;
			} //
			break;
		case 0xd: // play/pause
			usleep(100);
			if (IORD(BUTTON_PIO_BASE,0) == 0xf) { // no trigger on release
				break;
			}
			if (isPlaying == 0) { // pressed play
				printf("Pressed play\n");
				isPlaying = 1;
				//File1.clust = previousClust;
			} else { // pressed pause
				printf("Pressed pause\n");
				isPlaying = 0;
				//previousClust = File1.clust;
			}
			// wait for the button to be released before breaking
			break;
		case 0xe: // forwards
			//printf("isPlaying :%d\n", isPlaying);
			if (isPlaying) {
				// toggle doubleSpeed
				if (doubleSpeed == 1) {
					doubleSpeed = 0;
				} else {
					doubleSpeed = 1;
				}
			} else { // not playing
				songPointer++;
				if(songPointer == numSongs){
					songPointer = 0;
				}
				put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
				songByteCount = fileSize[songPointer];
				sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
				displayLCD(lcdString);
				// getting lucky with race conditions tbh
//				while(IORD(BUTTON_PIO_BASE,0) != 0xf);
			}
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
int main(void)
{
	// tests
	char* test = "test.wav";
	printf("test isWav(): %d\n", isWav(test));
	// end of tests

	// our shit
	int i = 0;
	unsigned int leftWord = 0;
	unsigned int rightWord = 0;
	unsigned int buttonData = 0;
	// end of our shit

	int fifospace;
    char *ptr, *ptr2;
    long p1, p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;                  /* Pointer to file system object */


    /* used for audio record/playback */

    // open the Audio port
    audio_dev = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev == NULL)
    alt_printf ("Error: could not open audio device \n");
    else
    alt_printf ("Opened audio device \n");

    IoInit();

    //IOWR(SEVEN_SEG_PIO_BASE,0,0xFF07);

    xputs(PSTR("FatFs module test monitor\n"));
    xputs(_USE_LFN ? "LFN Enabled" : "LFN Disabled");
    xprintf(", Code page: %u\n", _CODE_PAGE);

    display_help();

    // Our Initialization shit

    // di 0
    xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) 0));
    // fi 0
    put_rc(f_mount((uint8_t) 0, &Fatfs[0]));

    // must happen before displaying lcd
	songIndex();
	sprintf(lcdString, "%d: %s\n", songPointer, fileName[songPointer]);
	displayLCD(lcdString);

    // initilize interrupts
    init_button_pio();

    // open the first file or else pressing play will fry
	put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
	songByteCount = fileSize[songPointer];
    // End of our initialization shit

#if _USE_LFN
    Finfo.lfname = Lfname;
    Finfo.lfsize = sizeof(Lfname);
#endif


    // Design spec: When the song ends, we go into pause mode

    // Read the maximum amount of bytes
    // 256 (32-bit) words which is 1024 bytes
    musicWordCounter = 0;

#if !CMD_LINE
    while (1) {

		while ( songByteCount > 0) {

			if ((uint32_t) songByteCount >= bytesPerLoop) {
				songByteCount -= bytesPerLoop;
			} else {
				songByteCount = 0;
			}
			res = f_read(&File1, &Buff, bytesPerLoop, &bytesPerLoop); // size in bytes to be read
			if (res != FR_OK) {
				put_rc(res); // error occurrence
				break;
			}
			//
			for (i = 0; i < bytesPerLoop; i+= (4 + doubleSpeed*4) ) {
				leftWord = 0;
				rightWord = 0;

				leftWord |= Buff[i+1];
				leftWord <<= 8;
				leftWord |= Buff[i+0];

				rightWord |= Buff[i+3];
				rightWord <<= 8;
				rightWord |= Buff[i+2];

				while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT) < 1); // returns in units of words
				while(isPlaying == 0);

				musicWordCounter++;

				alt_up_audio_write_fifo (audio_dev, &leftWord, 1, ALT_UP_AUDIO_LEFT);
				alt_up_audio_write_fifo (audio_dev, &rightWord, 1, ALT_UP_AUDIO_RIGHT);
			} // end for loop
		} // end while

		//printf("song over\n");
		isPlaying = 0;
		put_rc(f_open(&File1, fileName[songPointer], 1)); // always 1
		songByteCount = fileSize[songPointer];
		//printf("after song is over isPlaying=%d\n",isPlaying);
    }
#endif

#if CMD_LINE
    for (;;) {

        get_line(Line, sizeof(Line));

        ptr = Line;
        switch (*ptr++) {

        case 'm':              /* System memroy/register controls */
            switch (*ptr++) {
            case 'd':          /* md <address> [<count>] - Dump memory */
                if (!xatoi(&ptr, &p1))
                    break;
                if (!xatoi(&ptr, &p2))
                    p2 = 128;
                for (ptr = (char *) p1; p2 >= 16; ptr += 16, p2 -= 16)
                    put_dump((uint8_t *) ptr, (uint32_t) ptr, 16);
                if (p2)
                    put_dump((uint8_t *) ptr, (uint32_t) ptr, p2);
                break;
            }
            break;

        case 'd':              /* Disk I/O layer controls */
            switch (*ptr++) {
            case 'd':          /* dd [<drv> [<lba>]] - Dump secrtor */
                if (!xatoi(&ptr, &p1)) {
                    p1 = drv;
                } else {
                    if (!xatoi(&ptr, &p2))
                        p2 = sect;
                }
                drv = (uint8_t) p1;
                sect = p2 + 1;
                res = disk_read((uint8_t) p1, Buff, p2, 1);
                if (res) {
                    xprintf("rc=%d\n", (uint16_t) res);
                    break;
                }
                xprintf("D:%lu S:%lu\n", p1, p2);
                for (ptr = (char *) Buff, ofs = 0; ofs < 0x200; ptr += 16, ofs += 16)
                    put_dump((uint8_t *) ptr, ofs, 16);
                break;

            case 'i':          /* di <drv> - Initialize disk */
                if (!xatoi(&ptr, &p1))
                    break;
                xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) p1));
                break;

            case 's':          /* ds <drv> - Show disk status */
                if (!xatoi(&ptr, &p1))
                    break;
                if (disk_ioctl((uint8_t) p1, GET_SECTOR_COUNT, &p2) == RES_OK) {
                    xprintf("Drive size: %lu sectors\n", p2);
                }
                if (disk_ioctl((uint8_t) p1, GET_SECTOR_SIZE, &w1) == RES_OK) {
                    xprintf("Sector size: %u bytes\n", w1);
                }
                if (disk_ioctl((uint8_t) p1, GET_BLOCK_SIZE, &p2) == RES_OK) {
                    xprintf("Block size: %lu sectors\n", p2);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_TYPE, &b1) == RES_OK) {
                    xprintf("MMC/SDC type: %u\n", b1);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_CSD, Buff) == RES_OK) {
                    xputs("CSD:\n");
                    put_dump(Buff, 0, 16);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_CID, Buff) == RES_OK) {
                    xputs("CID:\n");
                    put_dump(Buff, 0, 16);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_OCR, Buff) == RES_OK) {
                    xputs("OCR:\n");
                    put_dump(Buff, 0, 4);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_SDSTAT, Buff) == RES_OK) {
                    xputs("SD Status:\n");
                    for (s1 = 0; s1 < 64; s1 += 16)
                        put_dump(Buff + s1, s1, 16);
                }
                break;

            case 'c':          /* Disk ioctl */
                switch (*ptr++) {
                case 's':      /* dcs <drv> - CTRL_SYNC */
                    if (!xatoi(&ptr, &p1))
                        break;
                    xprintf("rc=%d\n", disk_ioctl((uint8_t) p1, CTRL_SYNC, 0));
                    break;
                case 'e':      /* dce <drv> <start> <end> - CTRL_ERASE_SECTOR */
                    if (!xatoi(&ptr, &p1) || !xatoi(&ptr, (long *) &blk[0]) || !xatoi(&ptr, (long *) &blk[1]))
                        break;
                    xprintf("rc=%d\n", disk_ioctl((uint8_t) p1, CTRL_ERASE_SECTOR, blk));
                    break;
                }
                break;
            }
            break;

        case 'b':              /* Buffer controls */
            switch (*ptr++) {
            case 'd':          /* bd <addr> - Dump R/W buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                for (ptr = (char *) &Buff[p1], ofs = p1, cnt = 32; cnt; cnt--, ptr += 16, ofs += 16)
                    put_dump((uint8_t *) ptr, ofs, 16);
                break;

            case 'e':          /* be <addr> [<data>] ... - Edit R/W buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                if (xatoi(&ptr, &p2)) {
                    do {
                        Buff[p1++] = (uint8_t) p2;
                    } while (xatoi(&ptr, &p2));
                    break;
                }
                for (;;) {
                    xprintf("%04X %02X-", (uint16_t) (p1), (uint16_t) Buff[p1]);
                    get_line(Line, sizeof(Line));
                    ptr = Line;
                    if (*ptr == '.')
                        break;
                    if (*ptr < ' ') {
                        p1++;
                        continue;
                    }
                    if (xatoi(&ptr, &p2))
                        Buff[p1++] = (uint8_t) p2;
                    else
                        xputs("???\n");
                }
                break;

            case 'r':          /* br <drv> <lba> [<num>] - Read disk into R/W buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                if (!xatoi(&ptr, &p2))
                    break;
                if (!xatoi(&ptr, &p3))
                    p3 = 1;
                xprintf("rc=%u\n", (uint16_t) disk_read((uint8_t) p1, Buff, p2, p3));
                break;

            case 'f':          /* bf <val> - Fill working buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                memset(Buff, (uint8_t) p1, sizeof(Buff));
                break;

            }
            break;

        case 'f':              /* FatFS API controls */
            switch (*ptr++) {

            case 'i':          /* fi <vol> - Force initialized the logical drive */
                if (!xatoi(&ptr, &p1))
                    break;
                put_rc(f_mount((uint8_t) p1, &Fatfs[p1]));
                break;

            case 's':          /* fs [<path>] - Show volume status */
                res = f_getfree(ptr, (uint32_t *) & p2, &fs);
                if (res) {
                    put_rc(res);
                    break;
                }
                xprintf("FAT type = FAT%u\nBytes/Cluster = %lu\nNumber of FATs = %u\n"
                        "Root DIR entries = %u\nSectors/FAT = %lu\nNumber of clusters = %lu\n"
                        "FAT start (lba) = %lu\nDIR start (lba,clustor) = %lu\nData start (lba) = %lu\n\n...",
                        ft[fs->fs_type & 3], (uint32_t) fs->csize * 512, fs->n_fats,
                        fs->n_rootdir, fs->fsize, (uint32_t) fs->n_fatent - 2, fs->fatbase, fs->dirbase, fs->database);
                acc_size = acc_files = acc_dirs = 0;
                res = scan_files(ptr);
                if (res) {
                    put_rc(res);
                    break;
                }
                xprintf("\r%u files, %lu bytes.\n%u folders.\n"
                        "%lu KB total disk space.\n%lu KB available.\n",
                        acc_files, acc_size, acc_dirs, (fs->n_fatent - 2) * (fs->csize / 2), p2 * (fs->csize / 2)
                    );
                break;

            case 'l':          /* fl [<path>] - Directory listing */

            	while (*ptr == ' ')
                    ptr++;
                res = f_opendir(&Dir, ptr);
                if (res) {
                    put_rc(res);
                    break;
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
                    //xprintf("&(Finfo.fname[0]): %s\n", &(Finfo.fname[0]));

                    //if ( isWav( &(Finfo.fname[0]) ) ) {
                    xprintf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9lu  %s",
                            (Finfo.fattrib & AM_DIR) ? 'D' : '-',
                            (Finfo.fattrib & AM_RDO) ? 'R' : '-',
                            (Finfo.fattrib & AM_HID) ? 'H' : '-',
                            (Finfo.fattrib & AM_SYS) ? 'S' : '-',
                            (Finfo.fattrib & AM_ARC) ? 'A' : '-',
                            (Finfo.fdate >> 9) + 1980, (Finfo.fdate >> 5) & 15, Finfo.fdate & 31,
                            (Finfo.ftime >> 11), (Finfo.ftime >> 5) & 63, Finfo.fsize, &(Finfo.fname[0]));
#if _USE_LFN
                    for (p2 = strlen(Finfo.fname); p2 < 14; p2++)
                        xputc(' ');
                    xprintf("%s\n", Lfname);
#else
                    xputc('\n');
#endif
                    //}
                }
                xprintf("%4u File(s),%10lu bytes total\n%4u Dir(s)", s1, p1, s2);
                res = f_getfree(ptr, (uint32_t *) & p1, &fs);
                if (res == FR_OK)
                    xprintf(", %10lu bytes free\n", p1 * fs->csize * 512);
                else
                    put_rc(res);
                break;

            case 'o':          /* fo <mode> <file> - Open a file */
                if (!xatoi(&ptr, &p1))
                    break;
                while (*ptr == ' ')
                    ptr++;
                printf("fo: ptr: %s, p1: %d\n", ptr, p1);
                put_rc(f_open(&File1, ptr, (uint8_t) p1));
                break;

            case 'c':          /* fc - Close a file */
                put_rc(f_close(&File1));
                break;

            case 'e':          /* fe - Seek file pointer */
                if (!xatoi(&ptr, &p1))
                    break;
                res = f_lseek(&File1, p1);
                put_rc(res);
                if (res == FR_OK)
                    xprintf("fptr=%lu(0x%lX)\n", File1.fptr, File1.fptr);
                break;

            case 'd':          /* fd <len> - read and dump file from current fp */
                if (!xatoi(&ptr, &p1))
                    break;
                ofs = File1.fptr;
                while (p1) {
                    if ((uint32_t) p1 >= 16) {
                        cnt = 16;
                        p1 -= 16;
                    } else {
                        cnt = p1;
                        p1 = 0;
                    }
                    res = f_read(&File1, Buff, cnt, &cnt);
                    if (res != FR_OK) {
                        put_rc(res);
                        break;
                    }
                    if (!cnt)
                        break;
                    put_dump(Buff, ofs, cnt);
                    ofs += 16;
                }
                break;

            case 'p':          /* fd <len> - read and dump file from current fp */
                if (!xatoi(&ptr, &p1))
                    break;
                ofs = File1.fptr;
                //p1 is the number of bytes to read from the file

                
                // Read the maximum amount of bytes
                // 256 (32-bit) words which is 1024 bytes

                musicWordCounter = 0;
                int bytesPerLoop = 1024;

                while (p1>0) {
					// read audio buffer
					if ((uint32_t) p1 >= bytesPerLoop) {
						p1 -= bytesPerLoop;
					} else {
						p1 = 0;
					}

					res = f_read(&File1, &Buff, bytesPerLoop, &bytesPerLoop); // size in bytes to be read
					if (res != FR_OK) {
						put_rc(res); // error occurrence
						break;
					}
					//printf("sclust: %d, clust: %d, dsect: %d\n", File1.sclust, File1.clust, File1.dsect);

					for (i = 0; i < bytesPerLoop; i+=4) {
						leftWord = 0;
						leftWord |= Buff[i+1];
						leftWord <<= 8;
						leftWord |= Buff[i+0];

						rightWord = 0;
						rightWord |= Buff[i+3];
						rightWord <<= 8;
						rightWord |= Buff[i+2];

						while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT) < 1); // returns in units of words
						musicWordCounter++;
						alt_up_audio_write_fifo (audio_dev, &leftWord, 1, ALT_UP_AUDIO_LEFT);
						alt_up_audio_write_fifo (audio_dev, &rightWord, 1, ALT_UP_AUDIO_RIGHT);
					}
                }

                break;

            case 'r':          /* fr <len> - read file */
                if (!xatoi(&ptr, &p1))
                    break;
                p2 = 0;
                Timer = 0;
                while (p1) {
                    if ((uint32_t) p1 >= blen) {
                        cnt = blen;
                        p1 -= blen;
                    } else {
                        cnt = p1;
                        p1 = 0;
                    }
                    res = f_read(&File1, Buff, cnt, &s2);
                    if (res != FR_OK) {
                        put_rc(res);
                        break;
                    }
                    p2 += s2;
                    if (cnt != s2)
                        break;
                }
                xprintf("%lu bytes read with %lu kB/sec.\n", p2, Timer ? (p2 / Timer) : 0);
                break;

            case 't':          /* ft <year> <month> <day> <hour> <min> <sec> <name> - Change timestamp */
                if (!xatoi(&ptr, &p1) || !xatoi(&ptr, &p2) || !xatoi(&ptr, &p3))
                    break;
                Finfo.fdate = ((p1 - 1980) << 9) | ((p2 & 15) << 5) | (p3 & 31);
                if (!xatoi(&ptr, &p1) || !xatoi(&ptr, &p2) || !xatoi(&ptr, &p3))
                    break;
                Finfo.ftime = ((p1 & 31) << 11) | ((p2 & 63) << 5) | ((p3 >> 1) & 31);
                put_rc(f_utime(ptr, &Finfo));
                break;

#if _FS_RPATH
            case 'g':          /* fg <path> - Change current directory */
                while (*ptr == ' ')
                    ptr++;
                put_rc(f_chdir(ptr));
                break;

            case 'j':          /* fj <drive#> - Change current drive */
                if (xatoi(&ptr, &p1)) {
                    put_rc(f_chdrive((uint8_t) p1));
                }
                break;
#if _FS_RPATH >= 2
            case 'q':          /* fq - Show current dir path */
                res = f_getcwd(Line, sizeof(Line));
                if (res)
                    put_rc(res);
                else
                    xprintf("%s\n", Line);
                break;
#endif
#endif
            case 'z':          /* fz [<rw size>] - Change R/W length for fr/fw/fx command */
                if (xatoi(&ptr, &p1) && p1 >= 1 && p1 <= sizeof(Buff))
                    blen = p1;
                xprintf("blen=%u\n", blen);
                break;
            }
            break;

        case 'h':
            display_help();
            break;

        }

    }
#endif
    /*
     * This return here make no sense.
     * But to prevent the compiler warning:
     * "return type of 'main' is not 'int'
     * we use an int as return :-)
     */
    return (0);
}


/*** EOF ***/
