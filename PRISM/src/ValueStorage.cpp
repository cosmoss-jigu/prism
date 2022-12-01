#include "ValueStorage.h"

#define MTS_SET_TIMER(timestamp)  \
{                                   \
    timestamp = read_tscp();      \
}

std::random_device vs_rd;
std::mt19937 vs_gen(vs_rd());

ValueStorage::ValueStorage() {}
ValueStorage::~ValueStorage() {
    g_endVS = true;
    is_writing= false;

    for(int i = 0; i < IO_URING_RRING_NUM; i++) {
	io_uring_queue_exit(&r_ring[i]);
    }
    
    for(int i = 0; i < IO_URING_WRING_NUM; i++)
	io_uring_queue_exit(&w_ring[i]);

    io_uring_queue_exit(&gc_w_ring);
    io_uring_queue_exit(&gc_r_ring);

    close(fd[0]); 
}

bool sort_by_entry(const std::pair<int, int> i, const std::pair<int, int> j) {
    if (i.second == j.second) i.first < j.first;
    return (i.second < j.second);
}

bool sort_by_key_write(const std::pair<Key_t, vs_entry_t *> i, const std::pair<Key_t, vs_entry_t *> j) {
    return (i.first < j.first);
}

bool sort_by_key_sync(const std::pair<Key_t, at_entry_t *> i, const std::pair<Key_t, at_entry_t *> j) {
    return (i.first < j.first);
}

ValueStorage::ValueStorage(const char *path, int vs_num) {
    int ret;
    g_endVS = false;
    gc_done = false;
    vs_id = vs_num;
    ts_trace(TS_INFO, "CREATED_VALUESTORAGE: %d\n", vs_id);

    if((fd[0] = open(path, O_RDWR | O_CREAT | O_DIRECT | O_NOATIME, 0666)) < 0) {
	perror("file open failed\n");
	exit(EXIT_FAILURE);
    }

    if(ftruncate(fd[0], MTS_VS_SIZE) == -1) {
	perror("ValueStorage ftruncate failed\n");
	exit(EXIT_FAILURE);
    }
    
    if(fallocate(fd[0], 0, 0, MTS_VS_SIZE) == -1) {
	perror("ValueStorage fallocate failed\n");
	exit(EXIT_FAILURE);
    }

    /* Prepare MANIFEST for ValueStorage */
    init_vs_bitmap_info();
    /* Indicates the offset of free chunks */
    init_free_chunk_list();
    /* Indicates the offset of used chunks and the number of how many valid entries are in */
    init_used_chunk_list();
    /* Managing victim & free chunks whil garbage collecting */

    for(int i = 0; i < IO_URING_RRING_NUM; i++) {
	struct io_uring_params *params = (struct io_uring_params *)malloc(sizeof(*params));
	memset(params, 0, sizeof(*params));

	ret = io_uring_queue_init_params(R_QD, &r_ring[i], params);
	if(ret < 0) {
	    ts_trace(TS_ERROR, "iouring r_read queue_init failed!\n");
	    exit(EXIT_FAILURE);
	}

	for (int j = 0; j < R_QD; j++) {
	    ret = posix_memalign((void **)&r_buffer[i][j].iov_base, SECTOR_SIZE, READ_IO_SIZE);
	    if(ret != 0) {
		ts_trace(TS_ERROR, "Failed to allocate r_buffer memory ValueStorage::get_val\n");
		exit(EXIT_FAILURE);
	    }
	    r_buffer[i][j].iov_len = READ_IO_SIZE;
	    memset(r_buffer[i][j].iov_base, 0, READ_IO_SIZE);
	}

	ret = io_uring_register_buffers(&r_ring[i], r_buffer[i], R_QD);
	if(ret) {
	    fprintf(stderr, "Please try again with with superuser privileges.\n");
	    fprintf(stderr, "Error registering buffers: %s", strerror(-ret));
	    exit(EXIT_FAILURE);
	}

	pending_ios[i] = 0;
	is_working[i] = 0;
	cur_pending_vec_ready[i] = true;

	/* for enqueueing at_entries,*/
	src_at_entry_vec[i] = new std::vector<at_entry_t *>;
	src_at_entry_vec[i]->reserve(R_QD);
	dst_at_entry_vec[i] = new std::vector<at_entry_t *>;
	dst_at_entry_vec[i]->reserve(R_QD);
    }
    ts_trace(TS_INFO, "IO_URING_READ_POLL enable!\n");

    for(int i = 0; i < IO_URING_WRING_NUM; i++) {
	ret = io_uring_queue_init(W_QD, &w_ring[i], 0);

	if(ret < 0) {
	    ts_trace(TS_ERROR, "iouring write queue_init failed!\n");
	    exit(EXIT_FAILURE);
	}
    }
    
    for (int i = 0; i < MTS_THREAD_NUM; i++) {
	ret = posix_memalign((void **)&w_buffer[i], SECTOR_SIZE, MTS_VS_CHUNK_SIZE);
	if(ret != 0) {
	    ts_trace(TS_ERROR, "Failed to allocate w_buffer memory ValueStorage::init()\n");
	    exit(EXIT_FAILURE);
	}

	ret = posix_memalign((void **)&s_buffer[i], SECTOR_SIZE, MTS_VS_CHUNK_SIZE);
	if(ret != 0) {
	    ts_trace(TS_ERROR, "Failed to allocate s_buffer memory ValueStorage::sort_chunk()\n");
	    exit(EXIT_FAILURE);
	}
    }

    ret = io_uring_queue_init(GC_QD, &gc_w_ring, 0);

    if(ret < 0) {
	ts_trace(TS_ERROR, "iouring gc-write queue_init failed!\n");
	exit(EXIT_FAILURE);
    }

    ret = io_uring_queue_init(GC_QD, &gc_r_ring, 0);

    if(ret < 0) {
	ts_trace(TS_ERROR, "iouring gc-read queue_init failed! %d \n", vs_id);
	exit(EXIT_FAILURE);
    }

    ret = posix_memalign((void **)&gc_w_buffer, SECTOR_SIZE, MTS_VS_CHUNK_SIZE);
    if(ret != 0) {
	ts_trace(TS_ERROR, "Failed to allocate memory(gc_w_buffer)\n");
	exit(EXIT_FAILURE);
    }

    ret = posix_memalign((void **)&gc_s_buffer, SECTOR_SIZE, MTS_VS_CHUNK_SIZE);
    if(ret != 0) {
	ts_trace(TS_ERROR, "Failed to allocate memory(gc_s_buffer)\n");
	exit(EXIT_FAILURE);
    }

    ret = posix_memalign((void **)&gc_r_buffer,  SECTOR_SIZE , MTS_VS_CHUNK_SIZE);
    if(ret != 0) {
	ts_trace(TS_ERROR, "Failed to allocate memory(gc_r_buffer)\n");
	exit(EXIT_FAILURE);
    }

    for(int i = 0; i < MTS_THREAD_NUM; i++) {
	w_chunk[i] = new w_chunk_t;
	w_chunk[i]->entry_offset = 0;
	moved_entry_list[i] = new std::vector<moved_entry_t>;	/* for sync at-vs */
	s_moved_entry_list[i] = new std::vector<std::pair<Key_t, at_entry_t *>>; /* for sorting */
	s_entry_list[i] = new std::vector<std::pair<Key_t, vs_entry_t *>>;

	moved_entry_list[i]->reserve(MTS_VS_ENTRIES_PER_CHUNK);
	s_moved_entry_list[i]->reserve(MTS_VS_ENTRIES_PER_CHUNK);
	s_entry_list[i]->reserve(MTS_VS_ENTRIES_PER_CHUNK);
    }
    
    gc_w_chunk = new w_chunk_t;
    gc_moved_entry_list = new std::vector<moved_entry_t>;
    s_gc_moved_entry_list = new std::vector<std::pair<Key_t, at_entry_t *>>;
    s_gc_entry_list = new std::vector<std::pair<Key_t, vs_entry_t *>>;

    /* r_ring, scan_r_ring bitmap */
    for(int i = 0; i < IO_URING_RRING_NUM; i++) {
	r_ring_bitmap[i] = false; 
    }

    last_ring_idx = 0;
    cur_ring_idx = 0;
    total_vs_write_count = 0;
}

