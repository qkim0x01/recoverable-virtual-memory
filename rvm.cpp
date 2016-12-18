#include "rvm.h"

rvmDB global_rvmDB;
rvm_t global_rvm_cntr = 0;

segDB global_segDB;

transDB global_transDB;
trans_t global_trans_cntr = 0;

void redo_to_log(string found_segname, string rvm_name);

rvm_t rvm_init(const char *directory) {
	struct stat st;
	if ( stat(directory, &st) == -1) {
		if (mkdir(directory, 0777) == -1){
			fprintf(stderr, "ERROR : rvm_init | can't create directory!\n");
			return -1;
		}
	}
	rvm_t rvm_id;

	rvm_id = global_rvm_cntr++;
	rvm_struct *new_rvm = new rvm_struct;

	new_rvm->dirname = string(directory);
	new_rvm->seg_db = new segset;
	new_rvm->seg_name_map = new segnmap;
	new_rvm->trans = new trans_list;

	global_rvmDB.insert(rvmDB_en(rvm_id, new_rvm));	

	#ifdef DEBUG
	//////////////PRINTF!!!//////////////////////////////////
	printf("rvm_init| RVM process %d added\n", rvm_id);
	rvmDB::iterator find_rvm;
	for (find_rvm = global_rvmDB.begin(); find_rvm != global_rvmDB.end(); ++find_rvm){
		//printf("rvm_id - %d\n", find_rvm->first);
	}
	segDB::iterator found_seg_global;
	for (found_seg_global = global_segDB.begin(); found_seg_global != global_segDB.end(); ++found_seg_global){
		///printf("segment name - %s\n", found_seg_global->first.c_str());
	}
	/////////////////////////////////////////////////////////
	#endif

	return rvm_id;
}


