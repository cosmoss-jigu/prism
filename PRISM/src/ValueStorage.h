#ifndef MTS_VALUESTORAGE_H
#define MTS_VALUESTORAGE_H

#include <sys/eventfd.h>
#include <stdlib.h>
#include <bitset>
#include <vector>
#include <list>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libpmem.h>
#include <libpmemobj.h>
#include <algorithm>
#include <sys/mman.h>
#include <malloc.h>
#include <shared_mutex>
#include <cassert>
#include "liburing.h"
#include "MTSImpl.h"
#include "SpinLock.h"

typedef struct at_entry at_entry_t;
typedef struct cq_entry cq_entry_t;

typedef struct vs_entry {
    at_entry_t *at_entry;
    Key_t key;
    Val_t val;
    unsigned char __reserved[KV_SIZE - sizeof(Key_t) - sizeof(Val_t) - sizeof(at_entry_t *)]; 
} vs_entry_t;

typedef struct moved_entry {
    Key_t key;
    at_entry_t *at_entry;
    int chunk_offset;
    int entry_offset;
    OpForm::Operation op_type;
} moved_entry_t;

typedef struct victim_chunk {
    int chunk_offset;
    int entry_num;
} victim_chunk_t;

typedef struct file_info {
    off_t file_sz;
    struct iovec iovec[];
} file_info_t;

typedef struct w_chunk {
    int id;
    int chunk_offset;
    int entry_offset;
    vs_entry_t *w_buffer;
    vs_entry_t *s_buffer;
    std::vector<moved_entry_t> *moved_entry_list;
    std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list;
    std::vector<std::pair<Key_t, vs_entry_t *>> *s_entry_list;
} w_chunk_t;


typedef std::bitset<MTS_VS_ENTRIES_PER_CHUNK> vs_bitmap;

class ValueStorage {
    private:
	SpinLock spinlock;
	int vs_id;
	bool gc_done;
	std::atomic<bool> g_endVS;

	/* for get_val() */
	int r_chunk_offset;
	int r_vs_entry_offset;

	/* for write() */
	w_chunk_t *w_chunk[MTS_THREAD_NUM];
	vs_entry_t *w_buffer[MTS_THREAD_NUM];
	vs_entry_t *s_buffer[MTS_THREAD_NUM];
	std::vector<moved_entry_t> *moved_entry_list[MTS_THREAD_NUM];
	std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list[MTS_THREAD_NUM];
	std::vector<std::pair<Key_t, vs_entry_t *>> *s_entry_list[MTS_THREAD_NUM];

	/* for garbage_collection */
	w_chunk_t *gc_w_chunk;
	vs_entry_t *gc_r_buffer;
	vs_entry_t *gc_w_buffer;
	vs_entry_t *gc_s_buffer;
	std::vector<moved_entry_t> *gc_moved_entry_list;
	std::vector<std::pair<Key_t, at_entry_t *>> *s_gc_moved_entry_list;
	std::vector<std::pair<Key_t, vs_entry_t *>> *s_gc_entry_list;

	/* how many free chunks are there */
	std::vector<int> *free_chunk_list;
	/* how many used chunks are there */
	std::vector<std::pair<int, int>> *used_chunk_list;
	std::vector<vs_bitmap> *vs_bitmap_info;

	/* io_uring completion */
	std::thread finisher;
	void listener_thread();

    public:
	std::atomic<uint64_t> total_vs_write_count;
	int fd[1];
	mutable std::shared_mutex s_mutex_;
	mutable std::mutex mutex_;
	std::atomic<bool> is_writing;
	std::atomic<bool> is_recovered;

	std::atomic<int> cur_ring_idx;
	std::atomic<int> last_ring_idx;
	bool r_ring_bitmap[IO_URING_RRING_NUM];
	bool scan_r_ring_bitmap[IO_URING_RRING_NUM];

	bool cur_pending_vec_ready[IO_URING_RRING_NUM];;

	uint64_t ready_timestamp[MTS_VS_NUM][IO_URING_RRING_NUM][R_QD];
	uint64_t work_timestamp[MTS_VS_NUM][IO_URING_RRING_NUM][R_QD];

