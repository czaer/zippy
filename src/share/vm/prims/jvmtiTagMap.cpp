/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "jvmtifiles/jvmtiEnv.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline2.hpp"
#include "prims/jvmtiEventController.hpp"
#include "prims/jvmtiEventController.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiImpl.hpp"
#include "prims/jvmtiTagMap.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/reflectionUtils.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vmThread.hpp"
#include "runtime/vm_operations.hpp"
#include "services/serviceUtil.hpp"
#ifndef SERIALGC
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#endif

// JvmtiTagHashmapEntry
//
// Each entry encapsulates a reference to the tagged object
// and the tag value. In addition an entry includes a next pointer which
// is used to chain entries together.

class JvmtiTagHashmapEntry : public CHeapObj {
 private:
  friend class JvmtiTagMap;

  oop _object;                          // tagged object
  jlong _tag;                           // the tag
  JvmtiTagHashmapEntry* _next;          // next on the list

  inline void init(oop object, jlong tag) {
    _object = object;
    _tag = tag;
    _next = NULL;
  }

  // constructor
  JvmtiTagHashmapEntry(oop object, jlong tag)         { init(object, tag); }

 public:

  // accessor methods
  inline oop object() const                           { return _object; }
  inline oop* object_addr()                           { return &_object; }
  inline jlong tag() const                            { return _tag; }

  inline void set_tag(jlong tag) {
    assert(tag != 0, "can't be zero");
    _tag = tag;
  }

  inline JvmtiTagHashmapEntry* next() const             { return _next; }
  inline void set_next(JvmtiTagHashmapEntry* next)      { _next = next; }
};


// JvmtiTagHashmap
//
// A hashmap is essentially a table of pointers to entries. Entries
// are hashed to a location, or position in the table, and then
// chained from that location. The "key" for hashing is address of
// the object, or oop. The "value" is the tag value.
//
// A hashmap maintains a count of the number entries in the hashmap
// and resizes if the number of entries exceeds a given threshold.
// The threshold is specified as a percentage of the size - for
// example a threshold of 0.75 will trigger the hashmap to resize
// if the number of entries is >75% of table size.
//
// A hashmap provides functions for adding, removing, and finding
// entries. It also provides a function to iterate over all entries
// in the hashmap.

class JvmtiTagHashmap : public CHeapObj {
 private:
  friend class JvmtiTagMap;

  enum {
    small_trace_threshold  = 10000,                  // threshold for tracing
    medium_trace_threshold = 100000,
    large_trace_threshold  = 1000000,
    initial_trace_threshold = small_trace_threshold
  };

  static int _sizes[];                  // array of possible hashmap sizes
  int _size;                            // actual size of the table
  int _size_index;                      // index into size table

  int _entry_count;                     // number of entries in the hashmap

  float _load_factor;                   // load factor as a % of the size
  int _resize_threshold;                // computed threshold to trigger resizing.
  bool _resizing_enabled;               // indicates if hashmap can resize

  int _trace_threshold;                 // threshold for trace messages

  JvmtiTagHashmapEntry** _table;        // the table of entries.

  // private accessors
  int resize_threshold() const                  { return _resize_threshold; }
  int trace_threshold() const                   { return _trace_threshold; }

  // initialize the hashmap
  void init(int size_index=0, float load_factor=4.0f) {
    int initial_size =  _sizes[size_index];
    _size_index = size_index;
    _size = initial_size;
    _entry_count = 0;
    if (TraceJVMTIObjectTagging) {
      _trace_threshold = initial_trace_threshold;
    } else {
      _trace_threshold = -1;
    }
    _load_factor = load_factor;
    _resize_threshold = (int)(_load_factor * _size);
    _resizing_enabled = true;
    size_t s = initial_size * sizeof(JvmtiTagHashmapEntry*);
    _table = (JvmtiTagHashmapEntry**)os::malloc(s);
    if (_table == NULL) {
      vm_exit_out_of_memory(s, "unable to allocate initial hashtable for jvmti object tags");
    }
    for (int i=0; i<initial_size; i++) {
      _table[i] = NULL;
    }
  }

  // hash a given key (oop) with the specified size
  static unsigned int hash(oop key, int size) {
    // shift right to get better distribution (as these bits will be zero
    // with aligned addresses)
    unsigned int addr = (unsigned int)((intptr_t)key);
#ifdef _LP64
    return (addr >> 3) % size;
#else
    return (addr >> 2) % size;
#endif
  }

  // hash a given key (oop)
  unsigned int hash(oop key) {
    return hash(key, _size);
  }

  // resize the hashmap - allocates a large table and re-hashes
  // all entries into the new table.
  void resize() {
    int new_size_index = _size_index+1;
    int new_size = _sizes[new_size_index];
    if (new_size < 0) {
      // hashmap already at maximum capacity
      return;
    }

    // allocate new table
    size_t s = new_size * sizeof(JvmtiTagHashmapEntry*);
    JvmtiTagHashmapEntry** new_table = (JvmtiTagHashmapEntry**)os::malloc(s);
    if (new_table == NULL) {
      warning("unable to allocate larger hashtable for jvmti object tags");
      set_resizing_enabled(false);
      return;
    }

    // initialize new table
    int i;
    for (i=0; i<new_size; i++) {
      new_table[i] = NULL;
    }

    // rehash all entries into the new table
    for (i=0; i<_size; i++) {
      JvmtiTagHashmapEntry* entry = _table[i];
      while (entry != NULL) {
        JvmtiTagHashmapEntry* next = entry->next();
        oop key = entry->object();
        assert(key != NULL, "jni weak reference cleared!!");
        unsigned int h = hash(key, new_size);
        JvmtiTagHashmapEntry* anchor = new_table[h];
        if (anchor == NULL) {
          new_table[h] = entry;
          entry->set_next(NULL);
        } else {
          entry->set_next(anchor);
          new_table[h] = entry;
        }
        entry = next;
      }
    }

    // free old table and update settings.
    os::free((void*)_table);
    _table = new_table;
    _size_index = new_size_index;
    _size = new_size;

    // compute new resize threshold
    _resize_threshold = (int)(_load_factor * _size);
  }


  // internal remove function - remove an entry at a given position in the
  // table.
  inline void remove(JvmtiTagHashmapEntry* prev, int pos, JvmtiTagHashmapEntry* entry) {
    assert(pos >= 0 && pos < _size, "out of range");
    if (prev == NULL) {
      _table[pos] = entry->next();
    } else {
      prev->set_next(entry->next());
    }
    assert(_entry_count > 0, "checking");
    _entry_count--;
  }

  // resizing switch
  bool is_resizing_enabled() const          { return _resizing_enabled; }
  void set_resizing_enabled(bool enable)    { _resizing_enabled = enable; }

  // debugging
  void print_memory_usage();
  void compute_next_trace_threshold();

 public:

  // create a JvmtiTagHashmap of a preferred size and optionally a load factor.
  // The preferred size is rounded down to an actual size.
  JvmtiTagHashmap(int size, float load_factor=0.0f) {
    int i=0;
    while (_sizes[i] < size) {
      if (_sizes[i] < 0) {
        assert(i > 0, "sanity check");
        i--;
        break;
      }
      i++;
    }

    // if a load factor is specified then use it, otherwise use default
    if (load_factor > 0.01f) {
      init(i, load_factor);
    } else {
      init(i);
    }
  }

  // create a JvmtiTagHashmap with default settings
  JvmtiTagHashmap() {
    init();
  }

  // release table when JvmtiTagHashmap destroyed
  ~JvmtiTagHashmap() {
    if (_table != NULL) {
      os::free((void*)_table);
      _table = NULL;
    }
  }

  // accessors
  int size() const                              { return _size; }
  JvmtiTagHashmapEntry** table() const          { return _table; }
  int entry_count() const                       { return _entry_count; }

  // find an entry in the hashmap, returns NULL if not found.
  inline JvmtiTagHashmapEntry* find(oop key) {
    unsigned int h = hash(key);
    JvmtiTagHashmapEntry* entry = _table[h];
    while (entry != NULL) {
      if (entry->object() == key) {
         return entry;
      }
      entry = entry->next();
    }
    return NULL;
  }


  // add a new entry to hashmap
  inline void add(oop key, JvmtiTagHashmapEntry* entry) {
    assert(key != NULL, "checking");
    assert(find(key) == NULL, "duplicate detected");
    unsigned int h = hash(key);
    JvmtiTagHashmapEntry* anchor = _table[h];
    if (anchor == NULL) {
      _table[h] = entry;
      entry->set_next(NULL);
    } else {
      entry->set_next(anchor);
      _table[h] = entry;
    }

    _entry_count++;
    if (trace_threshold() > 0 && entry_count() >= trace_threshold()) {
      assert(TraceJVMTIObjectTagging, "should only get here when tracing");
      print_memory_usage();
      compute_next_trace_threshold();
    }

    // if the number of entries exceed the threshold then resize
    if (entry_count() > resize_threshold() && is_resizing_enabled()) {
      resize();
    }
  }

  // remove an entry with the given key.
  inline JvmtiTagHashmapEntry* remove(oop key) {
    unsigned int h = hash(key);
    JvmtiTagHashmapEntry* entry = _table[h];
    JvmtiTagHashmapEntry* prev = NULL;
    while (entry != NULL) {
      if (key == entry->object()) {
        break;
      }
      prev = entry;
      entry = entry->next();
    }
    if (entry != NULL) {
      remove(prev, h, entry);
    }
    return entry;
  }

  // iterate over all entries in the hashmap
  void entry_iterate(JvmtiTagHashmapEntryClosure* closure);
};

// possible hashmap sizes - odd primes that roughly double in size.
// To avoid excessive resizing the odd primes from 4801-76831 and
// 76831-307261 have been removed. The list must be terminated by -1.
int JvmtiTagHashmap::_sizes[] =  { 4801, 76831, 307261, 614563, 1228891,
    2457733, 4915219, 9830479, 19660831, 39321619, 78643219, -1 };


// A supporting class for iterating over all entries in Hashmap
class JvmtiTagHashmapEntryClosure {
 public:
  virtual void do_entry(JvmtiTagHashmapEntry* entry) = 0;
};


// iterate over all entries in the hashmap
void JvmtiTagHashmap::entry_iterate(JvmtiTagHashmapEntryClosure* closure) {
  for (int i=0; i<_size; i++) {
    JvmtiTagHashmapEntry* entry = _table[i];
    JvmtiTagHashmapEntry* prev = NULL;
    while (entry != NULL) {
      // obtain the next entry before invoking do_entry - this is
      // necessary because do_entry may remove the entry from the
      // hashmap.
      JvmtiTagHashmapEntry* next = entry->next();
      closure->do_entry(entry);
      entry = next;
     }
  }
}

// debugging
void JvmtiTagHashmap::print_memory_usage() {
  intptr_t p = (intptr_t)this;
  tty->print("[JvmtiTagHashmap @ " INTPTR_FORMAT, p);

  // table + entries in KB
  int hashmap_usage = (size()*sizeof(JvmtiTagHashmapEntry*) +
    entry_count()*sizeof(JvmtiTagHashmapEntry))/K;

  int weak_globals_usage = (int)(JNIHandles::weak_global_handle_memory_usage()/K);
  tty->print_cr(", %d entries (%d KB) <JNI weak globals: %d KB>]",
    entry_count(), hashmap_usage, weak_globals_usage);
}

// compute threshold for the next trace message
void JvmtiTagHashmap::compute_next_trace_threshold() {
  if (trace_threshold() < medium_trace_threshold) {
    _trace_threshold += small_trace_threshold;
  } else {
    if (trace_threshold() < large_trace_threshold) {
      _trace_threshold += medium_trace_threshold;
    } else {
      _trace_threshold += large_trace_threshold;
    }
  }
}

// create a JvmtiTagMap
JvmtiTagMap::JvmtiTagMap(JvmtiEnv* env) :
  _env(env),
  _lock(Mutex::nonleaf+2, "JvmtiTagMap._lock", false),
  _free_entries(NULL),
  _free_entries_count(0)
{
  assert(JvmtiThreadState_lock->is_locked(), "sanity check");
  assert(((JvmtiEnvBase *)env)->tag_map() == NULL, "tag map already exists for environment");

  _hashmap = new JvmtiTagHashmap();

  // finally add us to the environment
  ((JvmtiEnvBase *)env)->set_tag_map(this);
}


// destroy a JvmtiTagMap
JvmtiTagMap::~JvmtiTagMap() {

  // no lock acquired as we assume the enclosing environment is
  // also being destroryed.
  ((JvmtiEnvBase *)_env)->set_tag_map(NULL);

  JvmtiTagHashmapEntry** table = _hashmap->table();
  for (int j = 0; j < _hashmap->size(); j++) {
    JvmtiTagHashmapEntry* entry = table[j];
    while (entry != NULL) {
      JvmtiTagHashmapEntry* next = entry->next();
      delete entry;
      entry = next;
    }
  }

  // finally destroy the hashmap
  delete _hashmap;
  _hashmap = NULL;

  // remove any entries on the free list
  JvmtiTagHashmapEntry* entry = _free_entries;
  while (entry != NULL) {
    JvmtiTagHashmapEntry* next = entry->next();
    delete entry;
    entry = next;
  }
  _free_entries = NULL;
}

// create a hashmap entry
// - if there's an entry on the (per-environment) free list then this
// is returned. Otherwise an new entry is allocated.
JvmtiTagHashmapEntry* JvmtiTagMap::create_entry(oop ref, jlong tag) {
  assert(Thread::current()->is_VM_thread() || is_locked(), "checking");
  JvmtiTagHashmapEntry* entry;
  if (_free_entries == NULL) {
    entry = new JvmtiTagHashmapEntry(ref, tag);
  } else {
    assert(_free_entries_count > 0, "mismatched _free_entries_count");
    _free_entries_count--;
    entry = _free_entries;
    _free_entries = entry->next();
    entry->init(ref, tag);
  }
  return entry;
}

