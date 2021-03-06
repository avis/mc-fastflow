/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef __FF_MPMCQUEUE_HPP_ 
#define __FF_MPMCQUEUE_HPP_ 

/* 
 * This file contains Multi-Producer/Multi-Consumer queue implementations.
 * 
 *   * MPMC_Ptr_Queue   bounded MPMC queue by Dmitry Vyukov 
 *   * uMPMC_Ptr_Queue  unbounded MPMC queue by Massimo Torquati 
 *   * MSqueue          unbounded MPMC queue by Michael & Scott
 *
 *  - Author: 
 *     Massimo Torquati <torquati@di.unipi.it> <massimotor@gmail.com>
 *
 */


#include <stdlib.h>
#include <ff/buffer.hpp>
#include <ff/sysdep.h>
#include <ff/allocator.hpp>
#include <ff/atomic/abstraction_dcas.h>

/*
 * NOTE: You should define USE_STD_C0X if you want to use 
 *       the new C++0x standard (-std=c++0x)
 *
 */
#if defined(USE_STD_C0X)
 // Check for g++ version >= 4.5
 #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
  #include <atomic>
 #else
  // Check for g++ version >= 4.4
  #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4)
   #include <cstdatomic>
  #else
   #define USE_STD_0X
  #endif
 #endif
#endif // USE_STD_C0X

namespace ff {

#define CAS abstraction_cas

/* 
 *  In the following we implement two kind of queues: 
 *   - the MPMC_Ptr_Queue is an implementation of the ** bounded ** 
 *     Multi-Producer/Multi-Consumer queue algorithm by Dmitry Vyukov 
 *     (www.1024cores.net). It stores pointers.
 *
 *   - the uMPMC_Ptr_Queue implements an ** unbounded ** 
 *     Multi-Producer/Multi-Consumer queue which does not require 
 *     any special memory allocator to avoid dangling pointers. 
 *     The implementation blends together the MPMC_Ptr_Queue and the 
 *     uSWSR_Ptr_Buffer.
 *  
 */
#if defined(USE_STD_C0X)
class MPMC_Ptr_Queue {
private:
    struct element_t {
        std::atomic<unsigned long> seq;
        void *                     data;
    };
public:
    MPMC_Ptr_Queue(size_t size) {
        if (size<2) size=2;
        // we need a size that is a power 2 in order to set the mask 
        if (size & (size-1)) {
            size_t p=1;
            while (size>p) p <<= 1;
            size = p;
        }
        mask = size-1;
    }
    
    ~MPMC_Ptr_Queue() {
        if (buf) {
            delete [] buf;
            buf=NULL;
        }
    }

    inline bool init() {
        size_t size=mask+1;
        buf = new element_t[size];
        if (!buf) return false;
        for(size_t i=0;i<size;++i) {
            buf[i].data = NULL;
            buf[i].seq.store(i,std::memory_order_relaxed);
        }
        pwrite.store(0,std::memory_order_relaxed);        
        pread.store(0,std::memory_order_relaxed);
        return true;
    }
    
    // non-blocking push
    inline bool push(void *const data) {
        unsigned long pw, seq;
        element_t * node;
        do {
            pw    = pwrite.load(std::memory_order_relaxed);
            node  = &buf[pw & mask];
            seq   = node->seq.load(std::memory_order_acquire);
            if (pw == seq) {
                if (pwrite.compare_exchange_weak(pw, pw+1, std::memory_order_relaxed))
                    break;
            } else 
                if (pw > seq) return false; // queue full
        } while(1);
        node->data = data;
        node->seq.store(seq+1,std::memory_order_release);
        return true;
    }

