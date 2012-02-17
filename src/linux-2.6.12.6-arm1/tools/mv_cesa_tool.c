#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#include "cesa_dev.h"



void show_usage(int badarg)
{
        fprintf(stderr,
                "Usage:                                                                                 \n"
                " mv_cesa_tool -h                                                                       \n"
                "   Display this help.                                                                  \n"
                "                                                                                       \n"
		" mv_cesa_tool -test  <test_mode> <iter> <req_size> <checkmode>				\n"
		"	(can be used only in Test Mode)							\n"
		"	test_mode: Multi, Size AES, DES, 3DES MD5 SHA1     				\n"
		"	iter	 : number of iteration to run						\n"
		"	req_size : size of the buffer to be encrypt/decrypt/HASING			\n"
		"	checkmode: 0 - Fast verify 							\n"
		"		   1 - Full verify						        \n"
		"		   2 - without verify (for performence)					\n"
		"											\n"
		" mv_cesa_tool -test -s <iter> <req_size> <session_id> <data_id>			\n"
		"       (can be used only in Test Mode)                                                 \n"
		"	+++ for debug usage only +++							\n"
		"	req_size  : size of the buffer to be encrypt/decrypt/HASING                     \n"
		"	iter      : number of iteration to run 						\n"
		"	session_id: which session to open out of the cesa_test session see in kernel.	\n"
		"	data_id   : which data pattern to use out of the cesa_test session see in kernel\n"
		"       checkmode : 0 - Fast verify                                                     \n"
                "                   1 - Full verify                                                     \n"
                "                   2 - without verify (for performence)                                \n"
		"											\n"
		" mv_cesa_tool -debug <debug_mode> [-i index] [-v] [-s size]				\n"
		"	(Tst_req and Tst_ses can be used only in Test Mode)				\n"
		"	debug_mode: Sts - display general status 					\n"
		"		    Chan - display channel index status 				\n"
		"		    Q - display SW Q 							\n"
		"		    SA - display SA index 						\n"
		"		    SAD - display entire SAD						\n"
		"		    Cache_idx - display cache index 					\n"
		"		    Sram - display SRAM contents					\n"
		"		    Tst_req - display request index with buffer of size size		\n"
		"		    Tst_ses - display session index					\n"
		"	-i index  : index (only relevant for: Chan, SA, Cache_idx, Tst_req and Tst_ses  \n"
		"	-v        : verbose mode							\n"
		"	-s size	  : buffer size (only relevant for Tst_req)				\n\n"
	);

	exit(badarg);
}

static void get_index(int argc, char **argv, CESA_DEBUG *cesa_debug)
{
	int j;
	for(j = 3; j < argc; j++) {
      		if(!strcmp(argv[j], "-i")) {
           		j++;
       			break;
		}
       	}
  	if(!(j < argc)) {
		fprintf(stderr,"missing/ illegal index. \n");
		exit(1);
	}
     	cesa_debug->index = atoi(argv[j]);
}

static void parse_debug_cmdline(int argc, char **argv, CESA_DEBUG *cesa_debug)
{
	unsigned int i = 2,j;

	for(j = i; j < argc; j++) {
		if(!strcmp(argv[j], "-v"))
			cesa_debug->mode = 1; 
	}

	if(argc < 3) {
                fprintf(stderr,"missing arguments\n");
		exit(1);
	}

	if(!strcmp(argv[i], "Sts")) 
        	cesa_debug->debug = STATUS;
	else if(!strcmp(argv[i], "Chan")) {
                cesa_debug->debug = CHAN;
		get_index(argc, argv, cesa_debug);
	}
        else if(!strcmp(argv[i], "Q"))
                cesa_debug->debug = QUEUE;
        else if(!strcmp(argv[i], "SA")) {
                cesa_debug->debug = SA;
		get_index(argc, argv, cesa_debug);
	}
        else if(!strcmp(argv[i], "Cache_idx")) {
                cesa_debug->debug = CACHE_IDX;
		get_index(argc, argv, cesa_debug);
	}
        else if(!strcmp(argv[i], "Sram"))
                cesa_debug->debug = SRAM;
        else if(!strcmp(argv[i], "SAD"))
                cesa_debug->debug = SAD;
        else if(!strcmp(argv[i], "Tst_req")) {
                cesa_debug->debug = TST_REQ;
        	for(j = i; j < argc; j++) {
                	if(!strcmp(argv[j], "-s")) {
				j++;
                        	break;
			}
		}
		if(!(j < argc)) {
			fprintf(stderr,"missing/illegal size\n");
			exit(1);
		}
		cesa_debug->size = atoi(argv[j]);
		get_index(argc, argv, cesa_debug);
	}
        else if(!strcmp(argv[i], "Tst_req")) {
                cesa_debug->debug = TST_SES;
		get_index(argc, argv, cesa_debug);
	}
	else{
		fprintf(stderr,"illegal debug option\n");
		exit(1);
	}

}

