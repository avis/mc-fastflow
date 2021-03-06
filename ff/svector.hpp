/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _FF_SVECTOR_HPP_
#define _FF_SVECTOR_HPP_
/* ***************************************************************************
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License version 3 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 ****************************************************************************
 */


/* Simple yet efficient dynamic vector */

#include <stdlib.h>
#include <new>

namespace ff {

template <typename T>
class svector {
    enum {SVECTOR_CHUNK=1024};
public:
    typedef T* iterator;
    typedef const T* const_iterator;
    typedef T vector_type;

    // NOTE: @param chunk is the allocation chunk and not the svector size
    svector(size_t chunk=SVECTOR_CHUNK):first(0),len(0),cap(0),chunk(chunk) {
        reserve(chunk);
    }
    svector(const svector & v):first(0),len(0),cap(0),chunk(v.chunk) {
        if(v.len) {
            const_iterator i1=v.begin();
            const_iterator i2=v.end();
            first=(vector_type*)::malloc((i2-i1)*sizeof(vector_type));
            while(i1!=i2) push_back(*(i1++));
        }
    }
    svector(const_iterator i1,const_iterator i2):first(0),len(0),cap(0),chunk(SVECTOR_CHUNK) {
        first=(vector_type*)::malloc((i2-i1)*sizeof(vector_type));
        while(i1!=i2) push_back(*(i1++));
    }
    
    ~svector() { 
        if(first) {  clear(); ::free(first); }  
    }
    
    svector& operator=(const svector & v) {
        len=0;
        if(!v.len) first=0; 
        else {
            const_iterator i1=v.begin();
            const_iterator i2=v.end();
            first=(vector_type*)::malloc((i2-i1)*sizeof(vector_type));
            while(i1!=i2) push_back(*(i1++));
        }
        return this;
    }
    inline void reserve(size_t newcapacity) {
        if(newcapacity<=cap) return;
        if (!first)
            first=(vector_type*)::malloc(sizeof(vector_type)*newcapacity);
        else 
            first=(vector_type*)::realloc(first,sizeof(vector_type)*newcapacity);
        cap = newcapacity;
    }
    
    inline void resize(size_t newsize) {
        if (len >= newsize) {
            while(len>newsize) pop_back();
            return;
        }
        reserve(newsize);
        while(len<newsize)
            new (first + len++) vector_type();
    }

    inline void push_back(const vector_type & elem) {
        if (len==cap) reserve(cap+chunk);	    
        new (first + len++) vector_type(elem); 
    }
    
    inline void pop_back() { (first + --len)->~vector_type();  }
    inline vector_type& back() { 
        return first[len-1]; 
        //return *(vector_type *)0;
    }
    inline iterator erase(iterator where) {
        iterator i1=begin();
        iterator i2=end();
        while(i1!=i2) {
            if (i1==where) { --len; break;}
            else i1++;
        }
        
        for(iterator i3=i1++; i1!=i2; ++i3, ++i1) 
            *i3 = *i1;
        
        return begin();
    }
    
    inline size_t size() const { return len; }
    inline bool   empty() const { return (len==0);}
    inline size_t capacity() const { return capacity;}
    
    inline void clear() { while(size()>0) pop_back(); }
    
    iterator begin() { return first; }
    
    iterator end() { return first+len; }
    
    const_iterator begin() const { return first; }
    
    const_iterator end() const { return first+len; }
    
    vector_type& operator[](size_t n) { 
        reserve(n+1);
        return first[n]; 
    }
    
    const vector_type& operator[](size_t n) const { return first[n]; }
    
private:
    vector_type * first;
    size_t        len;   
    size_t        cap;   
    size_t        chunk;
};

}

#endif /* _FF_SVECTOR_HPP_ */
