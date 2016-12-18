#ifndef RVM_INTERNAL
#define RVM_INTERNAL

#define DEBUG

#include <map>
#include <set>
#include <list>
#include <string>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <fstream>

#define MAX_BUFF 100000

using namespace std;

typedef int32_t rvm_t;
typedef struct rvm_struct rvm_struct;
typedef struct trans_unit trans_unit_t;
typedef struct trans_node trans_node_t;

/*************************************************************
****************Segment Description***************************
*************************************************************/
typedef struct seg_struct {
	string name;			//segment name, the same as for name.redo and neme.undo
	void *memory;			//memory pointer to the segment 	??? maybe we dont need this one?
	int size;				//size of the segmant
	bool is_used;			//is not committed by transaction
	bool is_mapped;			//whether it is mapped or not (may not be mapped, but not destroyed yet)
} seg_t;

//global segment database
typedef map<string, seg_t *> segDB;					//<name, segment>
typedef pair<string, seg_t *> segDB_en;

//segment names
typedef set<string> segset;							//<name>

//map to connect memory pointers with segment names
typedef map<const void *, string> segnmap;			//<mem_ptr, name>
typedef pair<const void *, string> segnmap_en;

/*************************************************************
****************Transaction Description***********************
*************************************************************/
typedef int32_t trans_t;

typedef list<trans_unit_t*> trans_queue;
typedef set<void *> seg_base_set;

//transaction node, an entity operated by rvm_about_to_modify()
struct trans_node {
	seg_base_set *seg_bases;						// map between segments and corresponding transactions
	trans_queue *transactions;						// Queue of transactions
	rvm_t rvm_id;
};


struct trans_unit {
	void *segbase;
	void *undo_log;
	int offset;
	int size;
};

//queue of all transactions which are not committed
typedef list<trans_t> trans_list;

//global transaction DB
typedef map<const trans_t, trans_node_t *> transDB;			//<transaction id, trans_node_t*>
typedef pair<const trans_t, trans_node_t *> transDB_en;

/*************************************************************
****************RVM Structure Description*********************
*************************************************************/

//global list of all RVM processes
typedef map<const rvm_t, const rvm_struct *> rvmDB;					//<transaction id, set of transaction units>
typedef pair<const rvm_t, const rvm_struct *> rvmDB_en;

struct rvm_struct {
	//directory where to store log files
	string dirname;
	
	//all the memory segments used by rvm
	segset *seg_db;

	//mem_ptr to name convertion for memory segments
	segnmap *seg_name_map;

	//transaction queue
	trans_list *trans;
};

extern rvmDB global_rvmDB;
extern rvm_t global_rvm_cntr;
extern segDB global_segDB;
extern transDB global_transDB;
extern trans_t global_trans_cntr;

#endif