/*
  map a segment from disk into memory. 
  If the segment does not already exist, then create it and give it size size_to_create. 
  If the segment exists but is shorter than size_to_create, then extend it until it is long enough. 
  It is an error to try to map the same segment twice.
*/
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create) {

	rvmDB::iterator found_rvm = global_rvmDB.find(rvm);
	if (found_rvm == global_rvmDB.end()) {
		fprintf(stderr, "ERROR | rvm_Map : No corresponding rvm found!\n");
		exit(EXIT_FAILURE);
	}

	string rvm_name = found_rvm->second->dirname;

	segDB::iterator found_seg_global = global_segDB.find(string(segname));
	segset::iterator found_seg = global_rvmDB[rvm]->seg_db->find(string(segname));
	segnmap::iterator found_seg_ptr;


	char *buff = new char[MAX_BUFF];
	int filesize = 0;

	struct stat st;
	int fd = 0;

	bool log_exist = false;
	bool redo_exist = false;
	// Check if .redo or .log file exists
	if ( stat((rvm_name + "/" + string(segname) + ".redo").c_str(), &st) == 0){
		redo_exist = true;
	}

	if (redo_exist) {
		#ifdef DEBUG
		printf("rvm_map | truncating redo log\n");
		#endif

		rvm_truncate_log(rvm);
		redo_to_log(string(segname), rvm_name);
		log_exist = true;
	}

	if ( stat((rvm_name + "/" + string(segname) + ".log").c_str(), &st) == 0){
		log_exist = true;
	}

	if (log_exist) {
		// Open redo log
		fd = open((rvm_name + "/" + string(segname) + ".log").c_str(), O_RDWR);
		if (fd < 0){
			fprintf(stderr, "ERROR : rvm_turncate | can not open redo log for the segment!\n");
			exit (EXIT_FAILURE);
		}

		filesize = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);

		read(fd, buff, filesize);

		#ifdef DEBUG
		printf("rvm_map | log filesize : %d\n", filesize);
		#endif

		seg_t *restored_seg = new seg_t;
		restored_seg->memory = buff;
		restored_seg->size = filesize;
		restored_seg->is_used = false;
		restored_seg->is_mapped = false;

		// Add restored segment to segDB
		global_segDB.insert(segDB_en(segname, restored_seg));
	}

	found_seg_global = global_segDB.find(string(segname));

	bool seg_exist = found_seg_global != global_segDB.end();
	bool seg_in_rvm = found_seg != global_rvmDB[rvm]->seg_db->end();
	
	#ifdef DEBUG
	printf("rvm_map | segment is %s found and %s mapped\n", seg_exist ? "" : "not", seg_in_rvm ? "" : "not");
	#endif
	
	// segment not found
	if (!seg_in_rvm && !seg_exist) {
		seg_t *new_seg = new seg_t;
		void *mem_ptr = (void *)malloc(size_to_create);
/*
		if (log_exist) {
			memcpy(mem_ptr, buff, filesize);
			#ifdef DEBUG
			printf("rvm_map | copying data from log : %s\n", buff);
			printf("rvm_map | mapping : %p\n", mem_ptr);
			#endif
		}
*/
		found_seg_ptr = global_rvmDB[rvm]->seg_name_map->find(mem_ptr);
		if(found_seg_ptr != global_rvmDB[rvm]->seg_name_map->end()){
			fprintf(stderr, "ERROR : rvm_map | malloc returns memory segment which is marked as mapped!\n");
			exit (EXIT_FAILURE);
		}

		// Fill out new segment information
		new_seg->memory = mem_ptr;
		new_seg->size = size_to_create;
		new_seg->is_used = false;
		new_seg->is_mapped = true;

		// Add segment information to the rvmDB
		global_rvmDB[rvm]->seg_db->insert(string(segname));
		global_rvmDB[rvm]->seg_name_map->insert(segnmap_en(mem_ptr, segname));

		// Add segment information to segDB
		global_segDB.insert(segDB_en(segname, new_seg));

		int fd = 0;
		if (!log_exist) {
			fd = creat((rvm_name + "/" + segname + ".log").c_str(), 0777);
		}
		if (fd < 0){
			fprintf(stderr, "ERROR : rvm_map | can not create persistent file for the segment!\n");
			exit (EXIT_FAILURE);
		}
    	ftruncate(fd, size_to_create);
    	close(fd);
    	return mem_ptr;
	}
	//segment mapped to the rvm process
	else if ((seg_exist && seg_in_rvm) || 
			(seg_exist && !found_seg_global->second->is_used && !found_seg_global->second->is_mapped)){

		//truncate redo log before re-mapping the segment
		rvm_truncate_log(rvm);

		if (found_seg_global->second->is_used){
			fprintf(stderr, "ERROR : rvm_map | trying to relallocate segment while in use!!!\n");
			exit (EXIT_FAILURE);
		}

		void *mem_ptr;

		if (found_seg_global->second->size < size_to_create){
			mem_ptr = realloc(found_seg_global->second->memory, size_to_create);
/*
			if (log_exist) {
printf("rvm_map | copying data from log : %s\n", buff);
printf("rvm_map | mapping : %p\n", mem_ptr);
				memcpy(mem_ptr, buff, filesize);
			}
*/
			found_seg_ptr = global_rvmDB[rvm]->seg_name_map->find(found_seg_global->second->memory);
			if(found_seg_ptr != global_rvmDB[rvm]->seg_name_map->end()){
				global_rvmDB[rvm]->seg_name_map->erase(found_seg_ptr);
			}

			found_seg_global->second->memory = mem_ptr;
			found_seg_global->second->size = size_to_create;
			found_seg_global->second->is_used = false;
			found_seg_global->second->is_mapped = true;
			global_rvmDB[rvm]->seg_name_map->insert(segnmap_en(mem_ptr, segname));
		}
		else {mem_ptr = found_seg_global->second->memory;}

		if (!seg_in_rvm){
			// Add segment information to the rvmDB
			global_rvmDB[rvm]->seg_db->insert(string(segname));
			global_rvmDB[rvm]->seg_name_map->insert(segnmap_en(mem_ptr, segname));
		}
		return mem_ptr;
	}
	//segment not in global DB
	else if (seg_in_rvm){
		fprintf(stderr, "ERROR : rvm_map | segment is found in the process DB, but not in global DB!!!\n");
		exit (EXIT_FAILURE);
	}
	//segment is mapped to another rvm process
	else {
		fprintf(stderr, "ERROR : rvm_map | segment is used by another RVM process!!!\n");
		exit (EXIT_FAILURE);	
	}

	return 0;
}