    // non-blocking pop
    inline bool pop(void** data) {
        unsigned long pr, seq;
        element_t * node;
        do {
            pr    = pread.load(std::memory_order_relaxed);
            node  = &buf[pr & mask];
            seq   = node->seq.load(std::memory_order_acquire);

            long diff = seq - (pr+1);
            if (diff == 0) {
                if (pread.compare_exchange_weak(pr, (pr+1), std::memory_order_relaxed))
                    break;
            } else { 
                if (diff < 0) return false; // queue empty
            }
        } while(1);
        *data = node->data;
        node->seq.store((pr+mask+1), std::memory_order_release);
        return true;
    }
    
private:
    // WARNING: on 64bit Windows platform sizeof(unsigned long) = 32 !!
    std::atomic<unsigned long>  pwrite;
    long padding1[longxCacheLine-1];
    std::atomic<unsigned long>  pread;
    long padding2[longxCacheLine-1];
    element_t *                 buf;
    unsigned long               mask;
};
#else  // using internal atomic operations

class MPMC_Ptr_Queue {
protected:
    struct element_t {
        atomic_long_t seq;
        void *        data;
    };

public:
    MPMC_Ptr_Queue(size_t size) {
        if (size<2) size=2;
        // we need a size that is a power 2 in order to set the mask 
        if (size & (size-1)) {
            size_t p=1;
            while (size>p) p <<= 1;
            size = p;
        }
        mask = size-1;
    }

    ~MPMC_Ptr_Queue() { 
        if (buf) {
#if defined(_MSC_VER)
            if (buf) ::posix_memalign_free(buf);    
#else	
            if (buf) ::free(buf);
#endif               
            buf = NULL;
        }
    }

    inline bool init() {
        size_t size= mask+1;

#if (defined(MAC_OS_X_VERSION_MIN_REQUIRED) && (MAC_OS_X_VERSION_MIN_REQUIRED < 1060))
        buf = (element_t*)::malloc(size*sizeof(element_t));
        if (!buf) return false;       
#else
        void * ptr;
        if (posix_memalign(&ptr,longxCacheLine*sizeof(long),size*sizeof(element_t))!=0)
            return false;
        buf=(element_t*)ptr;
#endif
        for(size_t i=0;i<size;++i) {
            buf[i].data = NULL;
            atomic_long_set(&buf[i].seq,i);
        }
        atomic_long_set(&pwrite,0);
        atomic_long_set(&pread,0);
        return true;
    }

    // non-blocking push
    inline bool push(void *const data) {
        unsigned long pw, seq;
        element_t * node;
        do {
            pw    = atomic_long_read(&pwrite);
            node  = &buf[pw & mask];
            seq   = atomic_long_read(&node->seq);

            if (pw == seq) {
                if (abstraction_cas((volatile atom_t*)&pwrite, (atom_t)(pw+1), (atom_t)pw)==(atom_t)pw) 
                    break;
            } else 
                if (pw > seq) return false;
        } while(1);
        node->data = data;
        //atomic_long_inc(&node->seq);
        atomic_long_set(&node->seq, (seq+1));
        return true;
    }
        
    // non-blocking pop
    inline bool pop(void** data) {
        unsigned long pr , seq;
        element_t * node;
        do {
            pr    = atomic_long_read(&pread);
            node  = &buf[pr & mask];
            seq   = atomic_long_read(&node->seq);
            long diff = seq - (pr+1);
            if (diff == 0) {
                if (abstraction_cas((volatile atom_t*)&pread, (atom_t)(pr+1), (atom_t)pr)==(atom_t)pr) 
                    break;
            } else { 
                if (diff < 0) return false;
            }
        } while(1);
        *data = node->data;
        atomic_long_set(&node->seq,(pr+mask+1));
        return true;
    }
    
private:
    // WARNING: on 64bit Windows platform sizeof(unsigned long) = 32 !!
    atomic_long_t  pwrite;
    long           padding1[longxCacheLine-1];
    atomic_long_t  pread;
    long           padding2[longxCacheLine-1];
protected:
    element_t *    buf;
    unsigned long  mask;
};


// unbounded Multi-Producer/Multi-Consumer FIFO queue
class uMPMC_Ptr_Queue {
protected:
    enum {DEFAULT_NUM_QUEUES=4, DEFAULT_uSPSC_SIZE=2048};

    typedef void *        data_element_t;
    typedef atomic_long_t sequenceP_t;
    typedef atomic_long_t sequenceC_t;

public:
    uMPMC_Ptr_Queue() {}
    