static void parse_test_cmdline(int argc, char **argv, CESA_TEST *cesa_test)
{
        unsigned int i = 2;

	if(argc < 6) {
        	fprintf(stderr,"missing arguments\n");
		exit(1);
	}

	if(!strcmp(argv[i], "-s")) { /* single test */
		i++;
		if(argc != 8)
                	show_usage(1);
		cesa_test->test = SINGLE;
		cesa_test->iter = atoi(argv[i++]);
		cesa_test->req_size = atoi(argv[i++]);
		cesa_test->session_id = atoi(argv[i++]);
		cesa_test->data_id = atoi(argv[i++]);
                cesa_test->checkmode = atoi(argv[i++]);
	}
        else {
		if(argc != 6)
                        show_usage(1);
		if (!strcmp(argv[i], "Multi"))
                	cesa_test->test = MULTI;
                else if (!strcmp(argv[i], "Size"))
                        cesa_test->test = SIZE;
		else if(!strcmp(argv[i], "AES"))
                        cesa_test->test = AES;
                else if(!strcmp(argv[i], "DES"))
                        cesa_test->test = DES;
                else if(!strcmp(argv[i], "3DES"))
                        cesa_test->test = TRI_DES;
                else if(!strcmp(argv[i], "MD5"))
                        cesa_test->test = MD5;
                else if(!strcmp(argv[i], "SHA1"))
                        cesa_test->test = SHA1;
		else {
			fprintf(stderr,"illegal test option\n");	
			exit(1);
		}
		i++;	
		cesa_test->iter = atoi(argv[i++]);
		cesa_test->req_size = atoi(argv[i++]);
		cesa_test->checkmode = atoi(argv[i++]);
        }
}



int main(int argc, char *argv[])
{
        char *name = "/dev/cesa";
        int fd, t, i, fdflags;
        int rc = 0;
	CESA_TEST	cesa_test;
	CESA_DEBUG	cesa_debug;

	memset(&cesa_test, 0, sizeof(CESA_TEST));
	memset(&cesa_debug, 0, sizeof(CESA_DEBUG));

        /* open the device */
        fd = open(name, O_RDWR);
        if (fd <= 0) {
                printf("## Cannot open %s device.##\n",name);
                exit(2);
        }

        /* set some flags */
        fdflags = fcntl(fd,F_GETFL,0);
        fdflags |= O_NONBLOCK;
        fcntl(fd,F_SETFL,fdflags);

        if(argc < 2) {
                fprintf(stderr,"missing arguments\n");
		exit(1);
        }

	i = 1;

        if (!strcmp(argv[i], "-h")) {
                show_usage(0);
        }

        else if (!strcmp(argv[i], "-test")) { /* test */
		parse_test_cmdline(argc, argv, &cesa_test);
		printf("test %d iter %d req_size %d checkmode %d sess_id %d data_id %d \n", 
			cesa_test.test, cesa_test.iter, cesa_test.req_size, cesa_test.checkmode,
			cesa_test.session_id, cesa_test.data_id );

        	rc = ioctl(fd, CIOCTEST, &cesa_test);
	}
	else { /* debug */
                parse_debug_cmdline(argc, argv, &cesa_debug);
		printf("debug %d index %d mode %d size %d \n",
			cesa_debug.debug, cesa_debug.index, cesa_debug.mode, cesa_debug.size);
                rc = ioctl(fd, CIOCDEBUG, &cesa_debug);
	}
	if(rc < 0) printf("Cesa Tool failed to perform action!\n");

	close(fd);

	return 0;
}
