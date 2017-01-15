---
1. Data Structures Description
---

	rvm_t - pointer to structure that describes RVM process. The structure includes following fields:
		string dirname 				- directory name where to store all the log files corresponding to the process 
		segset seg_db 				- <name> set that collects all the memory segments' names used by rvm. Segments are brought here by rvm_map() function. rvm_unmap() just set is_mapped flag to false. rvm_destroy() delete the segment from the data structure, as well as file system, preliminarily truncating it
		segnmap seg_name_map 		- <mem_ptr, name> mem_ptr to name convertion for memory segments to eliminate linear search
		trans_list trans 			- <trans_t> transaction set with IDs of all transactions which are not truncated. It's populated by rvm_begin_trans() function

	struct trans_node - transaction node which describes transaction that created by rvm_begin_trans()
		seg_base_set *seg_bases 	- a set of segment bases, that are about to be modified
		trans_queue *transactions 	- a queue of transaction units that constitute transaction
		rvm_t rvm_id 				- ID of RVM process that initiated transaction

	trans_unit_t - transaction unit, data structure to describe each modification of each memory segment changed by transaction . The structure includes following fields:
		void *segbase 				- mem_ptr to the segment to be modified
		void *undo_log				- mem_ptr to the undo log for the segment to be modified (should be initialized as NULL)
		uint32_t offset 			- offset from the base for the transaction
		uint32_t size 				- size of the transaction from the base (should be within the segment)

	seg_t - structure that describes memory segment which mapped to one or more RVM process. The structure includes following fields:
		string name 				- segment name, the same as for name.log
		void *memory 				- memory pointer to the segment
		uint32_t size 				- size of the segment
		bool is_used 				- is not committed by transaction
		bool is_mapped 				- whether it is mapped or not (may be mapped by only one RVM process)

- rvm_map global_rvm_map - global set of all the RVM processes run in the environment;
- rvm_t global_rvm_cntr - global counter for RVM processes' IDs
- segDB global_segDB - global database of all memory segments used by any of RVM processes. Each segmend can be mapped by zero or more RVM processes;
- transDB global_transDB - global database of transaction node correspondent to transaction ID
- uint32_t global_trans_cntr - global counter for trans_t transaction ID

---
2. Functions Description
---

- rvm_t rvm_init(const char *directory) 
	- returns an ID rvm_t of a new rvm structure describing the rvm for this process and adds this structure to the global_rvm_map. It sets rvm_t->dirname field with directory name string. If directory exists, does nothing.

- void *rvm_map(rvm_t rvm, const char *segname, int size_to_create) 
	- maps the memory region to the RVM process. It returns memory address for the segment. It puts this memory address into rvm->seg_db and rvm->seg_name_map. It also puts memory address into global_segDB. Here are the cases for segment allocation:
	- * If the segment is not exist yet, but there is a .log or .redo file, it tries to truncate redo log if it's necessary, than read file to the memory and return the pointer to it
	- * If segment with presented name has already existed, it maps RVM process to this existing segment, file should be existed. It should update seg_t->users list with rvm process pointer 
	- * If the segment exists, but its size less than requested, the memory should be allocated with updating this segment in all coresponding structures, such global_segDB, rvm->seg_name_map for all RVM processes. Also transaction log is truncated in order to flush trans_unit_t structure that hold this memory pointer.
	- * If segment does not exist, seg_t structure with corresponding name and size is created, and function returns pointer to it. Also an empty file name.log is created

- void rvm_unmap(rvm_t rvm, void *segbase) 
	- unmaps the segment from rvm process. It should find segname in rvm->seg_name_map and than evict segment from rvm->seg_db, rvm->seg_name_map, and evict rvm pointer from global_segDB[segname].users. It sets global_segDB[segname].is_mapped to false. It should perform log truncation if necessary

- void rvm_destroy(rvm_t rvm, const char *segname) 
	- deletes file for segment and all reference to it from all corresponding datastructures. 

- trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases) 
	- initialize transuction for rvm process creating trunsaction node. It returns transaction ID number trans_t from global_trans_cntr. It also adds transaction to global_transDB, and adds all the memory segments from void **segbases array to trans_node->segbases. When rvm_begin_trans() is called, it doesn't have any information about future transaction parts, so no transaction units  are initialized.

- void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size) 
	- sets seg_t->is_used flag to true and creates an undo log in memory where it copies the part of segment. While seg_t->is_used flag is setted, nobody can modify this segment exacept this transaction. It also creates trans_unit_t transaction unit data structure and insert it into trans_node->transactions

- void rvm_abort_trans(trans_t tid) 
	- restores all the segments for transaction ID from global_transDB[trans_t]->transactions

- void rvm_commit_trans(trans_t tid) 
	- commits all the segments of transaction to segname.redo

- void rvm_truncate_log(rvm_t rvm) 
	- perform log trancation for all the logs of rvm from files segname.redo to file segname.log. segname.redo is deleted after truncation.

LOG Format

+---------+-----------------------------------------------------------------------

| Header  |  Payload                                                          ...

+---------+---------+-------------+--------+---------+-------------+--------+-----

| Size of | Offset  | Size of     | Data   | Offset  | Size of     | Data   | ...

| segment |         | transaction |        |         | transaction |        | ...

+---------+---------+-------------+--------+---------+-------------+--------+-----

| 32 bits | 32 bits |   32 bits   | N bits | 32 bits |   32 bits   | N bits | ...

+---------+---------+-------------+--------+---------+-------------+--------+-----