    ~uMPMC_Ptr_Queue() {
        if (buf) {
            for(size_t i=0;i<(mask+1);++i) {
                if (buf[i]) delete (uSWSR_Ptr_Buffer*)(buf[i]);
            }
#if defined(_MSC_VER)
            posix_memalign_free(buf);    
#else	
            ::free(buf);
#endif               
            buf = NULL;
        }
        if (seqP) {
#if defined(_MSC_VER)
            posix_memalign_free(seqP);    
#else	
            ::free(seqP);
#endif               
        }
        if (seqC) {
#if defined(_MSC_VER)
            posix_memalign_free(seqC);    
#else	
            ::free(seqC);
#endif               
        }
    }

    inline bool init(unsigned long nqueues=DEFAULT_NUM_QUEUES, size_t size=DEFAULT_uSPSC_SIZE) {
        if (nqueues<2) nqueues=2;
        if (nqueues & (nqueues-1)) {
            size_t p=1;
            while (nqueues>p) p <<= 1;
            size = p;
        }
        mask = nqueues-1;

#if (defined(MAC_OS_X_VERSION_MIN_REQUIRED) && (MAC_OS_X_VERSION_MIN_REQUIRED < 1060))
        buf = (data_element_t*)::malloc(nqueues*sizeof(data_element_t));
        if (!buf) return false;
        seqP = (sequenceP_t*)::malloc(nqueues*sizeof(sequenceP_t));
        if (!seqP) return false;
        seqC = (sequenceC_t*)::malloc(nqueues*sizeof(sequenceC_t));
        if (!seqC) return false;
#else
        void * ptr;
        if (posix_memalign(&ptr,longxCacheLine*sizeof(long),nqueues*sizeof(data_element_t))!=0)
            return false;
        buf=(data_element_t*)ptr;
        if (posix_memalign(&ptr,longxCacheLine*sizeof(long),nqueues*sizeof(sequenceP_t))!=0)
            return false;
        seqP=(sequenceP_t*)ptr;
        if (posix_memalign(&ptr,longxCacheLine*sizeof(long),nqueues*sizeof(sequenceC_t))!=0)
            return false;
        seqC=(sequenceP_t*)ptr;
#endif
        for(size_t i=0;i<nqueues;++i) {
            buf[i]= new uSWSR_Ptr_Buffer(size);
            ((uSWSR_Ptr_Buffer*)(buf[i]))->init();
            atomic_long_set(&(seqP[i]),i);
            atomic_long_set(&(seqC[i]),i);
        }
        atomic_long_set(&preadP,0);
        atomic_long_set(&preadC,0);
        return true;
    }

    // it always returns true
    inline bool push(void *const data) {
        unsigned long pw,seq,idx;
        do {
            pw    = atomic_long_read(&preadP);
            idx   = pw & mask;
            seq   = atomic_long_read(&seqP[idx]);
            if (pw == seq) {
                if (abstraction_cas((volatile atom_t*)&preadP, (atom_t)(pw+1), (atom_t)pw)==(atom_t)pw) 
                    break;
            } 
        } while(1);
        ((uSWSR_Ptr_Buffer*)(buf[idx]))->push(data); // cannot fail
        atomic_long_set(&seqP[idx],(pw+mask+1));
        return true;               
    }
    
    // non-blocking pop
    inline bool pop(void ** data) {
        unsigned long pr,seq,idx;
        do {
            pr     = atomic_long_read(&preadC);
            idx    = pr & mask;
            seq    = atomic_long_read(&seqC[idx]);
            if (pr == seq) { 
                if (atomic_long_read(&seqP[idx]) <= seq) return false; // queue 
                if (abstraction_cas((volatile atom_t*)&preadC, (atom_t)(pr+1), (atom_t)pr)==(atom_t)pr) 
                    break;
            }  
        } while(1);
        ((uSWSR_Ptr_Buffer*)(buf[idx]))->pop(data);
        atomic_long_set(&seqC[idx],(pr+mask+1));
        return true;
    }

private:
    // WARNING: on 64bit Windows platform sizeof(unsigned long) = 32 !!
    atomic_long_t  preadP;
    long           padding1[longxCacheLine-1];
    atomic_long_t  preadC;
    long           padding2[longxCacheLine-1];
protected:
    data_element_t *  buf;
    sequenceP_t    *  seqP;
    sequenceC_t    *  seqC;
    unsigned long     mask;

};

#endif // USE_STD_C0X


/* 
 * MSqueue is an implementation of the lock-free FIFO MPMC queue by
 * Maged M. Michael and Michael L. Scott described in the paper:
 *   "Simple, Fast, and Practical Non-Blocking and Blocking
 *    Concurrent Queue Algorithms", PODC 1996.
 *
 * The MSqueue implementation is inspired to the one in the liblfds 
 * libraly that is a portable, license-free, lock-free data structure 
 * library written in C. The liblfds implementation uses double-word CAS 
 * (aka DCAS) whereas this implementation uses only single-word CAS 
 * since it relies on a implementation of a memory allocator (used to 
 * allocate internal queue nodes) which implements a deferred reclamation 
 * algorithm able to solve both the ABA problem and the dangling pointer 
 * problem.
 *
 * More info about liblfds can be found at http://www.liblfds.org
 *
 */
class MSqueue {
private:
    enum {MSQUEUE_PTR=0 };

