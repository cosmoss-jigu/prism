
#include <iostream>
#include "indexkey.h"
#include "./PRISM/include/MTS.h"
#include "BwTree/bwtree.h"
#include "masstree/mtIndexAPI.hh"


#ifndef _INDEX_H
#define _INDEX_H

using namespace wangziqi2013;
using namespace bwtree;

template<typename KeyType, class KeyComparator>
class Index
{
 public:
  virtual bool insert(KeyType key, uint64_t value, threadinfo *ti) = 0;

  virtual uint64_t find(KeyType key, std::vector<uint64_t> *v, threadinfo *ti) = 0;

  virtual uint64_t find_bwtree_fast(KeyType key, std::vector<uint64_t> *v) {};

  // Used for bwtree only
  virtual bool insert_bwtree_fast(KeyType key, uint64_t value) {};

  virtual bool upsert(KeyType key, uint64_t value, threadinfo *ti) = 0;

  virtual uint64_t scan(KeyType key, int range, threadinfo *ti) = 0;

  virtual bool recover(size_t start_index, threadinfo *ti) = 0;

  virtual int64_t getMemory() const = 0;

  // This initializes the thread pool
  virtual void UpdateThreadLocal(size_t thread_num) = 0;
  virtual void AssignGCID(size_t thread_id) = 0;
  virtual void UnregisterThread(size_t thread_id) = 0;
  
  // After insert phase perform this action
  // By default it is empty
  // This will be called in the main thread
  virtual void AfterLoadCallback() {}
  
  // This is called after threads finish but before the thread local are
  // destroied by the thread manager
  virtual void CollectStatisticalCounter(int) {}
  virtual size_t GetIndexSize() { return 0UL; }

  // Destructor must also be virtual
  virtual ~Index() {}
};

template<typename KeyType, class KeyComparator>
class MTSIndex : public Index<KeyType, KeyComparator>
{
 public:

  ~MTSIndex() {
          //idx.~MTS();
  }

  void UpdateThreadLocal(size_t thread_num) {}
  //void AssignGCID(size_t thread_id) {}
  void AssignGCID(size_t thread_id) {idx.registerThread();}
  void UnregisterThread(size_t thread_id) {idx.unregisterThread();}
  //void UnregisterThread(size_t thread_id) {}

  bool insert(KeyType key, uint64_t value, threadinfo *ti) {
    idx.insert(key, value);
    return true;
  }

  uint64_t find(KeyType key, std::vector<uint64_t> *v, threadinfo *ti) {
    uint64_t result;
    result = idx.lookup(key);
    v->clear();
    v->push_back(result);
    return 0;
  }

  bool upsert(KeyType key, uint64_t value, threadinfo *ti) {
    idx.update(key, value);
    return true;
  }

  uint64_t scan(KeyType key, int range, threadinfo *ti) {
    std::vector<KeyType> result;
    uint64_t size = idx.scan(key, range, result);
    //if(range != size) printf("%d %d\n", range, size);
    return size;
  }

  bool recover(KeyType key, threadinfo *ti) {
      idx.recover(key);
      return true;
  }

  int64_t getMemory() const {
    return 0;
  }

  void merge() {}

  MTSIndex(uint64_t kt) : idx(2){

  }


 private:
 MTS idx;

};

#endif