	std::atomic<int> pending_ios[IO_URING_RRING_NUM];
	bool is_working[IO_URING_RRING_NUM];
	std::vector<at_entry_t *> *src_at_entry_vec[IO_URING_RRING_NUM];
	std::vector<at_entry_t *> *dst_at_entry_vec[IO_URING_RRING_NUM];

	int s_is_working[IO_URING_SRING_NUM];

	/* io_uring */
	/* for read() */
	struct iovec r_buffer[IO_URING_RRING_NUM][R_QD];
	struct io_uring r_ring[IO_URING_RRING_NUM];
	struct io_uring scan_r_ring[IO_URING_SRING_NUM];
	struct io_uring w_ring[IO_URING_WRING_NUM];

	struct io_uring gc_w_ring;
	struct io_uring_sqe *gc_w_sqe;
	struct io_uring_cqe *gc_w_cqe;

	struct io_uring gc_r_ring;
	struct io_uring_sqe *gc_r_sqe;
	struct io_uring_cqe *gc_r_cqe;

	ValueStorage();
	ValueStorage(const char *path);
	ValueStorage(const char *path, int vs_id);
	~ValueStorage();

	uint32_t get_vs_id();
	void init_free_chunk_list();
	void init_used_chunk_list();
	void init_vs_bitmap_info();
	void set_vs_bitmap_info(int chunk_offset, int vs_entry_offset);
	void clear_vs_bitmap_info(int chunk_offset, int vs_entry_offset);
	int is_empty(int chunk_offset);
	int need_gc();

	/* write() */
	vs_entry_t alloc(Key_t key, Val_t val, at_entry_t *at_entry);
	void put_vs_entry(int oplog_id, Key_t key, Val_t val, at_entry_t *at_entry);
	void forced_write_chunk(int oplog_id);
	void add_moved_entry_list(std::vector<moved_entry_t> *moved_entry_list, int chunk_offset, int entry_offset, vs_entry_t *vs_entry, OpForm::Operation op_type);
	void sync_with_at(std::vector<moved_entry_t> *moved_entry_list, std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list);

	/* read() */
	Val_t get_val(at_entry_t *at_entry);
	int get_val_ccsync(std::vector<at_entry_t *> *vec, int ring_idx);
	int get_val_ccsync(at_entry_t *at_entry, int ring_idx);


	/* scan() */
	int get_val_scan(std::vector<at_entry_t *> *cur_at_entry, int ring_idx);

	/* caching */
	cq_entry_t *make_cq_entry(at_entry_t *at_entry, Val_t val);

	void init_w_chunk(int oplog_id);
	void write_chunk(w_chunk_t *chunk, bool write_type);

	void link_to_at(int chunk_idx, int entry_idx, at_entry_t *at_entry);
	void unlink_to_at(int chunk_idx, int entry_idx, at_entry_t *at_entry);
	void gc_link_to_at(int chunk_idx, int entry_idx, at_entry_t *at_entry);

	void sort_w_buffer(w_chunk_t *w_chunk);
	void *sort_moved_entry(std::vector<moved_entry_t> *moved_entry_list, std::vector<std::pair<Key_t, at_entry_t *>> *s_moved_entry_list);

	/* Garbage Collection */
	bool not_enough_free_chunk();
	int get_free_chunk_offset();
	unsigned int get_used_chunk_num();
	bool garbage_collection();
	void init_gc_w_chunk();
	void add_free_chunk_list(int free_chunk_offset);
	void write_gc_w_chunk(int gc_w_chunk_offset);
	bool read_gc_r_chunk(int gc_r_offset);
	int get_victim_chunk_offset(unsigned int *chunk_read_cnt);
	void gc_sync_with_at(std::vector<moved_entry_t> *gc_moved_entry_list, std::vector<std::pair<Key_t, at_entry_t *>> *s_gc_moved_entry_list);

	int create_used_chunk_list();
	void check_all_chunks();
};

#endif /* MTS_VALUESTORAGE_H */