uint32_t ValueStorage::get_vs_id() {
    return vs_id;
}

/////////////////////////////////////////
////* Write value from valuestorage *////
/////////////////////////////////////////

void ValueStorage::put_vs_entry(int oplog_id, Key_t key, Val_t val, at_entry_t *at_entry) {
    /* step 1. Copy value and at_entry from oplog to w_buffer
     * step 2. when w_buffer is full, write()
     * step 3. after writing a chunk, sync_meatadata()
     */

    /* allocating a new vs_entry(value, addresstable address) */

    w_chunk_t *chunk = w_chunk[oplog_id];

    if(chunk->entry_offset == 0) {
	init_w_chunk(oplog_id);
    } 

    if(chunk->entry_offset < MTS_VS_ENTRIES_PER_CHUNK) {
	vs_entry_t vs_entry = alloc(key, val, at_entry);
	chunk->w_buffer[chunk->entry_offset] = vs_entry;
	add_moved_entry_list(chunk->moved_entry_list, chunk->chunk_offset, chunk->entry_offset,
		(vs_entry_t *)&chunk->w_buffer[chunk->entry_offset], OpForm::INSERT);
	chunk->entry_offset++;
    } else if(chunk->entry_offset == MTS_VS_ENTRIES_PER_CHUNK) {
	write_chunk(chunk, NORMAL_WRITE);
	sync_with_at(chunk->moved_entry_list, chunk->s_moved_entry_list);

	init_w_chunk(oplog_id);

	vs_entry_t vs_entry = alloc(key, val, at_entry);
	chunk->w_buffer[chunk->entry_offset] = vs_entry; 
	add_moved_entry_list(chunk->moved_entry_list, chunk->chunk_offset, chunk->entry_offset,
		(vs_entry_t *)&chunk->w_buffer[chunk->entry_offset], OpForm::INSERT);
	chunk->entry_offset++;
    }
}

void *ValueStorage::sort_moved_entry(std::vector<moved_entry_t> *moved_entry_list, 
	std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list) {
    for (std::vector<moved_entry_t>::iterator itr = moved_entry_list->begin(); itr != moved_entry_list->end(); itr++) {
	moved_entry_t temp = *itr;

	Key_t key = temp.key;
	at_entry_t *s_entry = temp.at_entry;
	auto s_moved_entry = std::make_pair(key, s_entry);
	s_moved_entry_list->push_back(s_moved_entry);
    }

    sort(s_moved_entry_list->begin(), s_moved_entry_list->end(), sort_by_key_sync);

    int i = 0;
    for(std::vector<std::pair<Key_t, at_entry_t *>>::iterator itr = s_moved_entry_list->begin(); itr != s_moved_entry_list->end(); itr++) {
	moved_entry_list->at(i).at_entry = itr->second;
	i++;
    }

    s_moved_entry_list->clear();
    return moved_entry_list;
}


void ValueStorage::sync_with_at(std::vector<moved_entry_t> *moved_entry_list, std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list) {
    /* add sort func */
    sort_moved_entry(moved_entry_list, s_moved_entry_list);
    ts_trace(TS_INFO, "[SYNC] BEGIN!! SIZE: %lu\n", moved_entry_list->size());

    for (std::vector<moved_entry_t>::iterator itr = moved_entry_list->begin(); itr != moved_entry_list->end(); itr++) {
	moved_entry_t temp = *itr;
	
	link_to_at(temp.chunk_offset, temp.entry_offset, temp.at_entry);
    }
    ts_trace(TS_INFO, "[SYNC] END!!\n");

    /* clear all elements */
    moved_entry_list->clear();
}

void ValueStorage::gc_sync_with_at(std::vector<moved_entry_t> *gc_moved_entry_list,  std::vector<std::pair<Key_t, at_entry_t *>> *s_gc_moved_entry_list) {
    sort_moved_entry(gc_moved_entry_list, s_gc_moved_entry_list);
    for (std::vector<moved_entry_t>::iterator itr = gc_moved_entry_list->begin(); itr != gc_moved_entry_list->end(); itr++) {
	moved_entry_t temp = *itr;

	if (temp.op_type == OpForm::INSERT) 
	    gc_link_to_at(temp.chunk_offset, temp.entry_offset, temp.at_entry);
	else if (temp.op_type == OpForm::REMOVE) 
	    unlink_to_at(temp.chunk_offset, temp.entry_offset, temp.at_entry);
    }

    ts_trace(TS_INFO, "[GC-SYNC] END!!\n");

    /* clear all elements */
    gc_moved_entry_list->clear();
}