/*
  unmap a segment from memory.
*/
void rvm_unmap(rvm_t rvm, void *segbase){
	//free memory pointed by segbase
	//check if it allocated for the rvm
	segset::iterator found_seg;
	segnmap::iterator found_seg_ptr;
	string segname;

	//truncate log
	rvm_truncate_log(rvm);

	// Earase the segment from the rvm segment list
	found_seg_ptr = global_rvmDB[rvm]->seg_name_map->find(segbase);
	if(found_seg_ptr != global_rvmDB[rvm]->seg_name_map->end()){
		segname = found_seg_ptr->second;
	}
	else{
		#ifdef DEBUG
		fprintf(stderr, "WARNINNG : rvm_unmap | trying to unmap segment which is not mapped to the process!!!\n");
		return;
		//exit (EXIT_FAILURE);
		#endif
	}

	segDB::iterator found_seg_global = global_segDB.find(string(segname));
	if (found_seg_global != global_segDB.end()) {
		if (found_seg_global->second->is_used){
			fprintf(stderr, "ERROR : rvm_unmap | trying to unmap active segment\n");
			exit (EXIT_FAILURE);
		}
		#ifdef DEBUG
		printf("rvm_unmap | unmapping : %s\n", segname.c_str());
		#endif
		found_seg_global->second->is_mapped = false;
		found_seg_global->second->is_used = false;
	}
	else{
		fprintf(stderr, "ERROR : rvm_unmap | trying to unmap segment which does not exist!!!\n");
		exit (EXIT_FAILURE);
	}

	if(found_seg_ptr != global_rvmDB[rvm]->seg_name_map->end()){
		#ifdef DEBUG
		printf("rvm_unmap | segment :  %s\n", segname.c_str());
		#endif
		global_rvmDB[rvm]->seg_name_map->erase(found_seg_ptr);
	}

	found_seg = global_rvmDB[rvm]->seg_db->find(segname);
	if (found_seg != global_rvmDB[rvm]->seg_db->end()) {
		global_rvmDB[rvm]->seg_db->erase(found_seg);
	}


printf("rvm_unmap | segment :  %s\n", segname.c_str());
	
	
	return;
}
	

/*
  destroy a segment completely, erasing its backing store. 
  This function should not be called on a segment that is currently mapped.
 */
void rvm_destroy(rvm_t rvm, const char *segname){

	rvmDB::iterator found_rvm = global_rvmDB.find(rvm);

	if (found_rvm == global_rvmDB.end()) {
		fprintf(stderr, "ERROR | rvm_Map : No corresponding rvm found!\n");
		exit(EXIT_FAILURE);
	}

	string rvm_name = found_rvm->second->dirname;
	segDB::iterator found_seg_global = global_segDB.find(string(segname));
	if (found_seg_global != global_segDB.end()) {
		if (found_seg_global->second->is_mapped || found_seg_global->second->is_used){
			fprintf(stderr, "ERROR : rvm_destroy | destorying segment that is used!\n");
			exit (EXIT_FAILURE);
		}
		
		// Remove the segment from segment DB
		global_segDB.erase(found_seg_global);
	} else {
		//it is legal to try to delete file if we want to get rid of old data after failure
		#ifdef DEBUG
		fprintf(stderr, "WARNING : rvm_destroy | destorying non existing segment!\n");
		//exit (EXIT_FAILURE);
		#endif
	}

	// Destroying the .log file
	struct stat st;

	if ( stat((rvm_name + "/" + segname + ".log").c_str(), &st) == 0){
		int fd = unlink((rvm_name + "/" + segname + ".log").c_str());
		if (fd < 0){
			fprintf(stderr, "ERROR : rvm_destroy | can not delete persistent file for the segment!\n");
			exit (EXIT_FAILURE);
		}
	}
	if ( stat((rvm_name + "/" + segname + ".redo").c_str(), &st) == 0){
		int fd = unlink((rvm_name + "/" + segname + ".redo").c_str());
		if (fd < 0){
			fprintf(stderr, "ERROR : rvm_destroy | can not delete persistent file for the segment!\n");
			exit (EXIT_FAILURE);
		}
	}
	return;
}