// destroy an entry by returning it to the free list
void JvmtiTagMap::destroy_entry(JvmtiTagHashmapEntry* entry) {
  assert(SafepointSynchronize::is_at_safepoint() || is_locked(), "checking");
  // limit the size of the free list
  if (_free_entries_count >= max_free_entries) {
    delete entry;
  } else {
    entry->set_next(_free_entries);
    _free_entries = entry;
    _free_entries_count++;
  }
}

// returns the tag map for the given environments. If the tag map
// doesn't exist then it is created.
JvmtiTagMap* JvmtiTagMap::tag_map_for(JvmtiEnv* env) {
  JvmtiTagMap* tag_map = ((JvmtiEnvBase*)env)->tag_map();
  if (tag_map == NULL) {
    MutexLocker mu(JvmtiThreadState_lock);
    tag_map = ((JvmtiEnvBase*)env)->tag_map();
    if (tag_map == NULL) {
      tag_map = new JvmtiTagMap(env);
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());
  }
  return tag_map;
}

// iterate over all entries in the tag map.
void JvmtiTagMap::entry_iterate(JvmtiTagHashmapEntryClosure* closure) {
  hashmap()->entry_iterate(closure);
}

// returns true if the hashmaps are empty
bool JvmtiTagMap::is_empty() {
  assert(SafepointSynchronize::is_at_safepoint() || is_locked(), "checking");
  return hashmap()->entry_count() == 0;
}


// Return the tag value for an object, or 0 if the object is
// not tagged
//
static inline jlong tag_for(JvmtiTagMap* tag_map, oop o) {
  JvmtiTagHashmapEntry* entry = tag_map->hashmap()->find(o);
  if (entry == NULL) {
    return 0;
  } else {
    return entry->tag();
  }
}

// If the object is a java.lang.Class then return the klassOop,
// otherwise return the original object
static inline oop klassOop_if_java_lang_Class(oop o) {
  if (o->klass() == SystemDictionary::Class_klass()) {
    if (!java_lang_Class::is_primitive(o)) {
      o = (oop)java_lang_Class::as_klassOop(o);
      assert(o != NULL, "class for non-primitive mirror must exist");
    }
  }
  return o;
}

// A CallbackWrapper is a support class for querying and tagging an object
// around a callback to a profiler. The constructor does pre-callback
// work to get the tag value, klass tag value, ... and the destructor
// does the post-callback work of tagging or untagging the object.
//
// {
//   CallbackWrapper wrapper(tag_map, o);
//
//   (*callback)(wrapper.klass_tag(), wrapper.obj_size(), wrapper.obj_tag_p(), ...)
//
// } // wrapper goes out of scope here which results in the destructor
//      checking to see if the object has been tagged, untagged, or the
//      tag value has changed.
//
class CallbackWrapper : public StackObj {
 private:
  JvmtiTagMap* _tag_map;
  JvmtiTagHashmap* _hashmap;
  JvmtiTagHashmapEntry* _entry;
  oop _o;
  jlong _obj_size;
  jlong _obj_tag;
  klassOop _klass;         // the object's class
  jlong _klass_tag;

 protected:
  JvmtiTagMap* tag_map() const      { return _tag_map; }

  // invoked post-callback to tag, untag, or update the tag of an object
  void inline post_callback_tag_update(oop o, JvmtiTagHashmap* hashmap,
                                       JvmtiTagHashmapEntry* entry, jlong obj_tag);
 public:
  CallbackWrapper(JvmtiTagMap* tag_map, oop o) {
    assert(Thread::current()->is_VM_thread() || tag_map->is_locked(),
           "MT unsafe or must be VM thread");

    // for Classes the klassOop is tagged
    _o = klassOop_if_java_lang_Class(o);

    // object size
    _obj_size = _o->size() * wordSize;

    // record the context
    _tag_map = tag_map;
    _hashmap = tag_map->hashmap();
    _entry = _hashmap->find(_o);

    // get object tag
    _obj_tag = (_entry == NULL) ? 0 : _entry->tag();

    // get the class and the class's tag value
    if (_o == o) {
      _klass = _o->klass();
    } else {
      // if the object represents a runtime class then use the
      // tag for java.lang.Class
      _klass = SystemDictionary::Class_klass();
    }
    _klass_tag = tag_for(tag_map, _klass);
  }

  ~CallbackWrapper() {
    post_callback_tag_update(_o, _hashmap, _entry, _obj_tag);
  }

  inline jlong* obj_tag_p()                     { return &_obj_tag; }
  inline jlong obj_size() const                 { return _obj_size; }
  inline jlong obj_tag() const                  { return _obj_tag; }
  inline klassOop klass() const                 { return _klass; }
  inline jlong klass_tag() const                { return _klass_tag; }
};



// callback post-callback to tag, untag, or update the tag of an object
void inline CallbackWrapper::post_callback_tag_update(oop o,
                                                      JvmtiTagHashmap* hashmap,
                                                      JvmtiTagHashmapEntry* entry,
                                                      jlong obj_tag) {
  if (entry == NULL) {
    if (obj_tag != 0) {
      // callback has tagged the object
      assert(Thread::current()->is_VM_thread(), "must be VMThread");
      entry = tag_map()->create_entry(o, obj_tag);
      hashmap->add(o, entry);
    }
  } else {
    // object was previously tagged - the callback may have untagged
    // the object or changed the tag value
    if (obj_tag == 0) {

      JvmtiTagHashmapEntry* entry_removed = hashmap->remove(o);
      assert(entry_removed == entry, "checking");
      tag_map()->destroy_entry(entry);

    } else {
      if (obj_tag != entry->tag()) {
         entry->set_tag(obj_tag);
      }
    }
  }
}

// An extended CallbackWrapper used when reporting an object reference
// to the agent.
//
// {
//   TwoOopCallbackWrapper wrapper(tag_map, referrer, o);
//
//   (*callback)(wrapper.klass_tag(),
//               wrapper.obj_size(),
//               wrapper.obj_tag_p()
//               wrapper.referrer_tag_p(), ...)
//
// } // wrapper goes out of scope here which results in the destructor
//      checking to see if the referrer object has been tagged, untagged,
//      or the tag value has changed.
//
class TwoOopCallbackWrapper : public CallbackWrapper {
 private:
  bool _is_reference_to_self;
  JvmtiTagHashmap* _referrer_hashmap;
  JvmtiTagHashmapEntry* _referrer_entry;
  oop _referrer;
  jlong _referrer_obj_tag;
  jlong _referrer_klass_tag;
  jlong* _referrer_tag_p;

  bool is_reference_to_self() const             { return _is_reference_to_self; }

 public:
  TwoOopCallbackWrapper(JvmtiTagMap* tag_map, oop referrer, oop o) :
    CallbackWrapper(tag_map, o)
  {
    // self reference needs to be handled in a special way
    _is_reference_to_self = (referrer == o);

    if (_is_reference_to_self) {
      _referrer_klass_tag = klass_tag();
      _referrer_tag_p = obj_tag_p();
    } else {
      // for Classes the klassOop is tagged
      _referrer = klassOop_if_java_lang_Class(referrer);
      // record the context
      _referrer_hashmap = tag_map->hashmap();
      _referrer_entry = _referrer_hashmap->find(_referrer);

      // get object tag
      _referrer_obj_tag = (_referrer_entry == NULL) ? 0 : _referrer_entry->tag();
      _referrer_tag_p = &_referrer_obj_tag;

      // get referrer class tag.
      klassOop k = (_referrer == referrer) ?  // Check if referrer is a class...
          _referrer->klass()                  // No, just get its class
         : SystemDictionary::Class_klass();   // Yes, its class is Class
      _referrer_klass_tag = tag_for(tag_map, k);
    }
  }

  ~TwoOopCallbackWrapper() {
    if (!is_reference_to_self()){
      post_callback_tag_update(_referrer,
                               _referrer_hashmap,
                               _referrer_entry,
                               _referrer_obj_tag);
    }
  }

  // address of referrer tag
  // (for a self reference this will return the same thing as obj_tag_p())
  inline jlong* referrer_tag_p()        { return _referrer_tag_p; }

  // referrer's class tag
  inline jlong referrer_klass_tag()     { return _referrer_klass_tag; }
};

// tag an object
//
// This function is performance critical. If many threads attempt to tag objects
// around the same time then it's possible that the Mutex associated with the
// tag map will be a hot lock.
void JvmtiTagMap::set_tag(jobject object, jlong tag) {
  MutexLocker ml(lock());

  // resolve the object
  oop o = JNIHandles::resolve_non_null(object);

  // for Classes we tag the klassOop
  o = klassOop_if_java_lang_Class(o);

  // see if the object is already tagged
  JvmtiTagHashmap* hashmap = _hashmap;
  JvmtiTagHashmapEntry* entry = hashmap->find(o);

  // if the object is not already tagged then we tag it
  if (entry == NULL) {
    if (tag != 0) {
      entry = create_entry(o, tag);
      hashmap->add(o, entry);
    } else {
      // no-op
    }
  } else {
    // if the object is already tagged then we either update
    // the tag (if a new tag value has been provided)
    // or remove the object if the new tag value is 0.
    if (tag == 0) {
      hashmap->remove(o);
      destroy_entry(entry);
    } else {
      entry->set_tag(tag);
    }
  }
}

// get the tag for an object
jlong JvmtiTagMap::get_tag(jobject object) {
  MutexLocker ml(lock());

  // resolve the object
  oop o = JNIHandles::resolve_non_null(object);

  // for Classes get the tag from the klassOop
  return tag_for(this, klassOop_if_java_lang_Class(o));
}


// Helper class used to describe the static or instance fields of a class.
// For each field it holds the field index (as defined by the JVMTI specification),
// the field type, and the offset.

class ClassFieldDescriptor: public CHeapObj {
 private:
  int _field_index;
  int _field_offset;
  char _field_type;
 public:
  ClassFieldDescriptor(int index, char type, int offset) :
    _field_index(index), _field_type(type), _field_offset(offset) {
  }
  int field_index()  const  { return _field_index; }
  char field_type()  const  { return _field_type; }
  int field_offset() const  { return _field_offset; }
};

class ClassFieldMap: public CHeapObj {
 private:
  enum {
    initial_field_count = 5
  };

  // list of field descriptors
  GrowableArray<ClassFieldDescriptor*>* _fields;

  // constructor
  ClassFieldMap();

  // add a field
  void add(int index, char type, int offset);

  // returns the field count for the given class
  static int compute_field_count(instanceKlassHandle ikh);

 public:
  ~ClassFieldMap();

  // access
  int field_count()                     { return _fields->length(); }
  ClassFieldDescriptor* field_at(int i) { return _fields->at(i); }

  // functions to create maps of static or instance fields
  static ClassFieldMap* create_map_of_static_fields(klassOop k);
  static ClassFieldMap* create_map_of_instance_fields(oop obj);
};

ClassFieldMap::ClassFieldMap() {
  _fields = new (ResourceObj::C_HEAP) GrowableArray<ClassFieldDescriptor*>(initial_field_count, true);
}

ClassFieldMap::~ClassFieldMap() {
  for (int i=0; i<_fields->length(); i++) {
    delete _fields->at(i);
  }
  delete _fields;
}

void ClassFieldMap::add(int index, char type, int offset) {
  ClassFieldDescriptor* field = new ClassFieldDescriptor(index, type, offset);
  _fields->append(field);
}

// Returns a heap allocated ClassFieldMap to describe the static fields
// of the given class.
//
ClassFieldMap* ClassFieldMap::create_map_of_static_fields(klassOop k) {
  HandleMark hm;
  instanceKlassHandle ikh = instanceKlassHandle(Thread::current(), k);

  // create the field map
  ClassFieldMap* field_map = new ClassFieldMap();

  FilteredFieldStream f(ikh, false, false);
  int max_field_index = f.field_count()-1;

  int index = 0;
  for (FilteredFieldStream fld(ikh, true, true); !fld.eos(); fld.next(), index++) {
    // ignore instance fields
    if (!fld.access_flags().is_static()) {
      continue;
    }
    field_map->add(max_field_index - index, fld.signature()->byte_at(0), fld.offset());
  }
  return field_map;
}

// Returns a heap allocated ClassFieldMap to describe the instance fields
// of the given class. All instance fields are included (this means public
// and private fields declared in superclasses and superinterfaces too).
//
ClassFieldMap* ClassFieldMap::create_map_of_instance_fields(oop obj) {
  HandleMark hm;
  instanceKlassHandle ikh = instanceKlassHandle(Thread::current(), obj->klass());

  // create the field map
  ClassFieldMap* field_map = new ClassFieldMap();

  FilteredFieldStream f(ikh, false, false);

  int max_field_index = f.field_count()-1;

  int index = 0;
  for (FilteredFieldStream fld(ikh, false, false); !fld.eos(); fld.next(), index++) {
    // ignore static fields
    if (fld.access_flags().is_static()) {
      continue;
    }
    field_map->add(max_field_index - index, fld.signature()->byte_at(0), fld.offset());
  }

  return field_map;
}

// Helper class used to cache a ClassFileMap for the instance fields of
// a cache. A JvmtiCachedClassFieldMap can be cached by an instanceKlass during
// heap iteration and avoid creating a field map for each object in the heap
// (only need to create the map when the first instance of a class is encountered).
//
class JvmtiCachedClassFieldMap : public CHeapObj {
 private:
   enum {
     initial_class_count = 200
   };
  ClassFieldMap* _field_map;

  ClassFieldMap* field_map() const          { return _field_map; }

  JvmtiCachedClassFieldMap(ClassFieldMap* field_map);
  ~JvmtiCachedClassFieldMap();

