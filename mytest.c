/* basic.c - test that basic persistency works */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define TEST_STRING "hello, world"
#define OFFSET1 0
#define OFFSET2 100


/* proc1 writes some data, commits it, then exits */

int proc1();
void proc2();
void numbers();

int main(int argc, char **argv)
{
     int pid;


	numbers();
	
	
/*
     pid = fork();
     if(pid < 0) {
	  perror("fork");
	  exit(2);
     }
     if(pid == 0) {
	  //proc1();
	exit(0);
     }
*/

     //waitpid(pid, NULL, 0);

	//proc1();

     //proc2();

     return 0;
}

int proc1() {
     rvm_t rvm;
     trans_t trans;
     char* segs[1];
     
     rvm = rvm_init("rvm_segments");
     rvm_destroy(rvm, "testseg1");
     segs[0] = (char *) rvm_map(rvm, "testseg1", 10000);
	segs[1] = (char *) rvm_map(rvm, "testseg2", 10000);

     
     trans = rvm_begin_trans(rvm, 1, (void **) segs);
     
	rvm_about_to_modify(trans, segs[0], OFFSET1, 100);
	rvm_about_to_modify(trans, segs[1], OFFSET2, 100);

	sprintf(segs[0] + OFFSET1, "first");
	sprintf(segs[1] + OFFSET2, "second");
     
	rvm_commit_trans(trans);

	rvm_truncate_log(rvm);

	return 0;

}


/* proc2 opens the segments and reads from them */
void proc2() {

     char* segs[1];
     rvm_t rvm;
     
     rvm = rvm_init("rvm_segments");

     segs[0] = (char *) rvm_map(rvm, "testseg", 10000);

	printf("segs : %s\n", segs[0]);
     if(strcmp(segs[0], TEST_STRING)) {
	  printf("ERROR: first hello not present\n");
	  exit(2);
     }
     if(strcmp(segs[0]+OFFSET2, TEST_STRING)) {
	  printf("ERROR: second hello not present\n");
	  exit(2);
     }

     printf("OK\n");

}

void numbers() {


     rvm_t rvm;
     trans_t trans;
     char* arr[0];
     
     rvm = rvm_init("rvm_segments");
     rvm_destroy(rvm, "testseg1");
     arr[0] = (char *) rvm_map(rvm, "testseg1", 10000);

int i = 0;


     
     trans = rvm_begin_trans(rvm, 1, (void **) arr);
     
	rvm_about_to_modify(trans, arr[0], 0, 100);
for(i = 0; i < 5; i++) {
	arr[0][i] = 0;
}
for(i = 0; i < 5; i++) {
	arr[0][i]= '1';
}

	rvm_about_to_modify(trans, arr[0], 1, 100);
for(i = 0; i < 3; i++) {
	arr[0][1+i] = '2';
}
	rvm_about_to_modify(trans, arr[0], 2, 100);
for(i = 0; i < 1; i++) {
	arr[0][2+i] = '3';
}
     
	rvm_commit_trans(trans);

	rvm_truncate_log(rvm);


}