/*
  begin a transaction that will modify the segments listed in segbases. 
  If any of the specified segments is already being modified by a transaction, 
	then the call should fail and return (trans_t) -1. 
  Note that trant_t needs to be able to be typecasted to an integer type.
 */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){

	int i = 0;
	trans_t trans_id = global_trans_cntr++;
	
	segnmap::iterator found_seg_ptr;
	segset::iterator found_seg_name;
	segDB::iterator found_seg_str;

	trans_node_t* trans = new trans_node_t;
	trans->seg_bases = new seg_base_set;
	trans->transactions = new trans_queue;
	trans->rvm_id = rvm;
	
	for(i = 0; i < numsegs; i++) {
		void *base = segbases[i];

		// Add segment base to the seg_base_list in the transaction
		trans->seg_bases->insert(base);	

		found_seg_ptr = global_rvmDB[rvm]->seg_name_map->find(base);
		// If Segment is not found in the list, something is wrong
		if(found_seg_ptr == global_rvmDB[rvm]->seg_name_map->end()) {
			fprintf(stderr, "ERROR : rvm_begin_trans | No corresponding address found!\n");
			exit(EXIT_FAILURE);
		}

		string found_seg_name = found_seg_ptr->second;

		found_seg_str = global_segDB.find(found_seg_name);
		seg_t *seg_str = found_seg_str->second;

		if(seg_str->is_used) {
			return -1;
		}
		// Set the segment is being used
		seg_str->is_used = true;
	}

	// Insert the transition to the transition list
	rvmDB::iterator find_rvm = global_rvmDB.find(rvm);
	if (find_rvm == global_rvmDB.end()){
		fprintf(stderr, "ERROR : rvm_begin_trans | fail to find rvm ID for the transaction!\n");
		exit (EXIT_FAILURE);
	}
	global_rvmDB[rvm]->trans->push_back(trans_id);
	
	// Insert trans_id and trans_unit to global_transDB
	global_transDB.insert(transDB_en(trans_id, trans));

	#ifdef DEBUG
	//////////////PRINTF!!!//////////////////////////////////
	printf("rvm_begin_trans | Transaction %d added\n", trans_id);
	transDB::iterator found_trans;
	printf("rvm_begin_trans | List of Transaction : ");
	for (found_trans = global_transDB.begin(); found_trans != global_transDB.end(); ++found_trans){
		printf("trans %d |", found_trans->first);
	}
	printf("\n");
	/////////////////////////////////////////////////////////
	#endif

	return trans_id;
}