    // forward decl of Node type
    struct Node;
 
    struct Pointer {
        Pointer() { ptr[MSQUEUE_PTR]=0;}

        inline bool operator !() {
            return (ptr[MSQUEUE_PTR]==0);
        }
        inline Pointer& operator=(const Pointer & p) {
            ptr[MSQUEUE_PTR]=p.ptr[MSQUEUE_PTR];
            return *this;
        }

        inline Pointer& operator=(Node & node) {
            ptr[MSQUEUE_PTR]=&node;
            return *this;
        }

        inline Pointer & getNodeNext() {
            return ptr[MSQUEUE_PTR]->next;
        }
        inline Node * getNode() { return  ptr[MSQUEUE_PTR]; }

        inline bool operator==( const Pointer& r ) const {
            return ((ptr[MSQUEUE_PTR]==r.ptr[MSQUEUE_PTR]));
        }

        inline operator volatile atom_t * () const { 
            union { Node* const volatile* p1; volatile atom_t * p2;} pn;
            pn.p1 = ptr;
            return pn.p2; 
        }
        inline operator atom_t * () const { 
            union { Node* const volatile* p1; atom_t * p2;} pn;
            pn.p1 = ptr;
            return pn.p2; 
        }
        
        inline operator atom_t () const { 
            union { Node* volatile p1; atom_t p2;} pn;
            pn.p1 = ptr[MSQUEUE_PTR];
            return pn.p2; 
        }

        inline void set(Node & node) {
            ptr[MSQUEUE_PTR]=&node;
        }

        inline void * getData() const { return ptr[MSQUEUE_PTR]->getData(); }

        Node * volatile ptr[1];
    } ALIGN_TO(ALIGN_SINGLE_POINTER);

    struct Node {
        Node():data(0) { next.ptr[MSQUEUE_PTR]=0;}
        Node(void * data):data(data) {
            next.ptr[MSQUEUE_PTR]=0;
        }
        
        inline operator atom_t * () const { return (atom_t *)next; }

        inline void   setData(void * const d) { data=d;}
        inline void * getData() const { return data; }

        Pointer   next;
        void    * data;
    } ALIGN_TO(ALIGN_DOUBLE_POINTER);

    Pointer  head;
    long padding1[longxCacheLine-(sizeof(Pointer)/sizeof(long))];
    Pointer  tail;
    long padding2[longxCacheLine-(sizeof(Pointer)/sizeof(long))];
    FFAllocator *delayedAllocator;

private:
    inline void allocnode(Pointer & p, void * data) {
        union { Node * p1; void * p2;} pn;

        if (delayedAllocator->posix_memalign((void**)&pn.p2,ALIGN_DOUBLE_POINTER,sizeof(Node))!=0) {
            abort();
        }            
        new (pn.p2) Node(data);
        p.set(*pn.p1);
    }

    inline void deallocnode( Node * n) {
        n->~Node();
        delayedAllocator->free(n);
    }

public:
    MSqueue(): delayedAllocator(NULL) { }
    
    ~MSqueue() {
        if (delayedAllocator)  {
            delete delayedAllocator;
            delayedAllocator = NULL;
        }
    }

    MSqueue& operator=(const MSqueue& v) { 
        head=v.head;
        tail=v.tail;
        return *this;
    }

