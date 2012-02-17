#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <linux/telephony.h>
#include <time.h>

#define BUFSIZE	80

#define PHONE_MV_READ_REG	_IOWR ('q', 0xB0, unsigned short)
#define PHONE_MV_WRITE_REG	_IOW ('q', 0xB1, unsigned int)
#define PHONE_MV_SPI_TEST	_IO ('q', 0xB2)
#define PHONE_MV_STAT_SHOW	_IO ('q', 0xB5)

#define MV_TOOL_VER	"ver 1.2"

/* 
v1.2, Tzachi, Dec 01, Add start/stop ioctl in loopback.
*/

void sw_loopback(int fd);
int sw_tone(int fd);
int spi_test(int fd);




int main(int argc, char *argv[])
{
	char *name = "/dev/phone0";
	int fd, t, i, fdflags;
	int rc = 0;

	if (argc > 1)
		name = argv[1];

	/* open the device */
	fd = open(name, O_RDWR); 
	if (fd <= 0) {	
		printf("## Cannot open /dev/phone0 device.##\n");
		exit(2);
	}

	/* set some flags */
	fdflags = fcntl(fd,F_GETFL,0);
	fdflags |= O_NONBLOCK;
	fcntl(fd,F_SETFL,fdflags);

	while(1) {
		char str[32];
		int reg=0;
		int val=0;
		printf("\n\tMarvell VoIP Tool %s:\n",MV_TOOL_VER);
		printf("\t0. Read ProSLIC register\n");
		printf("\t1. Write ProSLIC register\n");
		printf("\t2. Start ring\n");
		printf("\t3. Stop ring\n");
		printf("\t4. Start SLIC dial-tone\n");
		printf("\t5. Stop SLIC dial-tone\n");
                printf("\t6. Start SW tone\n");
                printf("\t7. SPI stress test\n");
                printf("\t8. Self echo on local phone (CTRL+C to stop)\n");
		printf("\t9. Show driver statistics\n");
		printf("\tx. exit\n");

		gets(str);
		switch(str[0])
		{
			case '0':
				printf("Enter register offset: ");
				gets(str);
				reg = atoi(str);
				printf("Reading register %d: ",reg);
				sleep(1);
				rc = ioctl(fd,PHONE_MV_READ_REG,reg);
				if(rc>=0)
					printf("0x%x\n",rc);
				break;
			case '1':
                                printf("Enter register offset: ");
                                gets(str);
                                reg = atoi(str);
                                printf("Enter value: ");
                                gets(str);
                                val = atoi(str);
                                printf("Writing %d to register %d\n",val,reg);
				reg <<= 16;
				reg |= val;
				sleep(1);
                                rc = ioctl(fd,PHONE_MV_WRITE_REG,reg);
                                break;
			case '2':
				printf("Start ringing...\n");
				sleep(1);
                                rc = ioctl(fd,PHONE_RING_START,0);
				break;
                        case '3':
		                printf("Stop ringing\n");
				sleep(1);
                                rc = ioctl(fd,PHONE_RING_STOP,0);
                                break;
                        case '4':
				printf("Start SLIC dial-tone...\n");
				sleep(1);
                                rc = ioctl(fd,PHONE_DIALTONE,0);
                                break;
			case '5':
				printf("Stop SLIC dial-tone\n");
				sleep(1);
				rc = ioctl(fd,PHONE_CPT_STOP,0);
				break;
                        case '6':
		                printf("Start SW tone...\n");
				rc = sw_tone(fd);
				break;
			case '7':
                                printf("Starting SPI tress test\n");
				sleep(1);
                                rc = ioctl(fd,PHONE_MV_SPI_TEST);
                                break;
			case '8':
                                printf("Entering loop back on local phone...\n");
                                sw_loopback(fd);
				break;
			case '9':
				printf("Driver statistics\n");
				sleep(1);
				rc = ioctl(fd,PHONE_MV_STAT_SHOW);
				break;
                        case 'x':
				exit(0);
			default:
				printf("Invalid command\n");
		}
	}

	if(rc<0) printk("Tool failed to perform action!\n");
	close(fd);
	return 0;
}