void ValueStorage::forced_write_chunk(int oplog_id) {
    ts_trace(TS_INFO, "[forced_write_chunk] start \n");
    w_chunk_t *chunk = w_chunk[oplog_id];

    write_chunk(chunk, NORMAL_WRITE);
    sync_with_at(chunk->moved_entry_list, chunk->s_moved_entry_list);
    chunk->entry_offset = 0;

    ts_trace(TS_INFO, "[forced_write_chunk] end \n");
}

void ValueStorage::gc_link_to_at(int chunk_offset, int entry_offset, at_entry_t *at_entry) {
    vs_idx_t vs_idx = at_entry->vs_idx;
    int cur_vs_id = vs_idx.vs_id;
    int cur_vs_offset = vs_idx.vs_offset;
    int new_vs_offset = chunk_offset * MTS_VS_ENTRIES_PER_CHUNK + entry_offset;

    if(smp_cas(&cur_vs_offset, NEWLY_UPDATED_VAL, &cur_vs_offset)) {
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] UPDATED VAL, SKIP | CASE 1. VALUE IS NEWLY UPDATED IN LOG | CHUNK_OFFSET: %d, ENTRY_OFFSET: %d\n", chunk_offset, entry_offset);
    } else if(smp_cas(&cur_vs_offset, PRE_VALUESTORAGE_VAL, &cur_vs_offset)) {
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] UPDATED VAL, SKIP | CASE 2. VALUE IS ARRIVING IN VS | CHUNK_OFFSET: %d, ENTRY_OFFSET: %d\n", chunk_offset, entry_offset);
    } else if(cur_vs_id != vs_id) {
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] UPDATED VAL, SKIP | CASE 3. ANOTHER VS TRIES TO UPDATE VALUE | CHUNK_OFFSET: %d, ENTRY_OFFSET: %d\n", chunk_offset, entry_offset);
    } else {
	assert(chunk_offset < MTS_VS_CHUNK_NUM);	
	assert(entry_offset < MTS_VS_ENTRIES_PER_CHUNK);

	vs_idx_t new_vs_idx;
	new_vs_idx.vs_id = vs_id;
	new_vs_idx.vs_offset = new_vs_offset;

	pmem_memcpy((void *)&at_entry->vs_idx, (void *)&new_vs_idx, sizeof(vs_idx_t), PMEM_F_MEM_NONTEMPORAL);
	_mm_sfence();

	ts_trace(TS_INFO, "[GC_VS_LINK_TO_AT] VS_ID: %d, CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, ENTRY_COUNT: %lu, at_entry: %p\n",
		vs_id, chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).count(), at_entry);

	set_vs_bitmap_info(chunk_offset, entry_offset);
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, test(): %d\n", 
		chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).test(entry_offset));
    }
    return;
}

void ValueStorage::link_to_at(int chunk_offset, int entry_offset, at_entry_t *at_entry) {
    op_entry_t *op_entry = (op_entry_t *)at_entry->val_addr;
    int cur_vs_offset = at_entry->vs_idx.vs_offset;
    int new_vs_offset = chunk_offset * MTS_VS_ENTRIES_PER_CHUNK + entry_offset;

    if(smp_cas(&cur_vs_offset, NEWLY_UPDATED_VAL, &cur_vs_offset)) {
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] UPDATED VAL, SKIP | CASE 1. VALUE IS NEWLY UPDATED IN LOG | CHUNK_OFFSET: %d, ENTRY_OFFSET: %d\n", chunk_offset, entry_offset);
    } else { 
	assert(chunk_offset < MTS_VS_CHUNK_NUM);	
	assert(entry_offset < MTS_VS_ENTRIES_PER_CHUNK);

	vs_idx_t new_vs_idx;
	new_vs_idx.vs_id = vs_id;
	new_vs_idx.vs_offset = new_vs_offset;

	pmem_memcpy((void *)&at_entry->vs_idx, (void *)&new_vs_idx, sizeof(vs_idx_t), PMEM_F_MEM_NONTEMPORAL);
	_mm_sfence();

	ts_trace(TS_INFO, "[VS_LINK_TO_AT] VS_ID: %d, CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, ENTRY_COUNT: %lu, at_entry: %p\n",
		vs_id, chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).count(), at_entry);

	set_vs_bitmap_info(chunk_offset, entry_offset);
	ts_trace(TS_INFO, "[SET_VS_BITMAP_INFO] CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, test(): %d\n", 
		chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).test(entry_offset));

	//if(get_tag((intptr_t)at_entry->val_addr) == DCACHE_VAL)
	    //at_entry->val_addr = nullptr;
    }
    return;
}

void ValueStorage::unlink_to_at(int chunk_offset, int entry_offset, at_entry_t *at_entry) {
    if(!vs_bitmap_info->at(chunk_offset).test(entry_offset)) {
	return;
    }

    ts_trace(TS_INFO, "[CLEAR_VS_BITMAP_INFO] CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, ENTRY_COUNT: %lu, test(): %d\n", 
	    chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).count(), vs_bitmap_info->at(chunk_offset).test(entry_offset));

    clear_vs_bitmap_info(chunk_offset, entry_offset);

    ts_trace(TS_INFO, "[VS_UNLINK_TO_AT] CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, ENTRY_COUNT: %lu, at_entry: %p\n",
	    chunk_offset, entry_offset, vs_bitmap_info->at(chunk_offset).count(), at_entry);

    return;
}

void ValueStorage::set_vs_bitmap_info(int chunk_offset, int entry_offset) {
    assert((vs_bitmap_info->at(chunk_offset).count() <= MTS_VS_ENTRIES_PER_CHUNK));
    vs_bitmap_info->at(chunk_offset).set(entry_offset);
}

void ValueStorage::clear_vs_bitmap_info(int chunk_offset, int entry_offset) {
    vs_bitmap_info->at(chunk_offset).reset(entry_offset);

    if(vs_bitmap_info->at(chunk_offset).count() == 0) {
	add_free_chunk_list(chunk_offset);
    }

}