    /* initialize the MSqueue */
    int init() {
        if (delayedAllocator) return 0;
        delayedAllocator = new FFAllocator(2); 
        if (!delayedAllocator) {
            std::cerr << "ERROR: MSqueue, cannot allocate FFAllocator!!!\n";
            return -1;
        }

        // create the first NULL node 
        // so the queue is never really empty
        Pointer dummy;
        allocnode(dummy,NULL);
        
        head = dummy;
        tail = dummy;
        return 1;
    }

    // insert method, it never fails
    inline bool push(void * const data) {
        bool done = false;

        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer tailptr;
        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer next;
        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer node;
        allocnode(node,data);

        do {
            tailptr = tail;
            next    = tailptr.getNodeNext();

            if (tailptr == tail) {
                if (!next) { // tail was pointing to the last node
                    done = (CAS((volatile atom_t *)(tailptr.getNodeNext()), 
                                (atom_t)node, 
                                (atom_t)next) == (atom_t)next);
                } else {     // tail was not pointing to the last node
                    CAS((volatile atom_t *)tail, (atom_t)next, (atom_t)tailptr);
                }
            }
        } while(!done);
        CAS((volatile atom_t *)tail, (atom_t)node, (atom_t) tailptr);
        return true;
    }
    
    // extract method, it returns false if the queue is empty
    inline bool  pop(void ** data) {        
        bool done = false;

        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer headptr;
        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer tailptr;
        ALIGN_TO(ALIGN_SINGLE_POINTER) Pointer next;

        do {
            headptr = head;
            tailptr = tail;
            next    = headptr.getNodeNext();

            if (head == headptr) {
                if (headptr.getNode() == tailptr.getNode()) {
                    if (!next) return false; // empty
                    CAS((volatile atom_t *)tail, (atom_t)next, (atom_t)tailptr);
                } else {
                    *data = next.getData();
                    done = (CAS((volatile atom_t *)head, (atom_t)next, (atom_t)headptr) == (atom_t)headptr);
                }
            }
        } while(!done);

        deallocnode(headptr.getNode());
        return true;
    } 

    // return true if the queue is empty 
    inline bool empty() { 
        if ((head.getNode() == tail.getNode()) && !(head.getNodeNext()))
            return true;
        return false;            
    }
};


/* ---------------------- experimental code -------------------------- */

/*
 * Simple and scalable Multi-Producer/Multi-Consumer queue.
 * By defining at compile time MULTI_MPMC_RELAX_FIFO_ORDERING it is possible 
 * to improve performance relaxing FIFO ordering in the pop method.
 *
 * The underling MPMC queue (the Q template parameter) should export at least 
 * the following methods:
 *
 *   bool push(T)
 *   bool pop(T&)
 *   bool empty() 
 *
 *
 */
template <typename Q>
class scalableMPMCqueue {
public:
    enum {DEFAULT_POOL_SIZE=4};

    scalableMPMCqueue() {
        atomic_long_set(&enqueue,0);
        atomic_long_set(&count, 0);

#if !defined(MULTI_MPMC_RELAX_FIFO_ORDERING)
        // NOTE: dequeue must start from 1 because enqueue is incremented
        //       using atomic_long_inc_return which first increments and than
        //       return the value.
        atomic_long_set(&dequeue,1); 
#else
        atomic_long_set(&dequeue,0);
#endif
    }
    
    int init(size_t poolsize = DEFAULT_POOL_SIZE) {
        if (poolsize > pool.size()) {
            pool.resize(poolsize);
        }
        
        // WARNING: depending on Q, pool elements may need to be initialized  

        return 1;
    }

    // insert method, it never fails if data is not NULL
    inline bool push(void * const data) {
        register long q = atomic_long_inc_return(&enqueue) % pool.size();
        bool r = pool[q].push(data);
        if (r) atomic_long_inc(&count);
        return r;
    }

    // extract method, it returns false if the queue is empty
    inline bool  pop(void ** data) {      
        if (!atomic_long_read(&count))  return false; // empty
#if !defined(MULTI_MPMC_RELAX_FIFO_ORDERING)
        //
        // enforce FIFO ordering for the consumers
        //
        register long q, q1;
        do {
            q  = atomic_long_read(&dequeue), q1 = atomic_long_read(&enqueue);            
            if (q > q1) return false;
            if (CAS((volatile atom_t *)&dequeue, (atom_t)(q+1), (atom_t)q) == (atom_t)q) break;
        } while(1);

        q %= pool.size(); 
        do ; while( !(pool[q].pop(data)) );
        atomic_long_dec(&count); 
        return true;
        
#else  // MULTI_MPMC_RELAX_FIFO_ORDERING
        register long q = atomic_long_inc_return(&dequeue) % pool.size();
        bool r = pool[q].pop(data);
        if (r) { atomic_long_dec(&count); return true;}
        return false;
#endif        
    }
    