  static GrowableArray<instanceKlass*>* _class_list;
  static void add_to_class_list(instanceKlass* ik);

 public:
  // returns the field map for a given object (returning map cached
  // by instanceKlass if possible
  static ClassFieldMap* get_map_of_instance_fields(oop obj);

  // removes the field map from all instanceKlasses - should be
  // called before VM operation completes
  static void clear_cache();

  // returns the number of ClassFieldMap cached by instanceKlasses
  static int cached_field_map_count();
};

GrowableArray<instanceKlass*>* JvmtiCachedClassFieldMap::_class_list;

JvmtiCachedClassFieldMap::JvmtiCachedClassFieldMap(ClassFieldMap* field_map) {
  _field_map = field_map;
}

JvmtiCachedClassFieldMap::~JvmtiCachedClassFieldMap() {
  if (_field_map != NULL) {
    delete _field_map;
  }
}

// Marker class to ensure that the class file map cache is only used in a defined
// scope.
class ClassFieldMapCacheMark : public StackObj {
 private:
   static bool _is_active;
 public:
   ClassFieldMapCacheMark() {
     assert(Thread::current()->is_VM_thread(), "must be VMThread");
     assert(JvmtiCachedClassFieldMap::cached_field_map_count() == 0, "cache not empty");
     assert(!_is_active, "ClassFieldMapCacheMark cannot be nested");
     _is_active = true;
   }
   ~ClassFieldMapCacheMark() {
     JvmtiCachedClassFieldMap::clear_cache();
     _is_active = false;
   }
   static bool is_active() { return _is_active; }
};

bool ClassFieldMapCacheMark::_is_active;


// record that the given instanceKlass is caching a field map
void JvmtiCachedClassFieldMap::add_to_class_list(instanceKlass* ik) {
  if (_class_list == NULL) {
    _class_list = new (ResourceObj::C_HEAP) GrowableArray<instanceKlass*>(initial_class_count, true);
  }
  _class_list->push(ik);
}

// returns the instance field map for the given object
// (returns field map cached by the instanceKlass if possible)
ClassFieldMap* JvmtiCachedClassFieldMap::get_map_of_instance_fields(oop obj) {
  assert(Thread::current()->is_VM_thread(), "must be VMThread");
  assert(ClassFieldMapCacheMark::is_active(), "ClassFieldMapCacheMark not active");

  klassOop k = obj->klass();
  instanceKlass* ik = instanceKlass::cast(k);

  // return cached map if possible
  JvmtiCachedClassFieldMap* cached_map = ik->jvmti_cached_class_field_map();
  if (cached_map != NULL) {
    assert(cached_map->field_map() != NULL, "missing field list");
    return cached_map->field_map();
  } else {
    ClassFieldMap* field_map = ClassFieldMap::create_map_of_instance_fields(obj);
    cached_map = new JvmtiCachedClassFieldMap(field_map);
    ik->set_jvmti_cached_class_field_map(cached_map);
    add_to_class_list(ik);
    return field_map;
  }
}

// remove the fields maps cached from all instanceKlasses
void JvmtiCachedClassFieldMap::clear_cache() {
  assert(Thread::current()->is_VM_thread(), "must be VMThread");
  if (_class_list != NULL) {
    for (int i = 0; i < _class_list->length(); i++) {
      instanceKlass* ik = _class_list->at(i);
      JvmtiCachedClassFieldMap* cached_map = ik->jvmti_cached_class_field_map();
      assert(cached_map != NULL, "should not be NULL");
      ik->set_jvmti_cached_class_field_map(NULL);
      delete cached_map;  // deletes the encapsulated field map
    }
    delete _class_list;
    _class_list = NULL;
  }
}

// returns the number of ClassFieldMap cached by instanceKlasses
int JvmtiCachedClassFieldMap::cached_field_map_count() {
  return (_class_list == NULL) ? 0 : _class_list->length();
}

// helper function to indicate if an object is filtered by its tag or class tag
static inline bool is_filtered_by_heap_filter(jlong obj_tag,
                                              jlong klass_tag,
                                              int heap_filter) {
  // apply the heap filter
  if (obj_tag != 0) {
    // filter out tagged objects
    if (heap_filter & JVMTI_HEAP_FILTER_TAGGED) return true;
  } else {
    // filter out untagged objects
    if (heap_filter & JVMTI_HEAP_FILTER_UNTAGGED) return true;
  }
  if (klass_tag != 0) {
    // filter out objects with tagged classes
    if (heap_filter & JVMTI_HEAP_FILTER_CLASS_TAGGED) return true;
  } else {
    // filter out objects with untagged classes.
    if (heap_filter & JVMTI_HEAP_FILTER_CLASS_UNTAGGED) return true;
  }
  return false;
}

// helper function to indicate if an object is filtered by a klass filter
static inline bool is_filtered_by_klass_filter(oop obj, KlassHandle klass_filter) {
  if (!klass_filter.is_null()) {
    if (obj->klass() != klass_filter()) {
      return true;
    }
  }
  return false;
}

// helper function to tell if a field is a primitive field or not
static inline bool is_primitive_field_type(char type) {
  return (type != 'L' && type != '[');
}

// helper function to copy the value from location addr to jvalue.
static inline void copy_to_jvalue(jvalue *v, address addr, jvmtiPrimitiveType value_type) {
  switch (value_type) {
    case JVMTI_PRIMITIVE_TYPE_BOOLEAN : { v->z = *(jboolean*)addr; break; }
    case JVMTI_PRIMITIVE_TYPE_BYTE    : { v->b = *(jbyte*)addr;    break; }
    case JVMTI_PRIMITIVE_TYPE_CHAR    : { v->c = *(jchar*)addr;    break; }
    case JVMTI_PRIMITIVE_TYPE_SHORT   : { v->s = *(jshort*)addr;   break; }
    case JVMTI_PRIMITIVE_TYPE_INT     : { v->i = *(jint*)addr;     break; }
    case JVMTI_PRIMITIVE_TYPE_LONG    : { v->j = *(jlong*)addr;    break; }
    case JVMTI_PRIMITIVE_TYPE_FLOAT   : { v->f = *(jfloat*)addr;   break; }
    case JVMTI_PRIMITIVE_TYPE_DOUBLE  : { v->d = *(jdouble*)addr;  break; }
    default: ShouldNotReachHere();
  }
}

// helper function to invoke string primitive value callback
// returns visit control flags
static jint invoke_string_value_callback(jvmtiStringPrimitiveValueCallback cb,
                                         CallbackWrapper* wrapper,
                                         oop str,
                                         void* user_data)
{
  assert(str->klass() == SystemDictionary::String_klass(), "not a string");

  // get the string value and length
  // (string value may be offset from the base)
  int s_len = java_lang_String::length(str);
  typeArrayOop s_value = java_lang_String::value(str);
  int s_offset = java_lang_String::offset(str);
  jchar* value;
  if (s_len > 0) {
    value = s_value->char_at_addr(s_offset);
  } else {
    value = (jchar*) s_value->base(T_CHAR);
  }

  // invoke the callback
  return (*cb)(wrapper->klass_tag(),
               wrapper->obj_size(),
               wrapper->obj_tag_p(),
               value,
               (jint)s_len,
               user_data);
}

// helper function to invoke string primitive value callback
// returns visit control flags
static jint invoke_array_primitive_value_callback(jvmtiArrayPrimitiveValueCallback cb,
                                                  CallbackWrapper* wrapper,
                                                  oop obj,
                                                  void* user_data)
{
  assert(obj->is_typeArray(), "not a primitive array");

  // get base address of first element
  typeArrayOop array = typeArrayOop(obj);
  BasicType type = typeArrayKlass::cast(array->klass())->element_type();
  void* elements = array->base(type);

  // jvmtiPrimitiveType is defined so this mapping is always correct
  jvmtiPrimitiveType elem_type = (jvmtiPrimitiveType)type2char(type);

  return (*cb)(wrapper->klass_tag(),
               wrapper->obj_size(),
               wrapper->obj_tag_p(),
               (jint)array->length(),
               elem_type,
               elements,
               user_data);
}

// helper function to invoke the primitive field callback for all static fields
// of a given class
static jint invoke_primitive_field_callback_for_static_fields
  (CallbackWrapper* wrapper,
   oop obj,
   jvmtiPrimitiveFieldCallback cb,
   void* user_data)
{
  // for static fields only the index will be set
  static jvmtiHeapReferenceInfo reference_info = { 0 };

  assert(obj->klass() == SystemDictionary::Class_klass(), "not a class");
  if (java_lang_Class::is_primitive(obj)) {
    return 0;
  }
  klassOop k = java_lang_Class::as_klassOop(obj);
  Klass* klass = k->klass_part();

  // ignore classes for object and type arrays
  if (!klass->oop_is_instance()) {
    return 0;
  }

  // ignore classes which aren't linked yet
  instanceKlass* ik = instanceKlass::cast(k);
  if (!ik->is_linked()) {
    return 0;
  }

  // get the field map
  ClassFieldMap* field_map = ClassFieldMap::create_map_of_static_fields(k);

  // invoke the callback for each static primitive field
  for (int i=0; i<field_map->field_count(); i++) {
    ClassFieldDescriptor* field = field_map->field_at(i);

    // ignore non-primitive fields
    char type = field->field_type();
    if (!is_primitive_field_type(type)) {
      continue;
    }
    // one-to-one mapping
    jvmtiPrimitiveType value_type = (jvmtiPrimitiveType)type;

    // get offset and field value
    int offset = field->field_offset();
    address addr = (address)k + offset;
    jvalue value;
    copy_to_jvalue(&value, addr, value_type);

    // field index
    reference_info.field.index = field->field_index();

    // invoke the callback
    jint res = (*cb)(JVMTI_HEAP_REFERENCE_STATIC_FIELD,
                     &reference_info,
                     wrapper->klass_tag(),
                     wrapper->obj_tag_p(),
                     value,
                     value_type,
                     user_data);
    if (res & JVMTI_VISIT_ABORT) {
      delete field_map;
      return res;
    }
  }

  delete field_map;
  return 0;
}

// helper function to invoke the primitive field callback for all instance fields
// of a given object
static jint invoke_primitive_field_callback_for_instance_fields(
  CallbackWrapper* wrapper,
  oop obj,
  jvmtiPrimitiveFieldCallback cb,
  void* user_data)
{
  // for instance fields only the index will be set
  static jvmtiHeapReferenceInfo reference_info = { 0 };

  // get the map of the instance fields
  ClassFieldMap* fields = JvmtiCachedClassFieldMap::get_map_of_instance_fields(obj);

  // invoke the callback for each instance primitive field
  for (int i=0; i<fields->field_count(); i++) {
    ClassFieldDescriptor* field = fields->field_at(i);

    // ignore non-primitive fields
    char type = field->field_type();
    if (!is_primitive_field_type(type)) {
      continue;
    }
    // one-to-one mapping
    jvmtiPrimitiveType value_type = (jvmtiPrimitiveType)type;

    // get offset and field value
    int offset = field->field_offset();
    address addr = (address)obj + offset;
    jvalue value;
    copy_to_jvalue(&value, addr, value_type);

    // field index
    reference_info.field.index = field->field_index();

    // invoke the callback
    jint res = (*cb)(JVMTI_HEAP_REFERENCE_FIELD,
                     &reference_info,
                     wrapper->klass_tag(),
                     wrapper->obj_tag_p(),
                     value,
                     value_type,
                     user_data);
    if (res & JVMTI_VISIT_ABORT) {
      return res;
    }
  }
  return 0;
}


// VM operation to iterate over all objects in the heap (both reachable
// and unreachable)
class VM_HeapIterateOperation: public VM_Operation {
 private:
  ObjectClosure* _blk;
 public:
  VM_HeapIterateOperation(ObjectClosure* blk) { _blk = blk; }

  VMOp_Type type() const { return VMOp_HeapIterateOperation; }
  void doit() {
    // allows class files maps to be cached during iteration
    ClassFieldMapCacheMark cm;

    // make sure that heap is parsable (fills TLABs with filler objects)
    Universe::heap()->ensure_parsability(false);  // no need to retire TLABs

    // Verify heap before iteration - if the heap gets corrupted then
    // JVMTI's IterateOverHeap will crash.
    if (VerifyBeforeIteration) {
      Universe::verify();
    }

    // do the iteration
    // If this operation encounters a bad object when using CMS,
    // consider using safe_object_iterate() which avoids perm gen
    // objects that may contain bad references.
    Universe::heap()->object_iterate(_blk);

    // when sharing is enabled we must iterate over the shared spaces
    if (UseSharedSpaces) {
      GenCollectedHeap* gch = GenCollectedHeap::heap();
      CompactingPermGenGen* gen = (CompactingPermGenGen*)gch->perm_gen();
      gen->ro_space()->object_iterate(_blk);
      gen->rw_space()->object_iterate(_blk);
    }
  }

};


// An ObjectClosure used to support the deprecated IterateOverHeap and
// IterateOverInstancesOfClass functions
class IterateOverHeapObjectClosure: public ObjectClosure {
 private:
  JvmtiTagMap* _tag_map;
  KlassHandle _klass;
  jvmtiHeapObjectFilter _object_filter;
  jvmtiHeapObjectCallback _heap_object_callback;
  const void* _user_data;

  // accessors
  JvmtiTagMap* tag_map() const                    { return _tag_map; }
  jvmtiHeapObjectFilter object_filter() const     { return _object_filter; }
  jvmtiHeapObjectCallback object_callback() const { return _heap_object_callback; }
  KlassHandle klass() const                       { return _klass; }
  const void* user_data() const                   { return _user_data; }

  // indicates if iteration has been aborted
  bool _iteration_aborted;
  bool is_iteration_aborted() const               { return _iteration_aborted; }
  void set_iteration_aborted(bool aborted)        { _iteration_aborted = aborted; }