/* sin table, 256 points */
static short sinTbl[] = {0,402,804,1205,1606,2005,2404,2801,3196,3590,3981,4370,4756,
5139,5519,5896,6270,6639,7005,7366,7723,8075,8423,8765,9102,9433,9759,10079,10393,
10701,11002,11297,11585,11865,12139,12405,12664,12915,13159,13394,13622,13841,14052,
14255,14449,14634,14810,14977,15136,15285,15425,15556,15678,15790,15892,15985,16068,
16142,16206,16260,16304,16339,16363,16378,16383,16378,16363,16339,16304,16260,16206,
16142,16068,15985,15892,15790,15678,15556,15425,15285,15136,14977,14810,14634,14449,
14255,14052,13841,13622,13394,13159,12915,12664,12405,12139,11865,11585,11297,11002,
10701,10393,10079,9759,9433,9102,8765,8423,8075,7723,7366,7005,6639,6270,5896,5519,
5139,4756,4370,3981,3590,3196,2801,2404,2005,1606,1205,804,402,0,-402,-804,-1205,-1606,
-2005,-2404,-2801,-3196,-3590,-3981,-4370,-4756,-5139,-5519,-5896,-6270,-6639,-7005,
-7366,-7723,-8075,-8423,-8765,-9102,-9433,-9759,-10079,-10393,-10701,-11002,-11297,
-11585,-11865,-12139,-12405,-12664,-12915,-13159,-13394,-13622,-13841,-14052,-14255,
-14449,-14634,-14810,-14977,-15136,-15285,-15425,-15556,-15678,-15790,-15892,-15985,
-16068,-16142,-16206,-16260,-16304,-16339,-16363,-16378,-16383,-16378,-16363,-16339,
-16304,-16260,-16206,-16142,-16068,-15985,-15892,-15790,-15678,-15556,-15425,-15285,
-15136,-14977,-14810,-14634,-14449,-14255,-14052,-13841,-13622,-13394,-13159,-12915,
-12664,-12405,-12139,-11865,-11585,-11297,-11002,-10701,-10393,-10079,-9759,-9433,-9102,
-8765,-8423,-8075,-7723,-7366,-7005,-6639,-6270,-5896,-5519,-5139,-4756,-4370,-3981,
-3590,-3196,-2801,-2404,-2005,-1606,-1205,-804,-402,0};
unsigned short txAudioBufs [80];
static unsigned short f1Mem = 0;
static unsigned short f2Mem = 0;
static short seg_uend[8] = {0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF};
static short search(short val, short *table, short size)
{
	short i;
	for (i = 0; i < size; i++) {
		if (val <= *table++)
			return (i);
	}
	return (size);
}
unsigned char linear2ulaw(short	pcm_val)
{
	short mask, seg;
	unsigned char uval;
	pcm_val = pcm_val >> 2;
	if (pcm_val < 0) {
		pcm_val = -pcm_val;
		mask = 0x7F;
	} else {
		mask = 0xFF;
	}
        if ( pcm_val > 8159 ) pcm_val = 8159;
	pcm_val += (0x84 >> 2);
	seg = search(pcm_val, seg_uend, 8);
	if (seg >= 8)
		return (unsigned char) (0x7F ^ mask);
	else {
		uval = (unsigned char) (seg << 4) | ((pcm_val >> (seg + 1)) & 0xF);
		return (uval ^ mask);
	}
}
short ulaw2linear(unsigned char u_val)
{
	short t;
	u_val = ~u_val;
	t = ((u_val & 0xf) << 3) + 0x84;
	t <<= ((unsigned)u_val & 0x70) >> 4;
	return ((u_val & 0x80) ? (0x84 - t) : (t - 0x84));
}
static void gen_tone(unsigned short f1, unsigned short f2)
{
	short i, j;
	short buf[80];
	register short *ptr;
	ptr = buf;
	for(i=0; i<80; i++) {
		*ptr++ = (sinTbl[f1Mem >> 8] + sinTbl[f2Mem >> 8]) >> 2;
		f1Mem += f1;
		f2Mem += f2;
	}
	memcpy (txAudioBufs, (void *)buf, 80*2);
}