    // check if the queue is empty
    inline bool empty() {
        for(size_t i=0;i<pool.size();++i)
            if (!pool[i].empty()) return false;
         return true;
    }
private:
    atomic_long_t enqueue;
    long padding1[longxCacheLine-sizeof(atomic_long_t)];
    atomic_long_t dequeue;
    long padding2[longxCacheLine-sizeof(atomic_long_t)];
    atomic_long_t count;
    long padding3[longxCacheLine-sizeof(atomic_long_t)];
protected:
    std::vector<Q> pool;
};

/* 
 * multiMSqueue is a specialization of the scalableMPMCqueue which uses the MSqueue 
*/
class multiMSqueue: public scalableMPMCqueue<MSqueue> {
public:

    multiMSqueue(size_t poolsize = scalableMPMCqueue<MSqueue>::DEFAULT_POOL_SIZE) {
        if (! scalableMPMCqueue<MSqueue>::init(poolsize)) {
            std::cerr << "multiMSqueue init ERROR, abort....\n";
            abort();
        }
        
        for(size_t i=0;i<poolsize;++i)
            if (pool[i].init()<0) {
                std::cerr << "ERROR initializing MSqueue, abort....\n";
                abort();
            }
    }
};








/* ---------------------- MaX experimental code -------------------------- */

/*
 *
 *   bool push(T)
 *   bool pop(T&)
 *   bool empty() 
 *
 *
 */
    typedef struct{
        unsigned long data;
        unsigned long next;        
        long padding1[64-2*sizeof(unsigned long)];
    }utMPMC_list_node_t;

    typedef struct{
        /*HEAD*/
        utMPMC_list_node_t* head;
        long padding0[64-sizeof(unsigned long)];
        /*TAIL*/
        utMPMC_list_node_t* tail;        
        long padding1[64-sizeof(unsigned long)];
    }utMPMC_list_info_t;

    typedef struct{
        /*address*/
        utMPMC_list_info_t l; 
        /*status*/
        unsigned long s;       
        long padding0[64-sizeof(unsigned long)]; 
    }utMPMC_VB_note_t;

#if !defined(NEXT_SMALLEST_2_POW)
#define NEXT_SMALLEST_2_POW(A) (1 << (32 - __builtin_clz((A)-1)))
#endif

#if !defined(VOLATILE_READ)
#define VOLATILE_READ(X)  (*(volatile typeof(X)*)&X)

#if !defined(OPTIMIZED_MOD_ON_2_POW)
#define OPTIMIZED_MOD_ON_2_POW(X,Y) ((X) & (Y))
#endif

#define IS_WRITABLE(STATUS,MYEQC) (STATUS==MYEQC)
#define WRITABLE_STATUS(STATUS,MYEQC) (MYEQC)
#define UPDATE_AFTER_WRITE(STATUS) (STATUS+1)

#define IS_READABLE(STATUS,MYDQC) (STATUS==MYDQC+1)
#define READABLE_STATUS(STATUS,MYDQC) (MYDQC+1)
#define UPDATE_AFTER_READ(STATUS,LEN) (STATUS+LEN-1)
#endif

    template <typename Q>
    class utMPMC_VB {
    public:
        enum {DEFAULT_POOL_SIZE=4};

        utMPMC_VB() {
            dqc =0;
            eqc = 0;
            /*
             * Both push and pop start from index 0
             */
            dqc = 0;
            eqc = 0;
        }
    
        int init(size_t vector_len) {
   
            len_v = NEXT_SMALLEST_2_POW(vector_len);
            len_v_minus_one = len_v-1;
            /*
             * Allocation and Init of the Vector
             */
            int done = posix_memalign((void **) v, longxCacheLine,
                                      sizeof(utMPMC_VB_note_t) * len_v);
            if (done != 0) {
                return 0;
            }
            int i = 0;
            for (i = 0; i < len_v; i++) {
                v[i].s = i;
                utMPMC_list_node_t * new_node;
                do{new_node = malloc (sizeof(utMPMC_list_node_t));}while(new_node);
                new_node->data=NULL;
                new_node->next=NULL;
                v[i].l.tail=new_node;
                v[i].l.head=new_node;
            }

            return 1;
        }