 public:
  IterateOverHeapObjectClosure(JvmtiTagMap* tag_map,
                               KlassHandle klass,
                               jvmtiHeapObjectFilter object_filter,
                               jvmtiHeapObjectCallback heap_object_callback,
                               const void* user_data) :
    _tag_map(tag_map),
    _klass(klass),
    _object_filter(object_filter),
    _heap_object_callback(heap_object_callback),
    _user_data(user_data),
    _iteration_aborted(false)
  {
  }

  void do_object(oop o);
};

// invoked for each object in the heap
void IterateOverHeapObjectClosure::do_object(oop o) {
  // check if iteration has been halted
  if (is_iteration_aborted()) return;

  // ignore any objects that aren't visible to profiler
  if (!ServiceUtil::visible_oop(o)) return;

  // instanceof check when filtering by klass
  if (!klass().is_null() && !o->is_a(klass()())) {
    return;
  }
  // prepare for the calllback
  CallbackWrapper wrapper(tag_map(), o);

  // if the object is tagged and we're only interested in untagged objects
  // then don't invoke the callback. Similiarly, if the object is untagged
  // and we're only interested in tagged objects we skip the callback.
  if (wrapper.obj_tag() != 0) {
    if (object_filter() == JVMTI_HEAP_OBJECT_UNTAGGED) return;
  } else {
    if (object_filter() == JVMTI_HEAP_OBJECT_TAGGED) return;
  }

  // invoke the agent's callback
  jvmtiIterationControl control = (*object_callback())(wrapper.klass_tag(),
                                                       wrapper.obj_size(),
                                                       wrapper.obj_tag_p(),
                                                       (void*)user_data());
  if (control == JVMTI_ITERATION_ABORT) {
    set_iteration_aborted(true);
  }
}

// An ObjectClosure used to support the IterateThroughHeap function
class IterateThroughHeapObjectClosure: public ObjectClosure {
 private:
  JvmtiTagMap* _tag_map;
  KlassHandle _klass;
  int _heap_filter;
  const jvmtiHeapCallbacks* _callbacks;
  const void* _user_data;

  // accessor functions
  JvmtiTagMap* tag_map() const                     { return _tag_map; }
  int heap_filter() const                          { return _heap_filter; }
  const jvmtiHeapCallbacks* callbacks() const      { return _callbacks; }
  KlassHandle klass() const                        { return _klass; }
  const void* user_data() const                    { return _user_data; }

  // indicates if the iteration has been aborted
  bool _iteration_aborted;
  bool is_iteration_aborted() const                { return _iteration_aborted; }

  // used to check the visit control flags. If the abort flag is set
  // then we set the iteration aborted flag so that the iteration completes
  // without processing any further objects
  bool check_flags_for_abort(jint flags) {
    bool is_abort = (flags & JVMTI_VISIT_ABORT) != 0;
    if (is_abort) {
      _iteration_aborted = true;
    }
    return is_abort;
  }

 public:
  IterateThroughHeapObjectClosure(JvmtiTagMap* tag_map,
                                  KlassHandle klass,
                                  int heap_filter,
                                  const jvmtiHeapCallbacks* heap_callbacks,
                                  const void* user_data) :
    _tag_map(tag_map),
    _klass(klass),
    _heap_filter(heap_filter),
    _callbacks(heap_callbacks),
    _user_data(user_data),
    _iteration_aborted(false)
  {
  }

  void do_object(oop o);
};

// invoked for each object in the heap
void IterateThroughHeapObjectClosure::do_object(oop obj) {
  // check if iteration has been halted
  if (is_iteration_aborted()) return;

  // ignore any objects that aren't visible to profiler
  if (!ServiceUtil::visible_oop(obj)) return;

  // apply class filter
  if (is_filtered_by_klass_filter(obj, klass())) return;

  // prepare for callback
  CallbackWrapper wrapper(tag_map(), obj);

  // check if filtered by the heap filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(), wrapper.klass_tag(), heap_filter())) {
    return;
  }

  // for arrays we need the length, otherwise -1
  bool is_array = obj->is_array();
  int len = is_array ? arrayOop(obj)->length() : -1;

  // invoke the object callback (if callback is provided)
  if (callbacks()->heap_iteration_callback != NULL) {
    jvmtiHeapIterationCallback cb = callbacks()->heap_iteration_callback;
    jint res = (*cb)(wrapper.klass_tag(),
                     wrapper.obj_size(),
                     wrapper.obj_tag_p(),
                     (jint)len,
                     (void*)user_data());
    if (check_flags_for_abort(res)) return;
  }

  // for objects and classes we report primitive fields if callback provided
  if (callbacks()->primitive_field_callback != NULL && obj->is_instance()) {
    jint res;
    jvmtiPrimitiveFieldCallback cb = callbacks()->primitive_field_callback;
    if (obj->klass() == SystemDictionary::Class_klass()) {
      res = invoke_primitive_field_callback_for_static_fields(&wrapper,
                                                                    obj,
                                                                    cb,
                                                                    (void*)user_data());
    } else {
      res = invoke_primitive_field_callback_for_instance_fields(&wrapper,
                                                                      obj,
                                                                      cb,
                                                                      (void*)user_data());
    }
    if (check_flags_for_abort(res)) return;
  }

  // string callback
  if (!is_array &&
      callbacks()->string_primitive_value_callback != NULL &&
      obj->klass() == SystemDictionary::String_klass()) {
    jint res = invoke_string_value_callback(
                callbacks()->string_primitive_value_callback,
                &wrapper,
                obj,
                (void*)user_data() );
    if (check_flags_for_abort(res)) return;
  }

  // array callback
  if (is_array &&
      callbacks()->array_primitive_value_callback != NULL &&
      obj->is_typeArray()) {
    jint res = invoke_array_primitive_value_callback(
               callbacks()->array_primitive_value_callback,
               &wrapper,
               obj,
               (void*)user_data() );
    if (check_flags_for_abort(res)) return;
  }
};


// Deprecated function to iterate over all objects in the heap
void JvmtiTagMap::iterate_over_heap(jvmtiHeapObjectFilter object_filter,
                                    KlassHandle klass,
                                    jvmtiHeapObjectCallback heap_object_callback,
                                    const void* user_data)
{
  MutexLocker ml(Heap_lock);
  IterateOverHeapObjectClosure blk(this,
                                   klass,
                                   object_filter,
                                   heap_object_callback,
                                   user_data);
  VM_HeapIterateOperation op(&blk);
  VMThread::execute(&op);
}


// Iterates over all objects in the heap
void JvmtiTagMap::iterate_through_heap(jint heap_filter,
                                       KlassHandle klass,
                                       const jvmtiHeapCallbacks* callbacks,
                                       const void* user_data)
{
  MutexLocker ml(Heap_lock);
  IterateThroughHeapObjectClosure blk(this,
                                      klass,
                                      heap_filter,
                                      callbacks,
                                      user_data);
  VM_HeapIterateOperation op(&blk);
  VMThread::execute(&op);
}

// support class for get_objects_with_tags

class TagObjectCollector : public JvmtiTagHashmapEntryClosure {
 private:
  JvmtiEnv* _env;
  jlong* _tags;
  jint _tag_count;

  GrowableArray<jobject>* _object_results;  // collected objects (JNI weak refs)
  GrowableArray<uint64_t>* _tag_results;    // collected tags

 public:
  TagObjectCollector(JvmtiEnv* env, const jlong* tags, jint tag_count) {
    _env = env;
    _tags = (jlong*)tags;
    _tag_count = tag_count;
    _object_results = new (ResourceObj::C_HEAP) GrowableArray<jobject>(1,true);
    _tag_results = new (ResourceObj::C_HEAP) GrowableArray<uint64_t>(1,true);
  }

  ~TagObjectCollector() {
    delete _object_results;
    delete _tag_results;
  }

  // for each tagged object check if the tag value matches
  // - if it matches then we create a JNI local reference to the object
  // and record the reference and tag value.
  //
  void do_entry(JvmtiTagHashmapEntry* entry) {
    for (int i=0; i<_tag_count; i++) {
      if (_tags[i] == entry->tag()) {
        oop o = entry->object();
        assert(o != NULL, "sanity check");

        // the mirror is tagged
        if (o->is_klass()) {
          klassOop k = (klassOop)o;
          o = Klass::cast(k)->java_mirror();
        }

        jobject ref = JNIHandles::make_local(JavaThread::current(), o);
        _object_results->append(ref);
        _tag_results->append((uint64_t)entry->tag());
      }
    }
  }

  // return the results from the collection
  //
  jvmtiError result(jint* count_ptr, jobject** object_result_ptr, jlong** tag_result_ptr) {
    jvmtiError error;
    int count = _object_results->length();
    assert(count >= 0, "sanity check");

    // if object_result_ptr is not NULL then allocate the result and copy
    // in the object references.
    if (object_result_ptr != NULL) {
      error = _env->Allocate(count * sizeof(jobject), (unsigned char**)object_result_ptr);
      if (error != JVMTI_ERROR_NONE) {
        return error;
      }
      for (int i=0; i<count; i++) {
        (*object_result_ptr)[i] = _object_results->at(i);
      }
    }

    // if tag_result_ptr is not NULL then allocate the result and copy
    // in the tag values.
    if (tag_result_ptr != NULL) {
      error = _env->Allocate(count * sizeof(jlong), (unsigned char**)tag_result_ptr);
      if (error != JVMTI_ERROR_NONE) {
        if (object_result_ptr != NULL) {
          _env->Deallocate((unsigned char*)object_result_ptr);
        }
        return error;
      }
      for (int i=0; i<count; i++) {
        (*tag_result_ptr)[i] = (jlong)_tag_results->at(i);
      }
    }

    *count_ptr = count;
    return JVMTI_ERROR_NONE;
  }
};

// return the list of objects with the specified tags
jvmtiError JvmtiTagMap::get_objects_with_tags(const jlong* tags,
  jint count, jint* count_ptr, jobject** object_result_ptr, jlong** tag_result_ptr) {

  TagObjectCollector collector(env(), tags, count);
  {
    // iterate over all tagged objects
    MutexLocker ml(lock());
    entry_iterate(&collector);
  }
  return collector.result(count_ptr, object_result_ptr, tag_result_ptr);
}


// ObjectMarker is used to support the marking objects when walking the
// heap.
//
// This implementation uses the existing mark bits in an object for
// marking. Objects that are marked must later have their headers restored.
// As most objects are unlocked and don't have their identity hash computed
// we don't have to save their headers. Instead we save the headers that
// are "interesting". Later when the headers are restored this implementation
// restores all headers to their initial value and then restores the few
// objects that had interesting headers.
//
// Future work: This implementation currently uses growable arrays to save
// the oop and header of interesting objects. As an optimization we could
// use the same technique as the GC and make use of the unused area
// between top() and end().
//

// An ObjectClosure used to restore the mark bits of an object
class RestoreMarksClosure : public ObjectClosure {
 public:
  void do_object(oop o) {
    if (o != NULL) {
      markOop mark = o->mark();
      if (mark->is_marked()) {
        o->init_mark();
      }
    }
  }
};

// ObjectMarker provides the mark and visited functions
class ObjectMarker : AllStatic {
 private:
  // saved headers
  static GrowableArray<oop>* _saved_oop_stack;
  static GrowableArray<markOop>* _saved_mark_stack;

 public:
  static void init();                       // initialize
  static void done();                       // clean-up

  static inline void mark(oop o);           // mark an object
  static inline bool visited(oop o);        // check if object has been visited
};

GrowableArray<oop>* ObjectMarker::_saved_oop_stack = NULL;
GrowableArray<markOop>* ObjectMarker::_saved_mark_stack = NULL;

// initialize ObjectMarker - prepares for object marking
void ObjectMarker::init() {
  assert(Thread::current()->is_VM_thread(), "must be VMThread");

  // prepare heap for iteration
  Universe::heap()->ensure_parsability(false);  // no need to retire TLABs

  // create stacks for interesting headers
  _saved_mark_stack = new (ResourceObj::C_HEAP) GrowableArray<markOop>(4000, true);
  _saved_oop_stack = new (ResourceObj::C_HEAP) GrowableArray<oop>(4000, true);

  if (UseBiasedLocking) {
    BiasedLocking::preserve_marks();
  }
}

// Object marking is done so restore object headers
void ObjectMarker::done() {
  // iterate over all objects and restore the mark bits to
  // their initial value
  RestoreMarksClosure blk;
  Universe::heap()->object_iterate(&blk);

  // When sharing is enabled we need to restore the headers of the objects
  // in the readwrite space too.
  if (UseSharedSpaces) {
    GenCollectedHeap* gch = GenCollectedHeap::heap();
    CompactingPermGenGen* gen = (CompactingPermGenGen*)gch->perm_gen();
    gen->rw_space()->object_iterate(&blk);
  }

  // now restore the interesting headers
  for (int i = 0; i < _saved_oop_stack->length(); i++) {
    oop o = _saved_oop_stack->at(i);
    markOop mark = _saved_mark_stack->at(i);
    o->set_mark(mark);
  }

  if (UseBiasedLocking) {
    BiasedLocking::restore_marks();
  }

  // free the stacks
  delete _saved_oop_stack;
  delete _saved_mark_stack;
}

// mark an object
inline void ObjectMarker::mark(oop o) {
  assert(Universe::heap()->is_in(o), "sanity check");
  assert(!o->mark()->is_marked(), "should only mark an object once");

  // object's mark word
  markOop mark = o->mark();

  if (mark->must_be_preserved(o)) {
    _saved_mark_stack->push(mark);
    _saved_oop_stack->push(o);
  }

  // mark the object
  o->set_mark(markOopDesc::prototype()->set_marked());
}