vs_entry_t ValueStorage::alloc(Key_t key, Val_t val, at_entry_t *at_entry) {
    at_entry->vs_idx.vs_offset = PRE_VALUESTORAGE_VAL;

    vs_entry_t vs_entry;
    vs_entry.key = key;
    vs_entry.val = val;
    vs_entry.at_entry = at_entry;

    return vs_entry;
}

void ValueStorage::init_w_chunk(int oplog_id) {
    is_writing = true;
    spinlock.lock();

    /* DO GARBAGE COLLECTION */
    if(unlikely(not_enough_free_chunk())) {
	this->gc_done = true;
	garbage_collection();
    } else this->gc_done = false;

    w_chunk[oplog_id]->id = oplog_id;
    w_chunk[oplog_id]->chunk_offset = get_free_chunk_offset();
    w_chunk[oplog_id]->entry_offset = 0;
    w_chunk[oplog_id]->w_buffer = w_buffer[oplog_id];
    w_chunk[oplog_id]->s_buffer = s_buffer[oplog_id];
    w_chunk[oplog_id]->moved_entry_list = moved_entry_list[oplog_id];
    w_chunk[oplog_id]->s_moved_entry_list = s_moved_entry_list[oplog_id];
    w_chunk[oplog_id]->s_entry_list = s_entry_list[oplog_id];

    /* INIT WRITE BUFFER */
    ts_trace(TS_INFO, "[INIT_W_CHUNK] w_chunk_offset: %d, MTS_VS_USED: %lu, MTS_VS_SIZE: %lu\n",
	    w_chunk[oplog_id]->chunk_offset, MTS_VS_CHUNK_SIZE * w_chunk[oplog_id]->chunk_offset, MTS_VS_SIZE);

    is_writing = false;
    spinlock.unlock();
}

void ValueStorage::init_gc_w_chunk() {
    gc_w_chunk->chunk_offset = get_free_chunk_offset();
    gc_w_chunk->entry_offset = 0;
    gc_w_chunk->w_buffer = gc_w_buffer;
    gc_w_chunk->s_buffer = gc_s_buffer;
    gc_w_chunk->moved_entry_list = gc_moved_entry_list;
    gc_w_chunk->s_moved_entry_list = s_gc_moved_entry_list;
    gc_w_chunk->s_entry_list = s_gc_entry_list;

    ts_trace(TS_INFO, "[INIT_GC_W_CHUNK] w_chunk_offset: %d, MTS_VS_USED: %lu, MTS_VS_SIZE: %lu\n",
	    gc_w_chunk->chunk_offset, MTS_VS_CHUNK_SIZE * gc_w_chunk->chunk_offset, MTS_VS_SIZE);

}

////////////////////////////////////////
////* Read value from valuestorage *////
////////////////////////////////////////