    // insert method, it never fails!!
    inline bool push(void * const p) {
        utMPMC_list_node_t * new_node;
        do{new_node = malloc (sizeof(utMPMC_list_node_t));}while(new_node);
        new_node->data=p;
        new_node->next=NULL;

		unsigned long myEQC = __sync_fetch_and_add (&eqc, 1UL);;
		unsigned long myI = OPTIMIZED_MOD_ON_2_POW(myEQC, len_v_minus_one);

		unsigned long target_status = WRITABLE_STATUS(target_status, myEQC);
		do{}while(VOLATILE_READ(v[myI].s) != target_status);

        /* List Stuff TODO*/
        v[myI].l.tail->next = new_node;
        v[myI].l.tail = new_node;
        target_status = UPDATE_AFTER_WRITE(target_status);
        /*barrier*/
        __sync_synchronize();
        v[myI].s = target_status;
        
		return true;
    }

    // extract method, it returns false if the queue is empty
    inline bool  pop(void ** ret_val) {      
        	for (;;) {
		unsigned long myDQC = VOLATILE_READ(dqc);
		unsigned long myI = OPTIMIZED_MOD_ON_2_POW(myDQC, len_v_minus_one);
		unsigned long target_status = v[myI].s;


		if (IS_READABLE(target_status,myDQC) && (v[myI].l.tail!=v[myI].l.head)) {
			int atomic_result = __sync_bool_compare_and_swap(&dqc, myDQC,
					myDQC + 1);
			if (atomic_result) {
				/*
				 * that is my lucky day!! I've fished something...
				 */
                utMPMC_list_node_t* to_be_remoed =  v[myI].l.head;
                /* First Advance */
                v[myI].l.head = v[myI].l.head->next;
                /* Secondly Extract elem */
                *ret_val = v[myI].l.head->data;
                /* update the rest */
				target_status = UPDATE_AFTER_READ(target_status,len_v);
                __sync_synchronize();                
				v[myI].s = target_status;
                free(to_be_remoed);
				return true;
			} else {
				continue;
			}
		} else {
			/*
			 * Check if someone changed the card while I was playing
			 */
			if (myDQC != VOLATILE_READ(dqc)) {
				continue;
			}
			if (VOLATILE_READ(eqc) != VOLATILE_READ(dqc)) {
				continue;
			}
			/*
			 * Sorry.. no space for you...
			 */
			return false;
		}
	}
	/*
	 * Impossible to reach this point!!!
	 */
	return true;
    }
    
//     inline bool empty() {
//         for(size_t i=0;i<pool.size();++i)
//             if (!pool[i].empty()) return false;
//          return true;
//     }
private:
        long padding0[64 - sizeof(unsigned long)];
        unsigned long eqc;
        long padding1[64 - sizeof(unsigned long)];
        unsigned long dqc;
        long padding2[64 - sizeof(unsigned long)];
        unsigned long len_v;
        unsigned long len_v_minus_one;
        utMPMC_VB_note_t * v;    
        long padding3[64 - 3*sizeof(unsigned long)];
    };

// /* 
//  * multiMSqueue is a specialization of the scalableMPMCqueue which uses the MSqueue 
// */
// class multiMSqueue: public scalableMPMCqueue<MSqueue> {
// public:

//     multiMSqueue(size_t poolsize = scalableMPMCqueue<MSqueue>::DEFAULT_POOL_SIZE) {
//         if (! scalableMPMCqueue<MSqueue>::init(poolsize)) {
//             std::cerr << "multiMSqueue init ERROR, abort....\n";
//             abort();
//         }
        
//         for(size_t i=0;i<poolsize;++i)
//             if (pool[i].init()<0) {
//                 std::cerr << "ERROR initializing MSqueue, abort....\n";
//                 abort();
//             }
//     }
// };


} // namespace

#endif /* __FF_MPMCQUEUE_HPP_ */