// return true if object is marked
inline bool ObjectMarker::visited(oop o) {
  return o->mark()->is_marked();
}

// Stack allocated class to help ensure that ObjectMarker is used
// correctly. Constructor initializes ObjectMarker, destructor calls
// ObjectMarker's done() function to restore object headers.
class ObjectMarkerController : public StackObj {
 public:
  ObjectMarkerController() {
    ObjectMarker::init();
  }
  ~ObjectMarkerController() {
    ObjectMarker::done();
  }
};


// helper to map a jvmtiHeapReferenceKind to an old style jvmtiHeapRootKind
// (not performance critical as only used for roots)
static jvmtiHeapRootKind toJvmtiHeapRootKind(jvmtiHeapReferenceKind kind) {
  switch (kind) {
    case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:   return JVMTI_HEAP_ROOT_JNI_GLOBAL;
    case JVMTI_HEAP_REFERENCE_SYSTEM_CLASS: return JVMTI_HEAP_ROOT_SYSTEM_CLASS;
    case JVMTI_HEAP_REFERENCE_MONITOR:      return JVMTI_HEAP_ROOT_MONITOR;
    case JVMTI_HEAP_REFERENCE_STACK_LOCAL:  return JVMTI_HEAP_ROOT_STACK_LOCAL;
    case JVMTI_HEAP_REFERENCE_JNI_LOCAL:    return JVMTI_HEAP_ROOT_JNI_LOCAL;
    case JVMTI_HEAP_REFERENCE_THREAD:       return JVMTI_HEAP_ROOT_THREAD;
    case JVMTI_HEAP_REFERENCE_OTHER:        return JVMTI_HEAP_ROOT_OTHER;
    default: ShouldNotReachHere();          return JVMTI_HEAP_ROOT_OTHER;
  }
}

// Base class for all heap walk contexts. The base class maintains a flag
// to indicate if the context is valid or not.
class HeapWalkContext VALUE_OBJ_CLASS_SPEC {
 private:
  bool _valid;
 public:
  HeapWalkContext(bool valid)                   { _valid = valid; }
  void invalidate()                             { _valid = false; }
  bool is_valid() const                         { return _valid; }
};

// A basic heap walk context for the deprecated heap walking functions.
// The context for a basic heap walk are the callbacks and fields used by
// the referrer caching scheme.
class BasicHeapWalkContext: public HeapWalkContext {
 private:
  jvmtiHeapRootCallback _heap_root_callback;
  jvmtiStackReferenceCallback _stack_ref_callback;
  jvmtiObjectReferenceCallback _object_ref_callback;

  // used for caching
  oop _last_referrer;
  jlong _last_referrer_tag;

 public:
  BasicHeapWalkContext() : HeapWalkContext(false) { }

  BasicHeapWalkContext(jvmtiHeapRootCallback heap_root_callback,
                       jvmtiStackReferenceCallback stack_ref_callback,
                       jvmtiObjectReferenceCallback object_ref_callback) :
    HeapWalkContext(true),
    _heap_root_callback(heap_root_callback),
    _stack_ref_callback(stack_ref_callback),
    _object_ref_callback(object_ref_callback),
    _last_referrer(NULL),
    _last_referrer_tag(0) {
  }

  // accessors
  jvmtiHeapRootCallback heap_root_callback() const         { return _heap_root_callback; }
  jvmtiStackReferenceCallback stack_ref_callback() const   { return _stack_ref_callback; }
  jvmtiObjectReferenceCallback object_ref_callback() const { return _object_ref_callback;  }

  oop last_referrer() const               { return _last_referrer; }
  void set_last_referrer(oop referrer)    { _last_referrer = referrer; }
  jlong last_referrer_tag() const         { return _last_referrer_tag; }
  void set_last_referrer_tag(jlong value) { _last_referrer_tag = value; }
};

// The advanced heap walk context for the FollowReferences functions.
// The context is the callbacks, and the fields used for filtering.
class AdvancedHeapWalkContext: public HeapWalkContext {
 private:
  jint _heap_filter;
  KlassHandle _klass_filter;
  const jvmtiHeapCallbacks* _heap_callbacks;

 public:
  AdvancedHeapWalkContext() : HeapWalkContext(false) { }

  AdvancedHeapWalkContext(jint heap_filter,
                           KlassHandle klass_filter,
                           const jvmtiHeapCallbacks* heap_callbacks) :
    HeapWalkContext(true),
    _heap_filter(heap_filter),
    _klass_filter(klass_filter),
    _heap_callbacks(heap_callbacks) {
  }

  // accessors
  jint heap_filter() const         { return _heap_filter; }
  KlassHandle klass_filter() const { return _klass_filter; }

  const jvmtiHeapReferenceCallback heap_reference_callback() const {
    return _heap_callbacks->heap_reference_callback;
  };
  const jvmtiPrimitiveFieldCallback primitive_field_callback() const {
    return _heap_callbacks->primitive_field_callback;
  }
  const jvmtiArrayPrimitiveValueCallback array_primitive_value_callback() const {
    return _heap_callbacks->array_primitive_value_callback;
  }
  const jvmtiStringPrimitiveValueCallback string_primitive_value_callback() const {
    return _heap_callbacks->string_primitive_value_callback;
  }
};

// The CallbackInvoker is a class with static functions that the heap walk can call
// into to invoke callbacks. It works in one of two modes. The "basic" mode is
// used for the deprecated IterateOverReachableObjects functions. The "advanced"
// mode is for the newer FollowReferences function which supports a lot of
// additional callbacks.
class CallbackInvoker : AllStatic {
 private:
  // heap walk styles
  enum { basic, advanced };
  static int _heap_walk_type;
  static bool is_basic_heap_walk()           { return _heap_walk_type == basic; }
  static bool is_advanced_heap_walk()        { return _heap_walk_type == advanced; }

  // context for basic style heap walk
  static BasicHeapWalkContext _basic_context;
  static BasicHeapWalkContext* basic_context() {
    assert(_basic_context.is_valid(), "invalid");
    return &_basic_context;
  }

  // context for advanced style heap walk
  static AdvancedHeapWalkContext _advanced_context;
  static AdvancedHeapWalkContext* advanced_context() {
    assert(_advanced_context.is_valid(), "invalid");
    return &_advanced_context;
  }

  // context needed for all heap walks
  static JvmtiTagMap* _tag_map;
  static const void* _user_data;
  static GrowableArray<oop>* _visit_stack;

  // accessors
  static JvmtiTagMap* tag_map()                        { return _tag_map; }
  static const void* user_data()                       { return _user_data; }
  static GrowableArray<oop>* visit_stack()             { return _visit_stack; }

  // if the object hasn't been visited then push it onto the visit stack
  // so that it will be visited later
  static inline bool check_for_visit(oop obj) {
    if (!ObjectMarker::visited(obj)) visit_stack()->push(obj);
    return true;
  }

  // invoke basic style callbacks
  static inline bool invoke_basic_heap_root_callback
    (jvmtiHeapRootKind root_kind, oop obj);
  static inline bool invoke_basic_stack_ref_callback
    (jvmtiHeapRootKind root_kind, jlong thread_tag, jint depth, jmethodID method,
     int slot, oop obj);
  static inline bool invoke_basic_object_reference_callback
    (jvmtiObjectReferenceKind ref_kind, oop referrer, oop referree, jint index);

  // invoke advanced style callbacks
  static inline bool invoke_advanced_heap_root_callback
    (jvmtiHeapReferenceKind ref_kind, oop obj);
  static inline bool invoke_advanced_stack_ref_callback
    (jvmtiHeapReferenceKind ref_kind, jlong thread_tag, jlong tid, int depth,
     jmethodID method, jlocation bci, jint slot, oop obj);
  static inline bool invoke_advanced_object_reference_callback
    (jvmtiHeapReferenceKind ref_kind, oop referrer, oop referree, jint index);

  // used to report the value of primitive fields
  static inline bool report_primitive_field
    (jvmtiHeapReferenceKind ref_kind, oop obj, jint index, address addr, char type);

 public:
  // initialize for basic mode
  static void initialize_for_basic_heap_walk(JvmtiTagMap* tag_map,
                                             GrowableArray<oop>* visit_stack,
                                             const void* user_data,
                                             BasicHeapWalkContext context);

  // initialize for advanced mode
  static void initialize_for_advanced_heap_walk(JvmtiTagMap* tag_map,
                                                GrowableArray<oop>* visit_stack,
                                                const void* user_data,
                                                AdvancedHeapWalkContext context);

   // functions to report roots
  static inline bool report_simple_root(jvmtiHeapReferenceKind kind, oop o);
  static inline bool report_jni_local_root(jlong thread_tag, jlong tid, jint depth,
    jmethodID m, oop o);
  static inline bool report_stack_ref_root(jlong thread_tag, jlong tid, jint depth,
    jmethodID method, jlocation bci, jint slot, oop o);

  // functions to report references
  static inline bool report_array_element_reference(oop referrer, oop referree, jint index);
  static inline bool report_class_reference(oop referrer, oop referree);
  static inline bool report_class_loader_reference(oop referrer, oop referree);
  static inline bool report_signers_reference(oop referrer, oop referree);
  static inline bool report_protection_domain_reference(oop referrer, oop referree);
  static inline bool report_superclass_reference(oop referrer, oop referree);
  static inline bool report_interface_reference(oop referrer, oop referree);
  static inline bool report_static_field_reference(oop referrer, oop referree, jint slot);
  static inline bool report_field_reference(oop referrer, oop referree, jint slot);
  static inline bool report_constant_pool_reference(oop referrer, oop referree, jint index);
  static inline bool report_primitive_array_values(oop array);
  static inline bool report_string_value(oop str);
  static inline bool report_primitive_instance_field(oop o, jint index, address value, char type);
  static inline bool report_primitive_static_field(oop o, jint index, address value, char type);
};

// statics
int CallbackInvoker::_heap_walk_type;
BasicHeapWalkContext CallbackInvoker::_basic_context;
AdvancedHeapWalkContext CallbackInvoker::_advanced_context;
JvmtiTagMap* CallbackInvoker::_tag_map;
const void* CallbackInvoker::_user_data;
GrowableArray<oop>* CallbackInvoker::_visit_stack;

// initialize for basic heap walk (IterateOverReachableObjects et al)
void CallbackInvoker::initialize_for_basic_heap_walk(JvmtiTagMap* tag_map,
                                                     GrowableArray<oop>* visit_stack,
                                                     const void* user_data,
                                                     BasicHeapWalkContext context) {
  _tag_map = tag_map;
  _visit_stack = visit_stack;
  _user_data = user_data;
  _basic_context = context;
  _advanced_context.invalidate();       // will trigger assertion if used
  _heap_walk_type = basic;
}

// initialize for advanced heap walk (FollowReferences)
void CallbackInvoker::initialize_for_advanced_heap_walk(JvmtiTagMap* tag_map,
                                                        GrowableArray<oop>* visit_stack,
                                                        const void* user_data,
                                                        AdvancedHeapWalkContext context) {
  _tag_map = tag_map;
  _visit_stack = visit_stack;
  _user_data = user_data;
  _advanced_context = context;
  _basic_context.invalidate();      // will trigger assertion if used
  _heap_walk_type = advanced;
}


// invoke basic style heap root callback
inline bool CallbackInvoker::invoke_basic_heap_root_callback(jvmtiHeapRootKind root_kind, oop obj) {
  assert(ServiceUtil::visible_oop(obj), "checking");

  // if we heap roots should be reported
  jvmtiHeapRootCallback cb = basic_context()->heap_root_callback();
  if (cb == NULL) {
    return check_for_visit(obj);
  }

  CallbackWrapper wrapper(tag_map(), obj);
  jvmtiIterationControl control = (*cb)(root_kind,
                                        wrapper.klass_tag(),
                                        wrapper.obj_size(),
                                        wrapper.obj_tag_p(),
                                        (void*)user_data());
  // push root to visit stack when following references
  if (control == JVMTI_ITERATION_CONTINUE &&
      basic_context()->object_ref_callback() != NULL) {
    visit_stack()->push(obj);
  }
  return control != JVMTI_ITERATION_ABORT;
}

// invoke basic style stack ref callback
inline bool CallbackInvoker::invoke_basic_stack_ref_callback(jvmtiHeapRootKind root_kind,
                                                             jlong thread_tag,
                                                             jint depth,
                                                             jmethodID method,
                                                             jint slot,
                                                             oop obj) {
  assert(ServiceUtil::visible_oop(obj), "checking");

  // if we stack refs should be reported
  jvmtiStackReferenceCallback cb = basic_context()->stack_ref_callback();
  if (cb == NULL) {
    return check_for_visit(obj);
  }

  CallbackWrapper wrapper(tag_map(), obj);
  jvmtiIterationControl control = (*cb)(root_kind,
                                        wrapper.klass_tag(),
                                        wrapper.obj_size(),
                                        wrapper.obj_tag_p(),
                                        thread_tag,
                                        depth,
                                        method,
                                        slot,
                                        (void*)user_data());
  // push root to visit stack when following references
  if (control == JVMTI_ITERATION_CONTINUE &&
      basic_context()->object_ref_callback() != NULL) {
    visit_stack()->push(obj);
  }
  return control != JVMTI_ITERATION_ABORT;
}