int sw_tone(int fd)
{
	char wr_buff[BUFSIZE];
	char rd_buff[BUFSIZE];
	char str[32];
	unsigned int x,fail_count = 0;
	fd_set wr_fds, rd_fds, ex_fds;

	while(1) {
		printf("Take hook off before continuing.\n");
		printf("Choose frequency: (1) 300HZ (2) 630HZ (3) 1000HZ (4) Back to main menu.\n");
		gets(str);
		if(str[0] == '1') {
			x = 2457;
			printf("Generating 300HZ tone\n");
		}
		else if (str[0] == '2') {
			x = 5161;
			printf("Generating 630HZ tone\n");
		}
		else if (str[0] == '3') {
			x = 8192;
			printf("Generating 1000HZ tone\n");
		}
        	else if (str[0] == '4') {
			return 0;
	        }
		else {
			printf("Input error\n");
			return -1;
		}

		printf("Put hook on to return to menu.\n");

	        /* seems like you must 'read' before 'selcet' (???) */
        	//read(fd,rd_buff,BUFSIZE);

	        while (fail_count<5)
        	{
	                int a, ready_fd, slic_ex;

                	FD_ZERO(&wr_fds);
	                FD_ZERO(&rd_fds);
        	        FD_ZERO(&ex_fds);

			FD_SET(fd,&wr_fds);
                        FD_SET(fd,&rd_fds);
                        FD_SET(fd,&ex_fds);

	                ready_fd = select(fd+1,&rd_fds,&wr_fds,&ex_fds,NULL);
        	        if(ready_fd == -1) {
                       		printf("##select error##\n");
                        	return -1;
	                }
	//		printf("continue...\n");
	//              if(FD_ISSET(fd,&wr_fds)) printf("wr_fds is set\n");
	//              if(FD_ISSET(fd,&rd_fds)) printf("rd_fds is set\n");
	//              if(FD_ISSET(fd,&ex_fds)) printf("ex_fds is set\n");
		
	//		sleep(1);
	                if(FD_ISSET(fd,&ex_fds)) {
        	                int ex;
                	        //printf("ex ioctl...\n");
                        	ex = ioctl(fd,PHONE_EXCEPTION,0);
	                        if(ex >= 0) {
        	                        //printf("##ex = 0x%08x##\n",ex);
                	                if(ex & 2) {
                        	                printf("##hook is off##\n");
						if(ioctl(fd,PHONE_PLAY_START) < 0)
							printf("error PHONE_PLAY_START\n");
						//break;
					}
                                	else {
						printf("##hook is on##\n");
						if(ioctl(fd,PHONE_PLAY_STOP) < 0)
							printf("error PHONE_PLAY_STOP\n");
						break;
					}
                        	}
	                        else {
        	                        printf("##PHONE_EXCEPTION failed##\n");
                	                fail_count++;
                        	}
	                }
        	        if(FD_ISSET(fd,&rd_fds)) {
                	        //printf("reading...\n");
                        	a = read(fd, rd_buff, BUFSIZE);
	                        if (a < 0) {
        	                        printf("##read() failed.##\n");
                	                fail_count++;
                        	}
	                        if(FD_ISSET(fd,&wr_fds)) {
					int i;
					gen_tone(x,x);
					for(i=0;i<80;i++)
				        	wr_buff[i] = linear2ulaw(txAudioBufs[i]);
	                        	//printf("writing...\n");
        	                        a = write(fd, wr_buff, a);
                	                if (a <= 0) {
                        	        	printf("##write() failed.##\n");
                                	        fail_count++;
	                                }
        	                }
                	}
	        }
		if(fail_count == 5) return -1;
	}
	return 0;
}

