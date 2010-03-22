/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _FF_SQUEUE_HPP_
#define _FF_SQUEUE_HPP_
/* ***************************************************************************
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  As a special exception, you may use this file as part of a free software
 *  library without restriction.  Specifically, if other files instantiate
 *  templates or use macros or inline functions from this file, or you compile
 *  this file and link it with other files to produce an executable, this
 *  file does not by itself cause the resulting executable to be covered by
 *  the GNU General Public License.  This exception does not however
 *  invalidate any other reasons why the executable file might be covered by
 *  the GNU General Public License.
 *
 ****************************************************************************
 */


/* Simple yet efficient unbounded FIFO queue.
 *
 */
#include <stdlib.h>

namespace ff {
        
template <typename T>
class squeue {
private:
    
    struct data_type {
        data_type():h(-1),t(-1),entry(0) {};
        data_type(int h, int t, T * entry):h(h),t(t),entry(entry) {}
        data_type(const data_type & de):h(de.h),t(de.t),entry(de.entry) {}
        int     h;
        int     t;
        T     * entry;
    };


    typedef T          elem_type;
    
protected:
    enum {DATA_CHUNK=1024, SQUEUE_CHUNK=4096};
    
    inline T * newchunk() {
        T * v =(T *)malloc(chunk*sizeof(T));
        return v;
    }
    
    inline void deletechunk(int idx) { 
        free(data[idx].entry);   
        data[idx].entry = NULL;
    }

    
public:
    
    squeue(size_t chunk=SQUEUE_CHUNK):data(0),datacap(DATA_CHUNK),
                                      nelements(0),
                                      head(0),tail(0),chunk(chunk)  {
        data = (data_type *)malloc(datacap*sizeof(data_type));
        data[0] = data_type(0, -1, newchunk());
    }
    
    ~squeue() {
        if (!data) return;
        for(unsigned int i=0;i<=tail;++i)
            if (data[i].entry) free(data[i].entry);
        free(data);
    }
    
    /* enqueue operation */
    inline void push_back(const elem_type & elem) {
        T * current       = data[tail].entry;
        int current_tail  = data[tail].t++;
        if ((unsigned)current_tail == (chunk-1)) {
            if (tail == (datacap-1)) {
                datacap += DATA_CHUNK;
                data = (data_type *)realloc(data, datacap*sizeof(data_type));
            }

            T * v = newchunk();
            data[++tail] = data_type(0,0,v);
            current = v;
            current_tail=-1;
        }
        current[current_tail+1] = elem;
        ++nelements;
    }

    /* dequeue one element from the back */
    inline void pop_back() { 
        if (!nelements) return;
        
        T * current        = data[tail].entry;
        int current_tail   = data[tail].t--;
        
        current[current_tail].~T();
                         
        --nelements;
        if (!current_tail && (tail>0)) {
            deletechunk(tail);
            --tail;
            data[tail].t = chunk-1;
        }
        
    }
    
    inline elem_type& back() { 
        if (!nelements) return *(elem_type*)0;

        T * current       = data[tail].entry;
        int current_tail  = data[tail].t;
        return current[current_tail];
    }

    /* dequeue one element from the head */
    inline void pop_front() { 
        if (!nelements) return;

        T * current      = data[head].entry;
        int current_head = data[head].h++;
        
        current[current_head].~T();

        --nelements;
        if (((unsigned)current_head==(chunk-1)) && (tail>head)) {
            deletechunk(head);
            ++head;
        }        
    }
    
    inline elem_type& front() { 
        if (!nelements) return *(elem_type*)0;

        T * current       = data[head].entry;
        int current_head  = data[head].h;
        return current[current_head];
    }

    /* return the number of items in the queue */
    inline size_t size() const { return nelements; }
    
    
private:
    data_type    * data;
    size_t         datacap;  
    size_t         nelements;
    unsigned int   head;  
    unsigned int   tail;
    size_t         chunk;    
};

}

#endif /* _FF_SQUEUE_HPP_ */

#if 0

#include <iostream>
#include <deque>
#include <squeue.hpp>

int main() {
    std::deque<int> e;

    ffTime(START_TIME);
    for(int i=0;i<200000;++i) e.push_back(i);
    while(e.size()) {
        //std::cout << d.back() << " ";
        e.pop_back();
    }
    ffTime(STOP_TIME);
    std::cerr << "DONE deque, time= " << ffTime(GET_TIME) << " (ms)\n";
    
    
    squeue<int> d;

    ffTime(START_TIME);
    for(int i=0;i<200000;++i) d.push_back(i);
    while(d.size()) {
        //std::cout << d.back() << " ";
        d.pop_back();
    }
    ffTime(STOP_TIME);
    std::cerr << "DONE squeue, time= " << ffTime(GET_TIME) << " (ms)\n";
    
    
    for(int i=0;i<1500;++i) d.push_back(i);    
    for(int i=0;i<500;++i) {
        std::cout << d.back() << " ";
        d.pop_back();
    }    
    while(d.size()) {
        std::cout << d.front() << " ";
        d.pop_front();
    }
    std::cerr << "size= " << d.size() << "\n";    
    return 0;
}

#endif 