// invoke basic style object reference callback
inline bool CallbackInvoker::invoke_basic_object_reference_callback(jvmtiObjectReferenceKind ref_kind,
                                                                    oop referrer,
                                                                    oop referree,
                                                                    jint index) {

  assert(ServiceUtil::visible_oop(referrer), "checking");
  assert(ServiceUtil::visible_oop(referree), "checking");

  BasicHeapWalkContext* context = basic_context();

  // callback requires the referrer's tag. If it's the same referrer
  // as the last call then we use the cached value.
  jlong referrer_tag;
  if (referrer == context->last_referrer()) {
    referrer_tag = context->last_referrer_tag();
  } else {
    referrer_tag = tag_for(tag_map(), klassOop_if_java_lang_Class(referrer));
  }

  // do the callback
  CallbackWrapper wrapper(tag_map(), referree);
  jvmtiObjectReferenceCallback cb = context->object_ref_callback();
  jvmtiIterationControl control = (*cb)(ref_kind,
                                        wrapper.klass_tag(),
                                        wrapper.obj_size(),
                                        wrapper.obj_tag_p(),
                                        referrer_tag,
                                        index,
                                        (void*)user_data());

  // record referrer and referrer tag. For self-references record the
  // tag value from the callback as this might differ from referrer_tag.
  context->set_last_referrer(referrer);
  if (referrer == referree) {
    context->set_last_referrer_tag(*wrapper.obj_tag_p());
  } else {
    context->set_last_referrer_tag(referrer_tag);
  }

  if (control == JVMTI_ITERATION_CONTINUE) {
    return check_for_visit(referree);
  } else {
    return control != JVMTI_ITERATION_ABORT;
  }
}

// invoke advanced style heap root callback
inline bool CallbackInvoker::invoke_advanced_heap_root_callback(jvmtiHeapReferenceKind ref_kind,
                                                                oop obj) {
  assert(ServiceUtil::visible_oop(obj), "checking");

  AdvancedHeapWalkContext* context = advanced_context();

  // check that callback is provided
  jvmtiHeapReferenceCallback cb = context->heap_reference_callback();
  if (cb == NULL) {
    return check_for_visit(obj);
  }

  // apply class filter
  if (is_filtered_by_klass_filter(obj, context->klass_filter())) {
    return check_for_visit(obj);
  }

  // setup the callback wrapper
  CallbackWrapper wrapper(tag_map(), obj);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return check_for_visit(obj);
  }

  // for arrays we need the length, otherwise -1
  jint len = (jint)(obj->is_array() ? arrayOop(obj)->length() : -1);

  // invoke the callback
  jint res  = (*cb)(ref_kind,
                    NULL, // referrer info
                    wrapper.klass_tag(),
                    0,    // referrer_class_tag is 0 for heap root
                    wrapper.obj_size(),
                    wrapper.obj_tag_p(),
                    NULL, // referrer_tag_p
                    len,
                    (void*)user_data());
  if (res & JVMTI_VISIT_ABORT) {
    return false;// referrer class tag
  }
  if (res & JVMTI_VISIT_OBJECTS) {
    check_for_visit(obj);
  }
  return true;
}

// report a reference from a thread stack to an object
inline bool CallbackInvoker::invoke_advanced_stack_ref_callback(jvmtiHeapReferenceKind ref_kind,
                                                                jlong thread_tag,
                                                                jlong tid,
                                                                int depth,
                                                                jmethodID method,
                                                                jlocation bci,
                                                                jint slot,
                                                                oop obj) {
  assert(ServiceUtil::visible_oop(obj), "checking");

  AdvancedHeapWalkContext* context = advanced_context();

  // check that callback is provider
  jvmtiHeapReferenceCallback cb = context->heap_reference_callback();
  if (cb == NULL) {
    return check_for_visit(obj);
  }

  // apply class filter
  if (is_filtered_by_klass_filter(obj, context->klass_filter())) {
    return check_for_visit(obj);
  }

  // setup the callback wrapper
  CallbackWrapper wrapper(tag_map(), obj);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return check_for_visit(obj);
  }

  // setup the referrer info
  jvmtiHeapReferenceInfo reference_info;
  reference_info.stack_local.thread_tag = thread_tag;
  reference_info.stack_local.thread_id = tid;
  reference_info.stack_local.depth = depth;
  reference_info.stack_local.method = method;
  reference_info.stack_local.location = bci;
  reference_info.stack_local.slot = slot;

  // for arrays we need the length, otherwise -1
  jint len = (jint)(obj->is_array() ? arrayOop(obj)->length() : -1);

  // call into the agent
  int res = (*cb)(ref_kind,
                  &reference_info,
                  wrapper.klass_tag(),
                  0,    // referrer_class_tag is 0 for heap root (stack)
                  wrapper.obj_size(),
                  wrapper.obj_tag_p(),
                  NULL, // referrer_tag is 0 for root
                  len,
                  (void*)user_data());

  if (res & JVMTI_VISIT_ABORT) {
    return false;
  }
  if (res & JVMTI_VISIT_OBJECTS) {
    check_for_visit(obj);
  }
  return true;
}

// This mask is used to pass reference_info to a jvmtiHeapReferenceCallback
// only for ref_kinds defined by the JVM TI spec. Otherwise, NULL is passed.
#define REF_INFO_MASK  ((1 << JVMTI_HEAP_REFERENCE_FIELD)         \
                      | (1 << JVMTI_HEAP_REFERENCE_STATIC_FIELD)  \
                      | (1 << JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT) \
                      | (1 << JVMTI_HEAP_REFERENCE_CONSTANT_POOL) \
                      | (1 << JVMTI_HEAP_REFERENCE_STACK_LOCAL)   \
                      | (1 << JVMTI_HEAP_REFERENCE_JNI_LOCAL))

// invoke the object reference callback to report a reference
inline bool CallbackInvoker::invoke_advanced_object_reference_callback(jvmtiHeapReferenceKind ref_kind,
                                                                       oop referrer,
                                                                       oop obj,
                                                                       jint index)
{
  // field index is only valid field in reference_info
  static jvmtiHeapReferenceInfo reference_info = { 0 };

  assert(ServiceUtil::visible_oop(referrer), "checking");
  assert(ServiceUtil::visible_oop(obj), "checking");

  AdvancedHeapWalkContext* context = advanced_context();

  // check that callback is provider
  jvmtiHeapReferenceCallback cb = context->heap_reference_callback();
  if (cb == NULL) {
    return check_for_visit(obj);
  }

  // apply class filter
  if (is_filtered_by_klass_filter(obj, context->klass_filter())) {
    return check_for_visit(obj);
  }

  // setup the callback wrapper
  TwoOopCallbackWrapper wrapper(tag_map(), referrer, obj);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return check_for_visit(obj);
  }

  // field index is only valid field in reference_info
  reference_info.field.index = index;

  // for arrays we need the length, otherwise -1
  jint len = (jint)(obj->is_array() ? arrayOop(obj)->length() : -1);

  // invoke the callback
  int res = (*cb)(ref_kind,
                  (REF_INFO_MASK & (1 << ref_kind)) ? &reference_info : NULL,
                  wrapper.klass_tag(),
                  wrapper.referrer_klass_tag(),
                  wrapper.obj_size(),
                  wrapper.obj_tag_p(),
                  wrapper.referrer_tag_p(),
                  len,
                  (void*)user_data());

  if (res & JVMTI_VISIT_ABORT) {
    return false;
  }
  if (res & JVMTI_VISIT_OBJECTS) {
    check_for_visit(obj);
  }
  return true;
}

// report a "simple root"
inline bool CallbackInvoker::report_simple_root(jvmtiHeapReferenceKind kind, oop obj) {
  assert(kind != JVMTI_HEAP_REFERENCE_STACK_LOCAL &&
         kind != JVMTI_HEAP_REFERENCE_JNI_LOCAL, "not a simple root");
  assert(ServiceUtil::visible_oop(obj), "checking");

  if (is_basic_heap_walk()) {
    // map to old style root kind
    jvmtiHeapRootKind root_kind = toJvmtiHeapRootKind(kind);
    return invoke_basic_heap_root_callback(root_kind, obj);
  } else {
    assert(is_advanced_heap_walk(), "wrong heap walk type");
    return invoke_advanced_heap_root_callback(kind, obj);
  }
}


// invoke the primitive array values
inline bool CallbackInvoker::report_primitive_array_values(oop obj) {
  assert(obj->is_typeArray(), "not a primitive array");

  AdvancedHeapWalkContext* context = advanced_context();
  assert(context->array_primitive_value_callback() != NULL, "no callback");

  // apply class filter
  if (is_filtered_by_klass_filter(obj, context->klass_filter())) {
    return true;
  }

  CallbackWrapper wrapper(tag_map(), obj);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return true;
  }

  // invoke the callback
  int res = invoke_array_primitive_value_callback(context->array_primitive_value_callback(),
                                                  &wrapper,
                                                  obj,
                                                  (void*)user_data());
  return (!(res & JVMTI_VISIT_ABORT));
}

// invoke the string value callback
inline bool CallbackInvoker::report_string_value(oop str) {
  assert(str->klass() == SystemDictionary::String_klass(), "not a string");

  AdvancedHeapWalkContext* context = advanced_context();
  assert(context->string_primitive_value_callback() != NULL, "no callback");

  // apply class filter
  if (is_filtered_by_klass_filter(str, context->klass_filter())) {
    return true;
  }

  CallbackWrapper wrapper(tag_map(), str);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return true;
  }

  // invoke the callback
  int res = invoke_string_value_callback(context->string_primitive_value_callback(),
                                         &wrapper,
                                         str,
                                         (void*)user_data());
  return (!(res & JVMTI_VISIT_ABORT));
}

// invoke the primitive field callback
inline bool CallbackInvoker::report_primitive_field(jvmtiHeapReferenceKind ref_kind,
                                                    oop obj,
                                                    jint index,
                                                    address addr,
                                                    char type)
{
  // for primitive fields only the index will be set
  static jvmtiHeapReferenceInfo reference_info = { 0 };

  AdvancedHeapWalkContext* context = advanced_context();
  assert(context->primitive_field_callback() != NULL, "no callback");

  // apply class filter
  if (is_filtered_by_klass_filter(obj, context->klass_filter())) {
    return true;
  }

  CallbackWrapper wrapper(tag_map(), obj);

  // apply tag filter
  if (is_filtered_by_heap_filter(wrapper.obj_tag(),
                                 wrapper.klass_tag(),
                                 context->heap_filter())) {
    return true;
  }

  // the field index in the referrer
  reference_info.field.index = index;

  // map the type
  jvmtiPrimitiveType value_type = (jvmtiPrimitiveType)type;

  // setup the jvalue
  jvalue value;
  copy_to_jvalue(&value, addr, value_type);

  jvmtiPrimitiveFieldCallback cb = context->primitive_field_callback();
  int res = (*cb)(ref_kind,
                  &reference_info,
                  wrapper.klass_tag(),
                  wrapper.obj_tag_p(),
                  value,
                  value_type,
                  (void*)user_data());
  return (!(res & JVMTI_VISIT_ABORT));
}


// instance field
inline bool CallbackInvoker::report_primitive_instance_field(oop obj,
                                                             jint index,
                                                             address value,
                                                             char type) {
  return report_primitive_field(JVMTI_HEAP_REFERENCE_FIELD,
                                obj,
                                index,
                                value,
                                type);
}

// static field
inline bool CallbackInvoker::report_primitive_static_field(oop obj,
                                                           jint index,
                                                           address value,
                                                           char type) {
  return report_primitive_field(JVMTI_HEAP_REFERENCE_STATIC_FIELD,
                                obj,
                                index,
                                value,
                                type);
}

// report a JNI local (root object) to the profiler
inline bool CallbackInvoker::report_jni_local_root(jlong thread_tag, jlong tid, jint depth, jmethodID m, oop obj) {
  if (is_basic_heap_walk()) {
    return invoke_basic_stack_ref_callback(JVMTI_HEAP_ROOT_JNI_LOCAL,
                                           thread_tag,
                                           depth,
                                           m,
                                           -1,
                                           obj);
  } else {
    return invoke_advanced_stack_ref_callback(JVMTI_HEAP_REFERENCE_JNI_LOCAL,
                                              thread_tag, tid,
                                              depth,
                                              m,
                                              (jlocation)-1,
                                              -1,
                                              obj);
  }
}


// report a local (stack reference, root object)
inline bool CallbackInvoker::report_stack_ref_root(jlong thread_tag,
                                                   jlong tid,
                                                   jint depth,
                                                   jmethodID method,
                                                   jlocation bci,
                                                   jint slot,
                                                   oop obj) {
  if (is_basic_heap_walk()) {
    return invoke_basic_stack_ref_callback(JVMTI_HEAP_ROOT_STACK_LOCAL,
                                           thread_tag,
                                           depth,
                                           method,
                                           slot,
                                           obj);
  } else {
    return invoke_advanced_stack_ref_callback(JVMTI_HEAP_REFERENCE_STACK_LOCAL,
                                              thread_tag,
                                              tid,
                                              depth,
                                              method,
                                              bci,
                                              slot,
                                              obj);
  }
}

// report an object referencing a class.
inline bool CallbackInvoker::report_class_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_CLASS, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_CLASS, referrer, referree, -1);
  }
}

// report a class referencing its class loader.
inline bool CallbackInvoker::report_class_loader_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_CLASS_LOADER, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_CLASS_LOADER, referrer, referree, -1);
  }
}

// report a class referencing its signers.
inline bool CallbackInvoker::report_signers_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_SIGNERS, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_SIGNERS, referrer, referree, -1);
  }
}

// report a class referencing its protection domain..
inline bool CallbackInvoker::report_protection_domain_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_PROTECTION_DOMAIN, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_PROTECTION_DOMAIN, referrer, referree, -1);
  }
}

// report a class referencing its superclass.
inline bool CallbackInvoker::report_superclass_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    // Send this to be consistent with past implementation
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_CLASS, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_SUPERCLASS, referrer, referree, -1);
  }
}

// report a class referencing one of its interfaces.
inline bool CallbackInvoker::report_interface_reference(oop referrer, oop referree) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_INTERFACE, referrer, referree, -1);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_INTERFACE, referrer, referree, -1);
  }
}