void sw_loopback(int fd)
{
        char rd_buff[BUFSIZE];
	int fail_count=0;

#ifdef RECORD_DATA
	short ulaw_buff[BUFSIZE];
	char *ulaw_name = "/root/voip_test/ulaw_log.pcm";
        int i, ulaw_fd = open(ulaw_name,O_RDWR);
        if(ulaw_fd <= 0) {
                printf("## Cannot open %s.##\n",ulaw_name);
                exit(2);
        }
#endif

	/* seems like you must 'read' before 'selcet' (???) */
        read(fd,rd_buff,BUFSIZE);

	while (fail_count<5) 
	{
		int a, ready_fd, slic_ex;
		fd_set wr_fds, rd_fds, ex_fds;

		FD_ZERO(&wr_fds);
		FD_ZERO(&rd_fds);
                FD_ZERO(&ex_fds);

                FD_SET(fd,&wr_fds);
		FD_SET(fd,&rd_fds);
		FD_SET(fd,&ex_fds);

		//printf("selecting...\n");
		ready_fd = select(fd+1,&rd_fds,NULL/*&wr_fds*/,&ex_fds,NULL);
		if(ready_fd == -1) {
			printf("##read select error##\n");
			exit(2);
		}

		//if(FD_ISSET(fd,&wr_fds)) printf("wr_fds is set\n");
		//if(FD_ISSET(fd,&rd_fds)) printf("rd_fds is set\n");
		//if(FD_ISSET(fd,&ex_fds)) printf("ex_fds is set\n");

                if(FD_ISSET(fd,&ex_fds)) {
                        int ex;
			//printf("ex ioctl...\n");
                        ex = ioctl(fd,PHONE_EXCEPTION,0);
                        if(ex >= 0) {
                                if(ex & 2) {
                                        printf("##hook is off##\n");
					ioctl(fd,PHONE_REC_START);
				}
                                else {
                                        printf("##hook is on##\n");
					ioctl(fd,PHONE_REC_STOP);
				}
                        }
                        else {
                                printf("##PHONE_EXCEPTION failed##\n");
                                fail_count++;
                        }
                }

		if(FD_ISSET(fd,&rd_fds)) {
			//printf("reading...\n");
			a = read(fd, rd_buff, BUFSIZE);
			if (a >= 0) {
#ifdef RECORD_DATA
				/* linear read data and store to log file */
				for(i=0; i<a; i++) {
					ulaw_buff[i] = ulaw2linear(rd_buff[i]);
				}
				write(ulaw_fd,ulaw_buff,i*2);
#endif
				if(FD_ISSET(fd,&wr_fds)) {
					//printf("writing...\n");
					a = write(fd, rd_buff, a);
        		                if (a <= 0) {
                	        		printf("##write() failed.##\n");
                        	        	fail_count++;
	                        	}
				}
			}
	                else {
        	        	printf("##read() failed.##\n");
                	        fail_count++;
                        }
		}	
	}

_exit:
#ifdef RECORD_DATA
	close(ulaw_fd);
#endif
	return;
}

void hw_loopback(int fd)
{
        char *wr_buff;/*[BUFSIZE];*/
        char rd_buff[BUFSIZE];
        int i, fail_count=0;
        char *p;

        /* init once the wr_buff */
	wr_buff = "123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789";
//        for(i=0,p=wr_buff; i<BUFSIZE; i++,p++)
//                *p = i;

        /* seems like you must 'read' before 'selcet' (???) */
        //read(fd,rd_buff,BUFSIZE);

        while (fail_count<5)
        {
                int a, ready_fd, slic_ex;
                fd_set wr_fds, rd_fds, ex_fds;

                FD_ZERO(&wr_fds);
                FD_ZERO(&rd_fds);
                FD_ZERO(&ex_fds);

                FD_SET(fd,&wr_fds);
                FD_SET(fd,&rd_fds);
                FD_SET(fd,&ex_fds);

		printf("##selecting...\n");
                ready_fd = select(fd+1,&rd_fds,&wr_fds,&ex_fds,NULL);
                if(ready_fd == -1) {
                        printf("##select error\n");
                        exit(2);
                }
		if(FD_ISSET(fd,&wr_fds)) printf("wr_fds is set\n");
		if(FD_ISSET(fd,&rd_fds)) printf("rd_fds is set\n");
		if(FD_ISSET(fd,&ex_fds)) printf("ex_fds is set\n");

                if(FD_ISSET(fd,&wr_fds) || FD_ISSET(fd,&rd_fds)) {
			/* debug */
			if(!(FD_ISSET(fd,&wr_fds) && FD_ISSET(fd,&rd_fds))) {
				printf("##read and write fds are not set together\n");
			}
			printf("##reading...\n");
                        a = read(fd, rd_buff, BUFSIZE);
                        if (a < 0) {
	                        printf("##read() failed.\n");
                                fail_count++;
                        }
                        else {
				printf("***\n");
				puts(rd_buff);
				printf("***\n");
			}
			printf("##writing...\n");
	               	a = write(fd, wr_buff, BUFSIZE);
        	        if (a < 0) {
        	              	printf("##write() failed.\n");
                	        fail_count++;
	                }
		}
                if(FD_ISSET(fd,&ex_fds)) {
                        int ex;
                        printf("##exception event\n");
                        ex = ioctl(fd,PHONE_EXCEPTION,0);
                        if(ex >= 0) {
                                if(ex & 2)
                                        printf("##hook is off\n");
                                else
                                        printf("##hook is on\n");
                        }
                        else {
                                printf("##PHONE_EXCEPTION failed\n");
                                fail_count++;
                        }
                }
        }
	exit(2);
}