int ValueStorage::get_val_ccsync(std::vector<at_entry_t *> *at_entry_vec, int ring_idx) {
    int ret;
    Val_t val;
    int entry_idx = 0;

    //ts_trace(TS_ERROR, "ccsync %d\n", at_entry_vec->size());
    for(std::vector<at_entry_t *>::iterator itr = at_entry_vec->begin(); itr != at_entry_vec->end(); itr++) {
	at_entry_t *at_entry = *itr;

	/* Value has just moved into the oplog */
	if(unlikely(at_entry->vs_idx.vs_offset < 0)) {
	    op_entry_t *op_entry = (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
	    val = op_entry->val;
	    ts_trace(TS_INFO, "[GET_VAL_ASYNC] entry in the oplog | val: %d\n", val);
#ifdef MTS_STATS_LATENCY
	    uint64_t start, end, elapsed_time;
	    end = read_tscp();
	    start = at_entry->timestamp;
	    if(start) {
		elapsed_time = end - start;
		add_timing_stat(elapsed_time, OPLOG_VAL);
		at_entry->timestamp = 0;
	    }
#endif
	    continue;
	}

	/* Step 1. gathering chunk/vs_entry offset */
	int r_chunk_offset = at_entry->vs_idx.vs_offset / MTS_VS_ENTRIES_PER_CHUNK;
	int r_vs_entry_offset = at_entry->vs_idx.vs_offset % MTS_VS_ENTRIES_PER_CHUNK;

	/* validation test */
	struct io_uring_sqe *r_sqe;
	off64_t offset = (r_chunk_offset * MTS_VS_CHUNK_SIZE + r_vs_entry_offset * MTS_VS_ENTRY_SIZE);

	r_sqe = io_uring_get_sqe(&r_ring[ring_idx]);
	if(!r_sqe) {
	    ts_trace(TS_ERROR, "[GET_VAL_ASYNC] get set failed, will submit sqe %d\n", entry_idx);
	    break;
	}

	io_uring_prep_read_fixed(r_sqe, fd[0], r_buffer[ring_idx][entry_idx].iov_base, READ_IO_SIZE, offset, entry_idx);
	io_uring_sqe_set_data(r_sqe, r_buffer[ring_idx][entry_idx].iov_base);
	ts_trace(TS_INFO, "sub at_entry %p ring_idx %d vs_id %d offset[idx] %lu idx %d\n", 
		at_entry, ring_idx, vs_id, offset, entry_idx);
	entry_idx++;
    }
    at_entry_vec->clear();

    int pending = entry_idx;
    if(unlikely(pending == 0)) {
	return pending;
    }

    ret = io_uring_submit(&r_ring[ring_idx]);
    if(ret != pending) {
	ts_trace(TS_ERROR, "[GET_VAL_ASYNC] %d io_uring_submit failed! %s %d %d\n", ring_idx, strerror(-ret), ret, pending);
	exit(EXIT_FAILURE);
    }

    pending_ios[ring_idx] = pending;
    smp_mb();

    return pending; 
}

cq_entry_t *ValueStorage::make_cq_entry(at_entry_t *at_entry, Val_t val) {
    cq_entry_t *cq_entry = new cq_entry_t;
    cq_entry->at_entry = at_entry;
    cq_entry->val = val;

    ts_trace(TS_INFO, "[MAKE_CQ_ENTRY] %p %lu\n", at_entry, val);

    return cq_entry;
}

int ValueStorage::get_val_scan(std::vector<at_entry_t *> *at_entry_vec, int ring_idx) {
    int ret;
    Val_t val;
    int entry_idx = 0;

    for(std::vector<at_entry_t *>::iterator itr = at_entry_vec->begin(); itr != at_entry_vec->end(); itr++) {
	at_entry_t *at_entry = *itr;

	if(at_entry->vs_idx.vs_offset < 0) {
	    op_entry_t *op_entry = (op_entry_t *)get_untagged_ptr((intptr_t)at_entry->val_addr);
	    ts_trace(TS_INFO, "[GET_VAL_SCAN] r_chunk_offset: %d, r_vs_entry_offset: %d\n", r_chunk_offset, r_vs_entry_offset);
	    val = op_entry->val;
	    continue;
	}

	/* Step 1. gathering chunk/vs_entry offset */
	int r_chunk_offset = at_entry->vs_idx.vs_offset / MTS_VS_ENTRIES_PER_CHUNK;;
	int r_vs_entry_offset = at_entry->vs_idx.vs_offset % MTS_VS_ENTRIES_PER_CHUNK;

	struct io_uring_sqe *r_sqe = io_uring_get_sqe(&r_ring[ring_idx]);
	if(!r_sqe) {
	    ts_trace(TS_ERROR, "[GET_VAL_SCAN] get set failed, will submit sqe\n");
	    break;
	}

	off64_t offset = r_chunk_offset * MTS_VS_CHUNK_SIZE + r_vs_entry_offset * MTS_VS_ENTRY_SIZE;
	io_uring_prep_read_fixed(r_sqe, fd[0], r_buffer[ring_idx][entry_idx].iov_base, READ_IO_SIZE, offset, entry_idx);
	io_uring_sqe_set_data(r_sqe, r_buffer[ring_idx][entry_idx].iov_base);

	entry_idx++;
    }
    at_entry_vec->clear();

    int pending = entry_idx;

    if(unlikely(pending == 0)) {
	return pending;
    }

    ret = io_uring_submit(&r_ring[ring_idx]);
    if(ret != pending) {
	ts_trace(TS_ERROR, "[GET_VAL_SCAN] io_uring_submit failed! vs_id %d ring_idx %d %d %d\n", vs_id, ring_idx, ret, pending);
	exit(EXIT_FAILURE);
    }

    pending_ios[ring_idx] = pending;
    return pending;
}


//////////////////////////////////////////////
////* Garbage collection of valuestorage *////
//////////////////////////////////////////////

/* Garbage collection process of ValueStorage
 * 0. init_w_chunk for writing new chunk
 * 1. not_enough_free_chunk() or not
 * 2. if (not_enough), run GC
 * 3. read entries from get_victim()
 * 4. get_free_chunk_offset()
 * 5. put_vs_entry()
 * -. goto 1.
 */
bool ValueStorage::not_enough_free_chunk() {
    if (get_used_chunk_num() > MTS_VS_HIGH_MARK) { 
	ts_trace(TS_INFO, "NOT ENOUGH FREE CHUNKS | VS_ID: %d | TOTAL: %lu | USED: %u | FREE: %lu\n", 
		get_vs_id(), MTS_VS_CHUNK_NUM, get_used_chunk_num(), MTS_VS_CHUNK_NUM - get_used_chunk_num()); 
	return true;
    }
    else {
	ts_trace(TS_INFO, "HAS ENOUGH FREE CHUNKS | TOTAL: %lu | USED: %u | FREE: %lu\n", 
		MTS_VS_CHUNK_NUM, get_used_chunk_num(),  MTS_VS_CHUNK_NUM - get_used_chunk_num());
	return false;
    }
}

bool ValueStorage::garbage_collection() {
    /* vs_info for gc 
     * 1. [entry unit] used_chunk_list:	has the number of valid entries per each chunk
     * 2. [chunk unit] free_chunk_list:	indicates free_chunk_offset for getting a new chunk
     * 3. [entry unit] vs_bitmap_info:	shows the position of valid entries of each chunk
     */

    bool whole_file_written = true;
    unsigned int victim_candidate_order = 0; /* means victim order of sorted used_chunk_list */
    int r_offset, w_offset; /* each offset indicates the position will read or write */
    int gc_w_chunk_offset, gc_r_chunk_offset; /* each offset indicates the pos. of chunk (r/w) */
    int used_chunk_num = get_used_chunk_num(); /* for the condition of terminating gc */
    int used_chunk_num_before_gc = used_chunk_num;

    /* whether garbage collection begeins or not */
    if(create_used_chunk_list() < 2) {
	ts_trace(TS_INFO, "%d GC_END | CASE 1 GC MUST REQUIRE AT LEAST TWO VICTIM CHUNKS\n", mts_get_now());
	return EXIT_SUCCESS;
    }

    unsigned int victim_chunk_candidate1 = used_chunk_list->at(victim_candidate_order).second;
    unsigned int victim_chunk_candidate2 = used_chunk_list->at(victim_candidate_order+1).second;
    if ((victim_chunk_candidate1 + victim_chunk_candidate2) > MTS_VS_ENTRIES_PER_CHUNK) {
	ts_trace(TS_INFO, "%d GC_END | CASE 2 THE SUM OF THE SMALLEST VICTIMS IS GREATER THAN MTS_VS_ENTRIES_PER_CHUNK\n", mts_get_now());
	return EXIT_SUCCESS;
    }

    ts_trace(TS_GC_DEBUG, "%d GC_BEGIN | VS_ID: %d | TOTAL: %lu | USED: %u | FREE: %lu\n", 
	    mts_get_now(), get_vs_id(), MTS_VS_CHUNK_NUM, get_used_chunk_num(), MTS_VS_CHUNK_NUM - get_used_chunk_num());

    w_offset = 0;
    r_offset = 0;

    init_gc_w_chunk();
    gc_w_chunk_offset = gc_w_chunk->chunk_offset;

    do {
	/* getting victim chunk offset */
	gc_r_chunk_offset = get_victim_chunk_offset(&victim_candidate_order);
	victim_candidate_order++;

	/* case: no proper victim chunks */
	if(gc_r_chunk_offset < 0) {
	    if(gc_moved_entry_list->empty()) {
		ts_trace(TS_INFO, "[whole_file_written] add_free_chunk: %d\n", gc_w_chunk_offset);
		add_free_chunk_list(gc_w_chunk_offset);
		ts_trace(TS_GC_DEBUG, "%d GC_END | VS_ID: %d | TOTAL: %lu | UED: %u | FREE: %lu | CASE 3 VICTIM's ENTRY NUM == MTS_VS_ENTRIES_PER_CHUNK\n", mts_get_now(), get_vs_id(), MTS_VS_CHUNK_NUM, get_used_chunk_num(), MTS_VS_CHUNK_NUM - get_used_chunk_num());
	    } else {
		write_gc_w_chunk(gc_w_chunk_offset);
		gc_sync_with_at(gc_w_chunk->moved_entry_list, gc_w_chunk->s_moved_entry_list);
		ts_trace(TS_GC_DEBUG, "%d GC_END | VS_ID: %d | TOTAL: %lu | UED: %u | FREE: %lu | CASE 5 NO MORE VICTIMS \n", mts_get_now(), get_vs_id(), MTS_VS_CHUNK_NUM, get_used_chunk_num(), MTS_VS_CHUNK_NUM - get_used_chunk_num());
	    }

	    return EXIT_SUCCESS;
	}


	/* reading(file I/O) victim chunk */
	read_gc_r_chunk(gc_r_chunk_offset);

	/* getting valid entry of victim chunk */
	for(unsigned int i = 0; i < MTS_VS_ENTRIES_PER_CHUNK; i++) {
	    if(vs_bitmap_info->at(gc_r_chunk_offset).test(i)) {
		r_offset = i;
		ts_trace(TS_INFO, "[GC_DEBUG]: VALID_ENTRY | gc_r_chunk_offset: %d, r_offset: %d\n", gc_r_chunk_offset, i);
	    } else {
		ts_trace(TS_INFO, "[GC_DEBUG]: INVALID_ENTRY | gc_r_chunk_offset: %d, r_offset: %d\n", gc_r_chunk_offset, i);
		continue;
	    }

	    /* copy entry from victim chunk to newly allocaed chunk */
	    memcpy((void *)&gc_w_buffer[w_offset], (void *)&gc_r_buffer[r_offset], sizeof(vs_entry_t));

	    /* will sync */
	    add_moved_entry_list(gc_moved_entry_list, gc_w_chunk_offset, w_offset, (vs_entry *)&gc_w_buffer[w_offset], OpForm::INSERT);
	    add_moved_entry_list(gc_moved_entry_list, gc_r_chunk_offset, r_offset, (vs_entry *)&gc_r_buffer[r_offset], OpForm::REMOVE);


	    /* write newly gathered entries and init gc_w_buffer */
	    /* because w_offset starts from '0' */
	    if(w_offset == (MTS_VS_ENTRIES_PER_CHUNK - 1)) { 
		write_gc_w_chunk(gc_w_chunk_offset);
		ts_trace(TS_INFO, "[GC: END OF WRITING A NEW CHUNK]\n");

		/* link/unlink from valuestorage to addresstable */
		gc_sync_with_at(gc_w_chunk->moved_entry_list, gc_w_chunk->s_moved_entry_list);
		used_chunk_num++;

		/* condition of terminating garbage collection */
		if ((used_chunk_num == used_chunk_num_before_gc) || (used_chunk_num < MTS_VS_LOW_MARK)) {
		    ts_trace(TS_GC_DEBUG, "%d GC_END | VS_ID: %d | TOTAL: %lu | UED: %u | FREE: %lu | CASE 4 REACH MTS_VS_LOW_MARK NO EFFECT OF GC\n", mts_get_now(), get_vs_id(), MTS_VS_CHUNK_NUM, get_used_chunk_num(), MTS_VS_CHUNK_NUM - get_used_chunk_num());
		    return EXIT_SUCCESS;
		}

		/* prepare next chunk to be written */
		init_gc_w_chunk();
		gc_w_chunk_offset = gc_w_chunk->chunk_offset;
		w_offset = 0;
	    } else {
		/* move to next w_offset */
		w_offset++;
	    }
	}
	/* one chunk is considered free */
	used_chunk_num--;
	ts_trace(TS_INFO, "[GC: END OF READING SINGLE CHUNK]\n");
    } while(true);
}

void ValueStorage::add_moved_entry_list(std::vector<moved_entry_t> *moved_entry_list, int chunk_offset, int entry_offset, vs_entry_t *vs_entry, OpForm::Operation op_type) {

    moved_entry_t moved_entry;

    moved_entry.key = vs_entry->key;
    moved_entry.chunk_offset = chunk_offset;
    moved_entry.entry_offset = entry_offset;
    moved_entry.at_entry = vs_entry->at_entry;
    moved_entry.op_type = op_type;

    moved_entry_list->push_back(moved_entry);

    ts_trace(TS_INFO, "[REC 3. ADD_MOVED_ENTRY_LIST] moved_entry_info.: %lu | VS_ID: %d, CHUNK_OFFSET: %d, ENTRY_OFFSET: %d, at_entry: %p, op_tpye: %d, Value: %lu\n",
	    moved_entry_list->size(), vs_id, moved_entry.chunk_offset, moved_entry.entry_offset, moved_entry.at_entry, moved_entry.op_type, vs_entry->val);
}

void ValueStorage::init_vs_bitmap_info() {
    vs_bitmap_info = new std::vector<vs_bitmap>(MTS_VS_CHUNK_NUM);

    for(unsigned long int i = 0; i < MTS_VS_CHUNK_NUM; i++) {
	vs_bitmap bitmap(0);
	vs_bitmap_info->push_back(bitmap);
    }
}

void ValueStorage::init_used_chunk_list() {
    used_chunk_list = new std::vector<std::pair<int, int>>;
}

void ValueStorage::init_free_chunk_list() {
    free_chunk_list = new std::vector<int>;
    for(int i = 0; i < MTS_VS_CHUNK_NUM; i++)
	free_chunk_list->push_back(i);
}

int ValueStorage::get_victim_chunk_offset(unsigned int *order) {
    /* step 1. find minimum value
     * step 2. find the pos. of min. value
     */

    int victim_chunk_offset, victim_chunk_entries;

    if(!(*order < used_chunk_list->size())) {
	ts_trace(TS_INFO, "[GET_VICITM_CHUNK_OFSET] *order over used_chunk_list->size()\n");
	return -1;
    }

    ts_trace(TS_INFO, "VICTIM_CHUNK_OFFSET | *order: %d\n", *order);
    victim_chunk_offset = used_chunk_list->at(*order).first;
    victim_chunk_entries = used_chunk_list->at(*order).second;

    if((victim_chunk_entries == MTS_VS_ENTRIES_PER_CHUNK)) {
	ts_trace(TS_INFO, "VICTIM_CHUNK_OFFSET: %d, NO MORE VICTIM CHUNK(EVERY CHUNKS HAVE FULL OF ENTIRIES)\n", victim_chunk_offset);
	return -1;
    }
    else {
	ts_trace(TS_INFO, "[GET_VICTIM_CHUNK_OFFSET] victim_chunk_offset: %d num of valid_entries: %d\n",
		victim_chunk_offset, victim_chunk_entries);

	/* return INDEX which has minimum valid entries (>0)*/
	return victim_chunk_offset;
    }
}

void ValueStorage::add_free_chunk_list(int free_chunk_offset) {
    ts_trace(TS_INFO, "[ADD_FREE_CHUNK_LIST] CHUNK_OFFSET: %d\n", free_chunk_offset);
    free_chunk_list->push_back(free_chunk_offset);
}

int ValueStorage::get_free_chunk_offset() {
    int free_chunk_offset;

    free_chunk_offset = free_chunk_list->front();

    while(!is_empty(free_chunk_offset)) {
	free_chunk_list->erase(free_chunk_list->begin());
	free_chunk_offset = free_chunk_list->front();
    }

    if(!(free_chunk_offset < MTS_VS_CHUNK_NUM))
    {
	ts_trace(TS_ERROR, "[GET_FREE_CHUNK_OFFSET] NO MORE FREE CHUNK | VS_ID: %d FREE_CHUNK_OFFSET: %d\n", vs_id, free_chunk_offset);
	exit(EXIT_FAILURE);
    }

    free_chunk_list->erase(free_chunk_list->begin());

    assert(free_chunk_offset < MTS_VS_CHUNK_NUM);
    assert(-1 < free_chunk_offset);

    return free_chunk_offset;
}

unsigned int ValueStorage::get_used_chunk_num() {
    return ((MTS_VS_CHUNK_NUM) - free_chunk_list->size());
}

bool ValueStorage::read_gc_r_chunk(int gc_r_chunk_offset) {
    int ret;
    off64_t offset = gc_r_chunk_offset * MTS_VS_CHUNK_SIZE;
    memset(gc_r_buffer, 0x00, MTS_VS_CHUNK_SIZE);
    
    int i = 0;
    int array_num = 0;
    int submitted_io = 0;
    do {
	gc_r_sqe = io_uring_get_sqe(&gc_r_ring);
	if(!gc_r_sqe) {
	    ts_trace(TS_INFO, "[GC-READ] get set failed, will submit \n");
	    break;
	}

	io_uring_prep_read(gc_r_sqe, fd[0], (void*)(vs_entry_t *)&gc_r_buffer[array_num], MTS_VS_CHUNK_SIZE/GC_QD, offset);

	offset += MTS_VS_CHUNK_SIZE/GC_QD;
	array_num += MTS_VS_CHUNK_SIZE/GC_QD/MTS_VS_ENTRY_SIZE;
	submitted_io++;
    } while (true);

    ret = io_uring_submit(&gc_r_ring);

    if(ret != submitted_io) {
	ts_trace(TS_ERROR, "[GC-READ] io_uring_submit failed! | io_uring_submit(&gc_r_ring): %d i: %d\n", ret, i);
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "[GC-READ] io_uring_submit successed! | ret: %d i: %d\n", ret, i);
    if (ret < 0) {
	fprintf(stderr, "[GC-READ]io_uring_submit failed! | io_uring_submit: %s\n", strerror(-ret));
	exit(EXIT_FAILURE);
    }

    int pending = ret;
    ret = io_uring_wait_cqe_nr(&gc_r_ring, &gc_r_cqe, pending);

    if(ret < 0) {
	ts_trace(TS_ERROR, "io_uring_wait_cqe failed!\n");
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "io_uring_wait_cqe successed!\n");

    for(i = 0; i < pending; i++) {
	io_uring_cqe_seen(&gc_r_ring, gc_r_cqe);
    }

    ts_trace(TS_INFO, "[READ_GC_R_CHUNK] gc_r_chunk_offset: %d\n", gc_r_chunk_offset);

    return ret;
}

void ValueStorage::sort_w_buffer(w_chunk_t *chunk) {
    //vs_entry_t *w_buffer)
    /* add sort function for enhancing scan() performance
     * 1. alloc
     * 2. converting
     * 3. sorting
     * 4. converting
     * 5. copy
     */
    long unsigned int entry_num, s_entry_num;

    vs_entry_t *unsorted_buffer = chunk->w_buffer;
    vs_entry_t *sorted_buffer = chunk->s_buffer;
    std::vector<std::pair<Key_t, vs_entry_t *>> *sorted_entry_list = chunk->s_entry_list;
    sorted_entry_list->reserve(MTS_VS_ENTRIES_PER_CHUNK);

    for(entry_num = 0; entry_num < MTS_VS_ENTRIES_PER_CHUNK; entry_num++) {
	vs_entry_t *w_entry = (vs_entry *)&unsorted_buffer[entry_num];

	if(w_entry == NULL)
	    break;

	if(w_entry->at_entry == NULL)
	    break;

	Key_t key = w_entry->key;
	auto s_entry = std::make_pair(key, w_entry);
	sorted_entry_list->push_back(s_entry);
    }

    sort(sorted_entry_list->begin(), sorted_entry_list->end(), sort_by_key_write);

    ts_trace(TS_INFO, "[SORT_W_BUFFER] MTS_VS_ENTRIES_PER_CHUNK: %lu\n", MTS_VS_ENTRIES_PER_CHUNK);

    memset((void *)sorted_buffer, 0, MTS_VS_CHUNK_SIZE);

    vs_entry_t *s_entry;
    for(s_entry_num = 0; s_entry_num < entry_num; s_entry_num++) {
	if(unlikely(sorted_entry_list->size() == 0))
	    break;
	s_entry = sorted_entry_list->at(s_entry_num).second;
	memcpy((void *)&sorted_buffer[s_entry_num], (void *)s_entry, sizeof(vs_entry_t));
    }

    sorted_entry_list->clear();
    memcpy(chunk->w_buffer, sorted_buffer, MTS_VS_CHUNK_SIZE);
}

void ValueStorage::write_chunk(w_chunk_t *chunk, bool write_type) {
    int ret;

    /* sorting for improving sanning */
    off64_t offset = chunk->chunk_offset * MTS_VS_CHUNK_SIZE;
    vs_entry_t *w_buffer = chunk->w_buffer;

    sort_w_buffer(chunk);

    ts_trace(TS_INFO, "write_chunk offset %lu\n", offset);

    int i = 0;
    int array_num = 0;
    int submitted_io = 0;
    int ring_idx = chunk->id;
    struct io_uring_sqe *w_sqe;
    struct io_uring_cqe *w_cqe;

    do {
	w_sqe = io_uring_get_sqe(&w_ring[ring_idx]);
	if(!w_sqe) {
	    ts_trace(TS_INFO, "[GET_VAL] write get set failed \n");
	    break;
	}

	io_uring_prep_write(w_sqe, fd[0], (void *)(vs_entry_t *)&w_buffer[array_num], MTS_VS_CHUNK_SIZE/W_QD, offset);

	offset += MTS_VS_CHUNK_SIZE / W_QD;
	array_num += MTS_VS_CHUNK_SIZE / W_QD / MTS_VS_ENTRY_SIZE;
	submitted_io++;
	ts_trace(TS_INFO, "[GET_VAL] offset %lu array_num %d submitted_io %d\n", offset, array_num, submitted_io);
    } while (true);

    ret = io_uring_submit(&w_ring[ring_idx]);
    if(ret < 0) {
	ts_trace(TS_ERROR, "io_uring_submit failed! | io_uring_submit(&w_ring) ret %d submitted_io %d\n", ret, submitted_io);
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "io_uring_submit successed! | ret: %d i: %d\n", ret, i);

    int pending = ret;

    ret = io_uring_wait_cqe_nr(&w_ring[ring_idx], &w_cqe, pending);
    if(ret < 0) {
	ts_trace(TS_ERROR, "io_uring_wait_cqe failed!\n");
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "io_uring_wait_cqe successed!\n");


    for(i = 0; i < pending; i++) {
	io_uring_cqe_seen(&w_ring[ring_idx], w_cqe);
	ts_trace(TS_INFO, "[GET_VAL] ring_idx %d pending %d\n", ring_idx, i);
    }

#ifdef MTS_STATS_WAF
    total_vs_write_count++;
#endif
    ts_trace(TS_INFO, "[WRITE_CHUNK] vs_id: %d\n", vs_id);
}

void ValueStorage::write_gc_w_chunk(int gc_w_chunk_offset) {
    int ret;

    /* sorting for improving scan performance */
    sort_w_buffer(gc_w_chunk);
    off64_t offset = gc_w_chunk_offset * MTS_VS_CHUNK_SIZE;
    
    int i = 0;
    int array_num = 0;
    int submitted_io = 0;
    do {
	gc_w_sqe = io_uring_get_sqe(&gc_w_ring);
	if(!gc_w_sqe) {
	    ts_trace(TS_INFO, "[GC-WRITE] get set stopped, will submit \n");
	    break;
	}

	io_uring_prep_write(gc_w_sqe, fd[0], (void *)(vs_entry_t *)&gc_w_buffer[array_num], MTS_VS_CHUNK_SIZE/GC_QD, offset);

	offset += MTS_VS_CHUNK_SIZE/GC_QD;
	array_num +=  MTS_VS_CHUNK_SIZE/GC_QD/MTS_VS_ENTRY_SIZE;
	submitted_io++;
    } while (true);

    ret = io_uring_submit(&gc_w_ring);

    if(ret != submitted_io) {
	ts_trace(TS_ERROR, "[GC-WRITE] io_uring_submit failed! | io_uring_submit(&gc_w_ring): %d i: %d\n", ret, i);
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "[GC-WRITE] io_uring_submit successed! | ret: %d i: %d\n", ret, i);
    if (ret < 0) {
	fprintf(stderr, "[GC-WRITE]io_uring_submit failed! | io_uring_submit: %s\n", strerror(-ret));
	exit(EXIT_FAILURE);
    }

    int pending = ret;
    ret = io_uring_wait_cqe_nr(&gc_w_ring, &gc_w_cqe, pending);

    if(ret < 0) {
	ts_trace(TS_ERROR, "io_uring_wait_cqe failed!\n");
	exit(EXIT_FAILURE);
    } else ts_trace(TS_INFO, "io_uring_wait_cqe successed!\n");

    for(i = 0; i < pending; i++) {
	io_uring_cqe_seen(&gc_w_ring, gc_w_cqe);
    }
    memset(gc_w_buffer, 0x00, MTS_VS_CHUNK_SIZE);
    ts_trace(TS_INFO, "[WRITE_GC_CHUNK] w_chunk_offset: %d\n", gc_w_chunk_offset);
}

int ValueStorage::create_used_chunk_list() {
    int used_chunk_cnt = 0;
    int entry_num = 0;

    used_chunk_list->clear();

    /* create used_chunk_list from vs_bitmap_info */
    for (unsigned long int chunk_offset = 0; chunk_offset < MTS_VS_CHUNK_NUM; chunk_offset++) {
	entry_num = vs_bitmap_info->at(chunk_offset).count();
	if(entry_num > 0) {
	    auto chunk_info = std::make_pair(chunk_offset, entry_num);
	    used_chunk_list->push_back(chunk_info);

	    ts_trace(TS_INFO, "[CREATE_USED_CHUNK_LIST] CHUNK_OFFSET: %d, VALID_ENTRY: %d\n", 
		    used_chunk_list->at(used_chunk_cnt).first,
		    used_chunk_list->at(used_chunk_cnt).second);

	    used_chunk_cnt++;
	}
    }

    /* sort by num of entry */
    sort(used_chunk_list->begin(), used_chunk_list->end(), sort_by_entry);

    return used_chunk_cnt;
}

int ValueStorage::is_empty(int chunk_offset) {
     if(vs_bitmap_info->at(chunk_offset).count() == 0)
	 return true;
     else return false;
}

int ValueStorage::need_gc() {
    if(this->gc_done)
	return true;
    else return false;
}