/*
  declare that the library is about to modify a specified range of memory in the specified segment. 
  The segment must be one of the segments specified in the call to rvm_begin_trans. 
  Your library needs to ensure that the old memory has been saved, in case an abort is executed. 
  It is legal call rvm_about_to_modify multiple times on the same memory area.
*/
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size){
	//created redo and undo logs for the particular segment
	//created undo log for the particular segment

	// Allocate and copy current data to undo log
	void *undo_log = malloc(size);
	memcpy(undo_log, (uint8_t*)segbase + offset, size);

	transDB::iterator found_trans;
	#ifdef DEBUG
	//////////////PRINTF!!!//////////////////////////////////
	printf("about_to_modify | List of trans : ");
	for (found_trans = global_transDB.begin(); found_trans != global_transDB.end(); ++found_trans){
		printf("%d, ", found_trans->first);
	}
	printf("\n");
	/////////////////////////////////////////////////////////
	#endif

	// Get corresponding trans_unit_t
	found_trans = global_transDB.find(tid);
	if(found_trans == global_transDB.end()) {
		fprintf(stderr, "ERROR | about_to_modify : No transaction found in global DB!\n");
		exit(EXIT_FAILURE);
	}
	trans_node_t *t_node = found_trans->second;

	// find segment based on segbase address
	seg_base_set::iterator found_t_unit = t_node->seg_bases->find(segbase);
	if(found_t_unit == t_node->seg_bases->end()) {
		fprintf(stderr, "ERROR | about_to_modify : No segment base found for this transaction ID!\n");
		exit(EXIT_FAILURE);
	}
	trans_unit_t *new_t_unit = new trans_unit_t;
	new_t_unit->segbase = segbase;
	new_t_unit->undo_log = undo_log;
	new_t_unit->offset = offset;
	new_t_unit->size = size;

	// Add new undo_log to found trans_unit_t
	t_node->transactions->push_back(new_t_unit);

	#ifdef DEBUG
	//////////////PRINTF!!!//////////////////////////////////
	printf("about_to_modify | List of undo logs | ");
	trans_queue::iterator trans_it;
	for (trans_it = t_node->transactions->begin(); trans_it != t_node->transactions->end(); ++trans_it){
		printf("offset : %d, size: %d, segbase : %p| ", (*trans_it)->offset, (*trans_it)->size, (*trans_it)->segbase);
	}
	printf("\n");
	/////////////////////////////////////////////////////////
	#endif
}


