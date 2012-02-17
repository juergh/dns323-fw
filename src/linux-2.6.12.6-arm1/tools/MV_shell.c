#include <stdio.h>

#define DEBUG

char str[256];


void show_help(void) {
  printf ("\n-----------\n");
  printf ("rr reg       - register read, used to read a GT register\n");
  printf ("rw reg value - register write, used to write a value to a GT register\n");
  printf ("md offset    - memory dump, used to dump 128 bytes in memory\n");
  printf ("mw reg value - memory write, used to write a value to a physical memory\n");
  printf ("-----------\n");
}

void gt_reg_read(unsigned int rgst) {
  unsigned int reg = rgst, value;
  char ch;
  FILE *resource_dump,*output;

  output = fopen ("./xxx.out","w");
  if (!output) { printf ("Canot open file\n");return;}

  while (1){ 
    resource_dump = fopen ("/proc/resource_dump" , "w");
    if (!fopen) {
      printf ("Eror opening file /proc/resource_dump\n");
      exit(-1);
    }
    fprintf (resource_dump,"register  r %08x",reg);
    fclose (resource_dump);
    resource_dump = fopen ("/proc/resource_dump" , "r");
    if (!fopen) {
      printf ("Eror opening file /proc/resource_dump\n");
      exit(-1);
    }
    fscanf (resource_dump , "%x" , &value);
    fclose (resource_dump);
    printf ("%08x : %08x ",reg,value);

    if (!gets (str)) {
     printf ("Error in reading line from stdin\n");
      exit (-1);
    }
    if (str[0] == '.') break;
    reg += 4;
  
  }
}

void gt_mem_dump(unsigned int rgst) {
  unsigned int offset = rgst, value , i , j;
  char ch;
  FILE *resource_dump;
  FILE *output;
  output = fopen ("./xxx.out","w");
  if (!output) { printf ("Canot open file\n");return;}
  i = 0;
  while (1) {
    if (i == 0) printf ("\n");
    printf ("%08x : ",offset);
    for (j = 0 ; j < 8 ; j++) {
      resource_dump = fopen ("/proc/resource_dump" , "w");
      if (!fopen) {
	printf ("Eror opening file /proc/resource_dump\n");
	exit(-1);
      }

      fprintf (resource_dump,"memory    r %08x",offset);
      fclose (resource_dump);
      resource_dump = fopen ("/proc/resource_dump" , "r");
      if (!fopen) {
	printf ("Eror opening file /proc/resource_dump\n");
	exit(-1);
      }
      fscanf (resource_dump , "%x" , &value);
      fclose (resource_dump);
            printf ("%08x ",value);
      
      {
	unsigned int temp;
	temp = ((value & 0xff) << 24) |
	  ((value & 0xff00) << 8) |
	  ((value & 0xff0000) >> 8) |
	  ((value & 0xff000000) >> 24);
	value = temp;
      }
      fprintf (output, "%c",value & 0xff);
      value = value >> 8;
      fprintf (output, "%c",value & 0xff);
      value = value >> 8;
      fprintf (output, "%c",value & 0xff);
      value = value >> 8;
      fprintf (output, "%c",value & 0xff);
      value = value >> 8;

      offset += 4;
      }
      if (!gets (str)) {
	printf ("Error in reading line from stdin\n");
	exit (-1);
      }
      if (str[0] == '.') break;
      i ++;

  }
  fclose (output);
  printf ("\n");
}

void gt_mem_write(unsigned int rgst, unsigned int vlue) {
  unsigned int reg = rgst, value = vlue;
  unsigned int element;
  char ch;
  FILE *resource_dump;
  resource_dump = fopen ("/proc/resource_dump" , "w");
  if (!fopen) {
    printf ("Eror opening file /proc/resource_dump\n");
    exit(-1);
  }
  fprintf (resource_dump,"memory    w %08x %08x",reg,value);
  fclose (resource_dump);
}

void gt_reg_write(unsigned int rgst, unsigned int vlue) {
  unsigned int reg = rgst, value = vlue;
  unsigned int element;
  char ch;
  FILE *resource_dump;
  resource_dump = fopen ("/proc/resource_dump" , "w");
  if (!fopen) {
    printf ("Eror opening file /proc/resource_dump\n");
    exit(-1);
  }
  fprintf (resource_dump,"register  w %08x %08x",reg,value);
  fclose (resource_dump);
}

int main(void) {
  unsigned int reg , value , offset , element;
  char inst[256];
  char value_s[10];
  printf ("\n\n\n*******\nWelcome to the GT Shell environment\n\n");
  printf ("Write 'help' for getting help on the instructions\n");
  while (1) {
    memset (str , 0 , 256);
    memset (inst , 0 , 256);
    printf ("MV Shell -> ");
    if (!gets (str)) {
      printf ("Error in reading line from stdin\n");
      exit (-1);
    }
    element = sscanf (str , "%s" , inst);
    if (element == 0) continue;
    offset = strlen (inst);
    if (!strcmp (inst , "quit")) break;
    
    if (!strcmp (inst , "rr")) {
      element = sscanf (str+offset , "%x" , &reg);
      if (element == 1) gt_reg_read (reg);
      else printf ("Insufficient parameters\n");
    }
    else if (!strcmp (inst , "rw")) {
      element = sscanf (str+offset , "%x %x" , &reg , &value);
      if (element == 2) gt_reg_write (reg,value);
      else printf ("Insufficient parameters\n");
    }
    else if (!strcmp (inst , "help")) {
      show_help();
    }
    else if (!strcmp (inst , "md")) {
      element = sscanf (str+offset , "%x" , &reg);
      if (element == 1) gt_mem_dump (reg);
      else printf ("Insufficient parameters\n");
    }
    else if (!strcmp (inst , "mw")) {
      element = sscanf (str+offset , "%x %x" , &reg , &value);
      if (element == 2) gt_mem_write (reg,value);
      else printf ("Insufficient parameters\n");
    }

    else if (strlen (str) != 0) printf ("Invalid command - %s\n",inst);
  }
  printf ("Good Bye\n");
  
}