// report a class referencing one of its static fields.
inline bool CallbackInvoker::report_static_field_reference(oop referrer, oop referree, jint slot) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_STATIC_FIELD, referrer, referree, slot);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_STATIC_FIELD, referrer, referree, slot);
  }
}

// report an array referencing an element object
inline bool CallbackInvoker::report_array_element_reference(oop referrer, oop referree, jint index) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_ARRAY_ELEMENT, referrer, referree, index);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT, referrer, referree, index);
  }
}

// report an object referencing an instance field object
inline bool CallbackInvoker::report_field_reference(oop referrer, oop referree, jint slot) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_FIELD, referrer, referree, slot);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_FIELD, referrer, referree, slot);
  }
}

// report an array referencing an element object
inline bool CallbackInvoker::report_constant_pool_reference(oop referrer, oop referree, jint index) {
  if (is_basic_heap_walk()) {
    return invoke_basic_object_reference_callback(JVMTI_REFERENCE_CONSTANT_POOL, referrer, referree, index);
  } else {
    return invoke_advanced_object_reference_callback(JVMTI_HEAP_REFERENCE_CONSTANT_POOL, referrer, referree, index);
  }
}

// A supporting closure used to process simple roots
class SimpleRootsClosure : public OopClosure {
 private:
  jvmtiHeapReferenceKind _kind;
  bool _continue;

  jvmtiHeapReferenceKind root_kind()    { return _kind; }

 public:
  void set_kind(jvmtiHeapReferenceKind kind) {
    _kind = kind;
    _continue = true;
  }

  inline bool stopped() {
    return !_continue;
  }

  void do_oop(oop* obj_p) {
    // iteration has terminated
    if (stopped()) {
      return;
    }

    // ignore null or deleted handles
    oop o = *obj_p;
    if (o == NULL || o == JNIHandles::deleted_handle()) {
      return;
    }

    jvmtiHeapReferenceKind kind = root_kind();

    // many roots are Klasses so we use the java mirror
    if (o->is_klass()) {
      klassOop k = (klassOop)o;
      o = Klass::cast(k)->java_mirror();
    } else {

      // SystemDictionary::always_strong_oops_do reports the application
      // class loader as a root. We want this root to be reported as
      // a root kind of "OTHER" rather than "SYSTEM_CLASS".
      if (o->is_instance() && root_kind() == JVMTI_HEAP_REFERENCE_SYSTEM_CLASS) {
        kind = JVMTI_HEAP_REFERENCE_OTHER;
      }
    }

    // some objects are ignored - in the case of simple
    // roots it's mostly symbolOops that we are skipping
    // here.
    if (!ServiceUtil::visible_oop(o)) {
      return;
    }

    // invoke the callback
    _continue = CallbackInvoker::report_simple_root(kind, o);

  }
  virtual void do_oop(narrowOop* obj_p) { ShouldNotReachHere(); }
};

// A supporting closure used to process JNI locals
class JNILocalRootsClosure : public OopClosure {
 private:
  jlong _thread_tag;
  jlong _tid;
  jint _depth;
  jmethodID _method;
  bool _continue;
 public:
  void set_context(jlong thread_tag, jlong tid, jint depth, jmethodID method) {
    _thread_tag = thread_tag;
    _tid = tid;
    _depth = depth;
    _method = method;
    _continue = true;
  }

  inline bool stopped() {
    return !_continue;
  }

  void do_oop(oop* obj_p) {
    // iteration has terminated
    if (stopped()) {
      return;
    }

    // ignore null or deleted handles
    oop o = *obj_p;
    if (o == NULL || o == JNIHandles::deleted_handle()) {
      return;
    }

    if (!ServiceUtil::visible_oop(o)) {
      return;
    }

    // invoke the callback
    _continue = CallbackInvoker::report_jni_local_root(_thread_tag, _tid, _depth, _method, o);
  }
  virtual void do_oop(narrowOop* obj_p) { ShouldNotReachHere(); }
};


// A VM operation to iterate over objects that are reachable from
// a set of roots or an initial object.
//
// For VM_HeapWalkOperation the set of roots used is :-
//
// - All JNI global references
// - All inflated monitors
// - All classes loaded by the boot class loader (or all classes
//     in the event that class unloading is disabled)
// - All java threads
// - For each java thread then all locals and JNI local references
//      on the thread's execution stack
// - All visible/explainable objects from Universes::oops_do
//
class VM_HeapWalkOperation: public VM_Operation {
 private:
  enum {
    initial_visit_stack_size = 4000
  };

  bool _is_advanced_heap_walk;                      // indicates FollowReferences
  JvmtiTagMap* _tag_map;
  Handle _initial_object;
  GrowableArray<oop>* _visit_stack;                 // the visit stack

  bool _collecting_heap_roots;                      // are we collecting roots
  bool _following_object_refs;                      // are we following object references

  bool _reporting_primitive_fields;                 // optional reporting
  bool _reporting_primitive_array_values;
  bool _reporting_string_values;

  GrowableArray<oop>* create_visit_stack() {
    return new (ResourceObj::C_HEAP) GrowableArray<oop>(initial_visit_stack_size, true);
  }

  // accessors
  bool is_advanced_heap_walk() const               { return _is_advanced_heap_walk; }
  JvmtiTagMap* tag_map() const                     { return _tag_map; }
  Handle initial_object() const                    { return _initial_object; }

  bool is_following_references() const             { return _following_object_refs; }

  bool is_reporting_primitive_fields()  const      { return _reporting_primitive_fields; }
  bool is_reporting_primitive_array_values() const { return _reporting_primitive_array_values; }
  bool is_reporting_string_values() const          { return _reporting_string_values; }

  GrowableArray<oop>* visit_stack() const          { return _visit_stack; }

  // iterate over the various object types
  inline bool iterate_over_array(oop o);
  inline bool iterate_over_type_array(oop o);
  inline bool iterate_over_class(klassOop o);
  inline bool iterate_over_object(oop o);

  // root collection
  inline bool collect_simple_roots();
  inline bool collect_stack_roots();
  inline bool collect_stack_roots(JavaThread* java_thread, JNILocalRootsClosure* blk);

  // visit an object
  inline bool visit(oop o);

 public:
  VM_HeapWalkOperation(JvmtiTagMap* tag_map,
                       Handle initial_object,
                       BasicHeapWalkContext callbacks,
                       const void* user_data);

  VM_HeapWalkOperation(JvmtiTagMap* tag_map,
                       Handle initial_object,
                       AdvancedHeapWalkContext callbacks,
                       const void* user_data);

  ~VM_HeapWalkOperation();

  VMOp_Type type() const { return VMOp_HeapWalkOperation; }
  void doit();
};


VM_HeapWalkOperation::VM_HeapWalkOperation(JvmtiTagMap* tag_map,
                                           Handle initial_object,
                                           BasicHeapWalkContext callbacks,
                                           const void* user_data) {
  _is_advanced_heap_walk = false;
  _tag_map = tag_map;
  _initial_object = initial_object;
  _following_object_refs = (callbacks.object_ref_callback() != NULL);
  _reporting_primitive_fields = false;
  _reporting_primitive_array_values = false;
  _reporting_string_values = false;
  _visit_stack = create_visit_stack();


  CallbackInvoker::initialize_for_basic_heap_walk(tag_map, _visit_stack, user_data, callbacks);
}

VM_HeapWalkOperation::VM_HeapWalkOperation(JvmtiTagMap* tag_map,
                                           Handle initial_object,
                                           AdvancedHeapWalkContext callbacks,
                                           const void* user_data) {
  _is_advanced_heap_walk = true;
  _tag_map = tag_map;
  _initial_object = initial_object;
  _following_object_refs = true;
  _reporting_primitive_fields = (callbacks.primitive_field_callback() != NULL);;
  _reporting_primitive_array_values = (callbacks.array_primitive_value_callback() != NULL);;
  _reporting_string_values = (callbacks.string_primitive_value_callback() != NULL);;
  _visit_stack = create_visit_stack();

  CallbackInvoker::initialize_for_advanced_heap_walk(tag_map, _visit_stack, user_data, callbacks);
}

VM_HeapWalkOperation::~VM_HeapWalkOperation() {
  if (_following_object_refs) {
    assert(_visit_stack != NULL, "checking");
    delete _visit_stack;
    _visit_stack = NULL;
  }
}

// an array references its class and has a reference to
// each element in the array
inline bool VM_HeapWalkOperation::iterate_over_array(oop o) {
  objArrayOop array = objArrayOop(o);
  if (array->klass() == Universe::systemObjArrayKlassObj()) {
    // filtered out
    return true;
  }

  // array reference to its class
  oop mirror = objArrayKlass::cast(array->klass())->java_mirror();
  if (!CallbackInvoker::report_class_reference(o, mirror)) {
    return false;
  }

  // iterate over the array and report each reference to a
  // non-null element
  for (int index=0; index<array->length(); index++) {
    oop elem = array->obj_at(index);
    if (elem == NULL) {
      continue;
    }

    // report the array reference o[index] = elem
    if (!CallbackInvoker::report_array_element_reference(o, elem, index)) {
      return false;
    }
  }
  return true;
}

// a type array references its class
inline bool VM_HeapWalkOperation::iterate_over_type_array(oop o) {
  klassOop k = o->klass();
  oop mirror = Klass::cast(k)->java_mirror();
  if (!CallbackInvoker::report_class_reference(o, mirror)) {
    return false;
  }

  // report the array contents if required
  if (is_reporting_primitive_array_values()) {
    if (!CallbackInvoker::report_primitive_array_values(o)) {
      return false;
    }
  }
  return true;
}

// verify that a static oop field is in range
static inline bool verify_static_oop(instanceKlass* ik,
                                     klassOop k, int offset) {
  address obj_p = (address)k + offset;
  address start = (address)ik->start_of_static_fields();
  address end = start + (ik->static_oop_field_size() * heapOopSize);
  assert(end >= start, "sanity check");

  if (obj_p >= start && obj_p < end) {
    return true;
  } else {
    return false;
  }
}

// a class references its super class, interfaces, class loader, ...
// and finally its static fields
inline bool VM_HeapWalkOperation::iterate_over_class(klassOop k) {
  int i;
  Klass* klass = klassOop(k)->klass_part();

  if (klass->oop_is_instance()) {
    instanceKlass* ik = instanceKlass::cast(k);

    // ignore the class if it's has been initialized yet
    if (!ik->is_linked()) {
      return true;
    }

    // get the java mirror
    oop mirror = klass->java_mirror();

    // super (only if something more interesting than java.lang.Object)
    klassOop java_super = ik->java_super();
    if (java_super != NULL && java_super != SystemDictionary::Object_klass()) {
      oop super = Klass::cast(java_super)->java_mirror();
      if (!CallbackInvoker::report_superclass_reference(mirror, super)) {
        return false;
      }
    }

    // class loader
    oop cl = ik->class_loader();
    if (cl != NULL) {
      if (!CallbackInvoker::report_class_loader_reference(mirror, cl)) {
        return false;
      }
    }

    // protection domain
    oop pd = ik->protection_domain();
    if (pd != NULL) {
      if (!CallbackInvoker::report_protection_domain_reference(mirror, pd)) {
        return false;
      }
    }

    // signers
    oop signers = ik->signers();
    if (signers != NULL) {
      if (!CallbackInvoker::report_signers_reference(mirror, signers)) {
        return false;
      }
    }

    // references from the constant pool
    {
      const constantPoolOop pool = ik->constants();
      for (int i = 1; i < pool->length(); i++) {
        constantTag tag = pool->tag_at(i).value();
        if (tag.is_string() || tag.is_klass()) {
          oop entry;
          if (tag.is_string()) {
            entry = pool->resolved_string_at(i);
            assert(java_lang_String::is_instance(entry), "must be string");
          } else {
            entry = Klass::cast(pool->resolved_klass_at(i))->java_mirror();
          }
          if (!CallbackInvoker::report_constant_pool_reference(mirror, entry, (jint)i)) {
            return false;
          }
        }
      }
    }

    // interfaces
    // (These will already have been reported as references from the constant pool
    //  but are specified by IterateOverReachableObjects and must be reported).
    objArrayOop interfaces = ik->local_interfaces();
    for (i = 0; i < interfaces->length(); i++) {
      oop interf = Klass::cast((klassOop)interfaces->obj_at(i))->java_mirror();
      if (interf == NULL) {
        continue;
      }
      if (!CallbackInvoker::report_interface_reference(mirror, interf)) {
        return false;
      }
    }

    // iterate over the static fields

    ClassFieldMap* field_map = ClassFieldMap::create_map_of_static_fields(k);
    for (i=0; i<field_map->field_count(); i++) {
      ClassFieldDescriptor* field = field_map->field_at(i);
      char type = field->field_type();
      if (!is_primitive_field_type(type)) {
        oop fld_o = k->obj_field(field->field_offset());
        assert(verify_static_oop(ik, k, field->field_offset()), "sanity check");
        if (fld_o != NULL) {
          int slot = field->field_index();
          if (!CallbackInvoker::report_static_field_reference(mirror, fld_o, slot)) {
            delete field_map;
            return false;
          }
        }
      } else {
         if (is_reporting_primitive_fields()) {
           address addr = (address)k + field->field_offset();
           int slot = field->field_index();
           if (!CallbackInvoker::report_primitive_static_field(mirror, slot, addr, type)) {
             delete field_map;
             return false;
          }
        }
      }
    }
    delete field_map;

    return true;
  }

  return true;
}