/*
  commit all changes that have been made within the specified transaction. 
  When the call returns, then enough information should have been saved to disk so that, 
	even if the program crashes, the changes will be seen by the program when it restarts.
*/
void rvm_commit_trans(trans_t tid){
	transDB::iterator find_trans = global_transDB.find(tid);
	if (find_trans == global_transDB.end()){
		fprintf(stderr, "ERROR : rvm_commit | committing non existing transaction!\n");
		exit (EXIT_FAILURE);
	}
	trans_node_t *trans = find_trans->second;

	rvmDB::iterator find_rvm = global_rvmDB.find(trans->rvm_id);
	if (find_rvm == global_rvmDB.end()){
		fprintf(stderr, "ERROR : rvm_commit | fail to find rvm ID for the transaction!\n");
		exit (EXIT_FAILURE);
	}
	const rvm_struct *rvm = find_rvm->second;

	string rvm_name = rvm->dirname;

	trans_queue::iterator trans_it;
	for (trans_it = trans->transactions->begin(); trans_it != trans->transactions->end(); ++trans_it){
		trans_unit_t *t_unit = *trans_it;
		string segname;

		// Find segment used in the particular transition unit
		segnmap::iterator found_seg_ptr = rvm->seg_name_map->find(t_unit->segbase);
		if(found_seg_ptr != rvm->seg_name_map->end()){
			segname = found_seg_ptr->second;
		}
		else{
			fprintf(stderr, "ERROR : rvm_commit | fail to find segname for the transaction in global DB!!!\n");
			exit (EXIT_FAILURE);
		}

		struct stat st;

		// Write redo log
		int fd;
		
		// Check if redo log already exists
		if ( stat((rvm_name + "/" +  segname + ".redo").c_str(), &st) != 0){
			//log truncation if redo log is more than 10KB
			if (st.st_size > 102400){
				rvm_truncate_log(find_rvm->first);
			}
			
			//create undo log
			fd = creat((rvm_name + "/" + segname + ".redo").c_str(), 0777);
			if (fd < 0){
				fprintf(stderr, "ERROR : rvm_commit | can not create redo log for the segment!\n");
				exit (EXIT_FAILURE);
			}
			fd = open((rvm_name + "/" + segname + ".redo").c_str(), O_RDWR);
			write(fd, &t_unit->size, sizeof(int));
			close(fd);
		}

		// Open redo log
		fd = open((rvm_name + "/" + segname + ".redo").c_str(), O_RDWR);
		if (fd < 0){
			fprintf(stderr, "ERROR : rvm_commit | can not open redo log for the segment!\n");
			exit (EXIT_FAILURE);
		}

		// Write the transition unit to the redo log
		if (lseek(fd, 0, SEEK_END) >= 0){
			write(fd, &t_unit->size, sizeof(int));
			write(fd, &t_unit->offset, sizeof(int));
			//write(fd, &t_unit->segbase, t_unit->size);
			write(fd, (uint8_t*)t_unit->segbase + t_unit->offset, t_unit->size);
			close(fd);

			#ifdef DEBUG
			printf("rvm_commit_trans | size: %d offset: %d base: %p data: %s\n", 
					t_unit->size, t_unit->offset, (uint8_t*)t_unit->segbase, (char *)((uint8_t*)t_unit->segbase+t_unit->offset));
			#endif
		}
		else{
			fprintf(stderr, "ERROR : rvm_commit | can not handle redo log for the segment!\n");
			exit (EXIT_FAILURE);
		}

		// Set segments are not used anymore
		segDB::iterator found_seg_global = global_segDB.find(string(segname));
		if (found_seg_global != global_segDB.end()){
			found_seg_global->second->is_used = false;
		}
		else{
			fprintf(stderr, "ERROR : rvm_commit | can not find segment in global DB!\n");
			exit (EXIT_FAILURE);
		}
	}
	return;
}

/*
  undo all changes that have happened within the specified transaction.
 */
void rvm_abort_trans(trans_t tid){

	#ifdef DEBUG
	//////////////PRINTF!!!//////////////////////////////////
	printf("Transaction %d is aborting\n", tid);
	transDB::iterator found_trans;
	for (found_trans = global_transDB.begin(); found_trans != global_transDB.end(); ++found_trans){
		printf("trans %d\n", found_trans->first);
	}
	/////////////////////////////////////////////////////////
	#endif

	transDB::iterator find_trans = global_transDB.find(tid);
	if (find_trans == global_transDB.end()){
		fprintf(stderr, "ERROR : rvm_abort | committing non existing transaction!\n");
		exit (EXIT_FAILURE);
	}
	trans_node_t *trans = find_trans->second;

	rvmDB::iterator find_rvm = global_rvmDB.find(trans->rvm_id);
	if (find_rvm == global_rvmDB.end()){
		fprintf(stderr, "ERROR : rvm_abort | fail to find rvm ID for the transaction!\n");
		exit (EXIT_FAILURE);
	}
	const rvm_struct *rvm = find_rvm->second;

	trans_queue::iterator trans_it;
	for (trans_it = trans->transactions->begin(); trans_it != trans->transactions->end(); ++trans_it){
		trans_unit_t *t_unit = *trans_it;
		string segname;

		segnmap::iterator found_seg_ptr = rvm->seg_name_map->find(t_unit->segbase);
		if(found_seg_ptr != rvm->seg_name_map->end()){
			segname = found_seg_ptr->second;
		}
		else{
			fprintf(stderr, "ERROR : rvm_abort | fail to find segname for the transaction in global DB!!!\n");
			exit (EXIT_FAILURE);
		}

		#ifdef DEBUG
		/////////////////////////////////////////////////////////
		printf("Offset - %d, size - %d\n", (*trans_it)->offset, (*trans_it)->size);
		/////////////////////////////////////////////////////////
		#endif

		memcpy((uint8_t*)(*trans_it)->segbase + (*trans_it)->offset, (*trans_it)->undo_log, (*trans_it)->size);

		segDB::iterator found_seg_global = global_segDB.find(string(segname));
		if (found_seg_global != global_segDB.end()){
			found_seg_global->second->is_used = false;
		}
		else{
			fprintf(stderr, "ERROR : rvm_abort | can not find segment in global DB!\n");
			exit (EXIT_FAILURE);
		}
	}
	return;
}

/*
  play through any committed or aborted items in the log file(s) 
  and shrink the log file(s) as much as possible.
*/
void rvm_truncate_log(rvm_t rvm){

	rvmDB::iterator rvm_iter;
	rvm_iter = global_rvmDB.find(rvm);

	// Find corresponding rvm
	if(rvm_iter == global_rvmDB.end()) {
		fprintf(stderr, "ERROR : rvm_truncate | No rvm found!\n");
		exit(EXIT_FAILURE);
	}

	const rvm_struct *found_rvm = rvm_iter->second;
	string rvm_name = found_rvm->dirname;

	// Get transaction list from the rvm
	trans_list::iterator trans_list_iter;

	// Get list of segments for the rvm
	// iterate through the segment.log
	segset::iterator segset_iter;

	for(segset_iter = found_rvm->seg_db->begin(); segset_iter != found_rvm->seg_db->end(); ++segset_iter) {
		string found_segname = *segset_iter;

		redo_to_log(found_segname, rvm_name);
	}
	return;
}

void redo_to_log(string found_segname, string rvm_name) {

	struct stat st;
	int fd;
	int log_fd;

	// Check if redo log already exists
	if ( stat((rvm_name + "/" + found_segname + ".redo").c_str(), &st) != 0){
		// No redo log for the segment, then go to next segment
		return;
	}
	// Open redo log
	fd = open((rvm_name + "/" + found_segname + ".redo").c_str(), O_RDWR);
	if (fd < 0){
		fprintf(stderr, "ERROR : rvm_turncate | can not open redo log for the segment!\n");
		exit (EXIT_FAILURE);
	}

	// Check if log 
	if ( stat((rvm_name + "/" + found_segname + ".log").c_str(), &st) != 0){
		fprintf(stderr, "ERROR : rvm_truncate | No segment .log found!\n");
		exit (EXIT_FAILURE);
	}
	// Open redo log
	log_fd = open((rvm_name + "/" + found_segname + ".log").c_str(), O_RDWR);
	if (log_fd < 0){
		fprintf(stderr, "ERROR : rvm_turncate | can not open .log for the segment!\n");
		exit (EXIT_FAILURE);
	}

	// Get the end of file
	int filesize = lseek(fd, 0, SEEK_END);

	#ifdef DEBUG
	printf("truncate_log | log filesize is : %d\n", filesize);
	#endif

	int data_size = 0;
	int offset = 0;
	void *data;

	char buf[MAX_BUFF];
	int curr_offset = 0;

	lseek(fd, 0, SEEK_SET);

	// Read header - size of segment
	curr_offset += read(fd, &buf, sizeof(int));

	// Read data and its information
	while (curr_offset < filesize) {
		curr_offset += read(fd, &buf, sizeof(int));
		data_size = *((int *)buf);

		curr_offset += read(fd, &buf, sizeof(int));
		offset = *((int *)buf);


		curr_offset += read(fd, &buf, data_size);
		data = buf;

		#ifdef DEBUG
		printf("truncate_log | offset: %d size : %d writing data : %s \n", offset, data_size, (char*)data);
		#endif

		if (lseek(log_fd, offset, SEEK_SET) >= 0){
			write(log_fd, data, data_size);
		}
	}

	close(fd);
	close(log_fd);
	
	remove((rvm_name + "/" + found_segname + ".redo").c_str());
}