// an object references a class and its instance fields
// (static fields are ignored here as we report these as
// references from the class).
inline bool VM_HeapWalkOperation::iterate_over_object(oop o) {
  // reference to the class
  if (!CallbackInvoker::report_class_reference(o, Klass::cast(o->klass())->java_mirror())) {
    return false;
  }

  // iterate over instance fields
  ClassFieldMap* field_map = JvmtiCachedClassFieldMap::get_map_of_instance_fields(o);
  for (int i=0; i<field_map->field_count(); i++) {
    ClassFieldDescriptor* field = field_map->field_at(i);
    char type = field->field_type();
    if (!is_primitive_field_type(type)) {
      oop fld_o = o->obj_field(field->field_offset());
      if (fld_o != NULL) {
        // reflection code may have a reference to a klassOop.
        // - see sun.reflect.UnsafeStaticFieldAccessorImpl and sun.misc.Unsafe
        if (fld_o->is_klass()) {
          klassOop k = (klassOop)fld_o;
          fld_o = Klass::cast(k)->java_mirror();
        }
        int slot = field->field_index();
        if (!CallbackInvoker::report_field_reference(o, fld_o, slot)) {
          return false;
        }
      }
    } else {
      if (is_reporting_primitive_fields()) {
        // primitive instance field
        address addr = (address)o + field->field_offset();
        int slot = field->field_index();
        if (!CallbackInvoker::report_primitive_instance_field(o, slot, addr, type)) {
          return false;
        }
      }
    }
  }

  // if the object is a java.lang.String
  if (is_reporting_string_values() &&
      o->klass() == SystemDictionary::String_klass()) {
    if (!CallbackInvoker::report_string_value(o)) {
      return false;
    }
  }
  return true;
}


// collects all simple (non-stack) roots.
// if there's a heap root callback provided then the callback is
// invoked for each simple root.
// if an object reference callback is provided then all simple
// roots are pushed onto the marking stack so that they can be
// processed later
//
inline bool VM_HeapWalkOperation::collect_simple_roots() {
  SimpleRootsClosure blk;

  // JNI globals
  blk.set_kind(JVMTI_HEAP_REFERENCE_JNI_GLOBAL);
  JNIHandles::oops_do(&blk);
  if (blk.stopped()) {
    return false;
  }

  // Preloaded classes and loader from the system dictionary
  blk.set_kind(JVMTI_HEAP_REFERENCE_SYSTEM_CLASS);
  SystemDictionary::always_strong_oops_do(&blk);
  if (blk.stopped()) {
    return false;
  }

  // Inflated monitors
  blk.set_kind(JVMTI_HEAP_REFERENCE_MONITOR);
  ObjectSynchronizer::oops_do(&blk);
  if (blk.stopped()) {
    return false;
  }

  // Threads
  for (JavaThread* thread = Threads::first(); thread != NULL ; thread = thread->next()) {
    oop threadObj = thread->threadObj();
    if (threadObj != NULL && !thread->is_exiting() && !thread->is_hidden_from_external_view()) {
      bool cont = CallbackInvoker::report_simple_root(JVMTI_HEAP_REFERENCE_THREAD, threadObj);
      if (!cont) {
        return false;
      }
    }
  }

  // Other kinds of roots maintained by HotSpot
  // Many of these won't be visible but others (such as instances of important
  // exceptions) will be visible.
  blk.set_kind(JVMTI_HEAP_REFERENCE_OTHER);
  Universe::oops_do(&blk);

  // If there are any non-perm roots in the code cache, visit them.
  blk.set_kind(JVMTI_HEAP_REFERENCE_OTHER);
  CodeBlobToOopClosure look_in_blobs(&blk, false);
  CodeCache::scavenge_root_nmethods_do(&look_in_blobs);

  return true;
}

// Walk the stack of a given thread and find all references (locals
// and JNI calls) and report these as stack references
inline bool VM_HeapWalkOperation::collect_stack_roots(JavaThread* java_thread,
                                                      JNILocalRootsClosure* blk)
{
  oop threadObj = java_thread->threadObj();
  assert(threadObj != NULL, "sanity check");

  // only need to get the thread's tag once per thread
  jlong thread_tag = tag_for(_tag_map, threadObj);

  // also need the thread id
  jlong tid = java_lang_Thread::thread_id(threadObj);


  if (java_thread->has_last_Java_frame()) {

    // vframes are resource allocated
    Thread* current_thread = Thread::current();
    ResourceMark rm(current_thread);
    HandleMark hm(current_thread);

    RegisterMap reg_map(java_thread);
    frame f = java_thread->last_frame();
    vframe* vf = vframe::new_vframe(&f, &reg_map, java_thread);

    bool is_top_frame = true;
    int depth = 0;
    frame* last_entry_frame = NULL;

    while (vf != NULL) {
      if (vf->is_java_frame()) {

        // java frame (interpreted, compiled, ...)
        javaVFrame *jvf = javaVFrame::cast(vf);

        // the jmethodID
        jmethodID method = jvf->method()->jmethod_id();

        if (!(jvf->method()->is_native())) {
          jlocation bci = (jlocation)jvf->bci();
          StackValueCollection* locals = jvf->locals();
          for (int slot=0; slot<locals->size(); slot++) {
            if (locals->at(slot)->type() == T_OBJECT) {
              oop o = locals->obj_at(slot)();
              if (o == NULL) {
                continue;
              }

              // stack reference
              if (!CallbackInvoker::report_stack_ref_root(thread_tag, tid, depth, method,
                                                   bci, slot, o)) {
                return false;
              }
            }
          }
        } else {
          blk->set_context(thread_tag, tid, depth, method);
          if (is_top_frame) {
            // JNI locals for the top frame.
            java_thread->active_handles()->oops_do(blk);
          } else {
            if (last_entry_frame != NULL) {
              // JNI locals for the entry frame
              assert(last_entry_frame->is_entry_frame(), "checking");
              last_entry_frame->entry_frame_call_wrapper()->handles()->oops_do(blk);
            }
          }
        }
        last_entry_frame = NULL;
        depth++;
      } else {
        // externalVFrame - for an entry frame then we report the JNI locals
        // when we find the corresponding javaVFrame
        frame* fr = vf->frame_pointer();
        assert(fr != NULL, "sanity check");
        if (fr->is_entry_frame()) {
          last_entry_frame = fr;
        }
      }

      vf = vf->sender();
      is_top_frame = false;
    }
  } else {
    // no last java frame but there may be JNI locals
    blk->set_context(thread_tag, tid, 0, (jmethodID)NULL);
    java_thread->active_handles()->oops_do(blk);
  }
  return true;
}


// collects all stack roots - for each thread it walks the execution
// stack to find all references and local JNI refs.
inline bool VM_HeapWalkOperation::collect_stack_roots() {
  JNILocalRootsClosure blk;
  for (JavaThread* thread = Threads::first(); thread != NULL ; thread = thread->next()) {
    oop threadObj = thread->threadObj();
    if (threadObj != NULL && !thread->is_exiting() && !thread->is_hidden_from_external_view()) {
      if (!collect_stack_roots(thread, &blk)) {
        return false;
      }
    }
  }
  return true;
}

// visit an object
// first mark the object as visited
// second get all the outbound references from this object (in other words, all
// the objects referenced by this object).
//
bool VM_HeapWalkOperation::visit(oop o) {
  // mark object as visited
  assert(!ObjectMarker::visited(o), "can't visit same object more than once");
  ObjectMarker::mark(o);

  // instance
  if (o->is_instance()) {
    if (o->klass() == SystemDictionary::Class_klass()) {
      o = klassOop_if_java_lang_Class(o);
      if (o->is_klass()) {
        // a java.lang.Class
        return iterate_over_class(klassOop(o));
      }
    } else {
      return iterate_over_object(o);
    }
  }

  // object array
  if (o->is_objArray()) {
    return iterate_over_array(o);
  }

  // type array
  if (o->is_typeArray()) {
    return iterate_over_type_array(o);
  }

  return true;
}

void VM_HeapWalkOperation::doit() {
  ResourceMark rm;
  ObjectMarkerController marker;
  ClassFieldMapCacheMark cm;

  assert(visit_stack()->is_empty(), "visit stack must be empty");

  // the heap walk starts with an initial object or the heap roots
  if (initial_object().is_null()) {
    if (!collect_simple_roots()) return;
    if (!collect_stack_roots()) return;
  } else {
    visit_stack()->push(initial_object()());
  }

  // object references required
  if (is_following_references()) {

    // visit each object until all reachable objects have been
    // visited or the callback asked to terminate the iteration.
    while (!visit_stack()->is_empty()) {
      oop o = visit_stack()->pop();
      if (!ObjectMarker::visited(o)) {
        if (!visit(o)) {
          break;
        }
      }
    }
  }
}

// iterate over all objects that are reachable from a set of roots
void JvmtiTagMap::iterate_over_reachable_objects(jvmtiHeapRootCallback heap_root_callback,
                                                 jvmtiStackReferenceCallback stack_ref_callback,
                                                 jvmtiObjectReferenceCallback object_ref_callback,
                                                 const void* user_data) {
  MutexLocker ml(Heap_lock);
  BasicHeapWalkContext context(heap_root_callback, stack_ref_callback, object_ref_callback);
  VM_HeapWalkOperation op(this, Handle(), context, user_data);
  VMThread::execute(&op);
}

// iterate over all objects that are reachable from a given object
void JvmtiTagMap::iterate_over_objects_reachable_from_object(jobject object,
                                                             jvmtiObjectReferenceCallback object_ref_callback,
                                                             const void* user_data) {
  oop obj = JNIHandles::resolve(object);
  Handle initial_object(Thread::current(), obj);

  MutexLocker ml(Heap_lock);
  BasicHeapWalkContext context(NULL, NULL, object_ref_callback);
  VM_HeapWalkOperation op(this, initial_object, context, user_data);
  VMThread::execute(&op);
}

// follow references from an initial object or the GC roots
void JvmtiTagMap::follow_references(jint heap_filter,
                                    KlassHandle klass,
                                    jobject object,
                                    const jvmtiHeapCallbacks* callbacks,
                                    const void* user_data)
{
  oop obj = JNIHandles::resolve(object);
  Handle initial_object(Thread::current(), obj);

  MutexLocker ml(Heap_lock);
  AdvancedHeapWalkContext context(heap_filter, klass, callbacks);
  VM_HeapWalkOperation op(this, initial_object, context, user_data);
  VMThread::execute(&op);
}


void JvmtiTagMap::weak_oops_do(BoolObjectClosure* is_alive, OopClosure* f) {
  // No locks during VM bring-up (0 threads) and no safepoints after main
  // thread creation and before VMThread creation (1 thread); initial GC
  // verification can happen in that window which gets to here.
  assert(Threads::number_of_threads() <= 1 ||
         SafepointSynchronize::is_at_safepoint(),
         "must be executed at a safepoint");
  if (JvmtiEnv::environments_might_exist()) {
    JvmtiEnvIterator it;
    for (JvmtiEnvBase* env = it.first(); env != NULL; env = it.next(env)) {
      JvmtiTagMap* tag_map = env->tag_map();
      if (tag_map != NULL && !tag_map->is_empty()) {
        tag_map->do_weak_oops(is_alive, f);
      }
    }
  }
}

void JvmtiTagMap::do_weak_oops(BoolObjectClosure* is_alive, OopClosure* f) {

  // does this environment have the OBJECT_FREE event enabled
  bool post_object_free = env()->is_enabled(JVMTI_EVENT_OBJECT_FREE);

  // counters used for trace message
  int freed = 0;
  int moved = 0;

  JvmtiTagHashmap* hashmap = this->hashmap();

  // reenable sizing (if disabled)
  hashmap->set_resizing_enabled(true);

  // if the hashmap is empty then we can skip it
  if (hashmap->_entry_count == 0) {
    return;
  }

  // now iterate through each entry in the table

  JvmtiTagHashmapEntry** table = hashmap->table();
  int size = hashmap->size();

  JvmtiTagHashmapEntry* delayed_add = NULL;

  for (int pos = 0; pos < size; ++pos) {
    JvmtiTagHashmapEntry* entry = table[pos];
    JvmtiTagHashmapEntry* prev = NULL;

    while (entry != NULL) {
      JvmtiTagHashmapEntry* next = entry->next();

      oop* obj = entry->object_addr();

      // has object been GC'ed
      if (!is_alive->do_object_b(entry->object())) {
        // grab the tag
        jlong tag = entry->tag();
        guarantee(tag != 0, "checking");

        // remove GC'ed entry from hashmap and return the
        // entry to the free list
        hashmap->remove(prev, pos, entry);
        destroy_entry(entry);

        // post the event to the profiler
        if (post_object_free) {
          JvmtiExport::post_object_free(env(), tag);
        }

        ++freed;
      } else {
        f->do_oop(entry->object_addr());
        oop new_oop = entry->object();

        // if the object has moved then re-hash it and move its
        // entry to its new location.
        unsigned int new_pos = JvmtiTagHashmap::hash(new_oop, size);
        if (new_pos != (unsigned int)pos) {
          if (prev == NULL) {
            table[pos] = next;
          } else {
            prev->set_next(next);
          }
          if (new_pos < (unsigned int)pos) {
            entry->set_next(table[new_pos]);
            table[new_pos] = entry;
          } else {
            // Delay adding this entry to it's new position as we'd end up
            // hitting it again during this iteration.
            entry->set_next(delayed_add);
            delayed_add = entry;
          }
          moved++;
        } else {
          // object didn't move
          prev = entry;
        }
      }

      entry = next;
    }
  }

  // Re-add all the entries which were kept aside
  while (delayed_add != NULL) {
    JvmtiTagHashmapEntry* next = delayed_add->next();
    unsigned int pos = JvmtiTagHashmap::hash(delayed_add->object(), size);
    delayed_add->set_next(table[pos]);
    table[pos] = delayed_add;
    delayed_add = next;
  }

  // stats
  if (TraceJVMTIObjectTagging) {
    int post_total = hashmap->_entry_count;
    int pre_total = post_total + freed;

    tty->print_cr("(%d->%d, %d freed, %d total moves)",
        pre_total, post_total, freed, moved);
  }
}
