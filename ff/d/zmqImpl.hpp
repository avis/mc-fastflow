/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _FF_zmqIMPL_HPP_
#define _FF_zmqIMPL_HPP_
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

#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include <assert.h>

#include <zmq.hpp>
#include <ff/svector.hpp>
#include <ff/d/zmqTransport.hpp>

namespace ff {

#define CONNECTION_PROTOCOL 1
//#undef CONNECTION_PROTOCOL
#if defined(DEBUG)
 #define zmqTDBG(x) x
#else
 #define zmqTDBG(x)
#endif

/* ---------------- Pattern macro definitions -------------------- */

#define UNICAST                          commPattern<zmq1_1>
#define UNICAST_DESC(name,trasp,P)       UNICAST::descriptor(name,1,trasp,P)
#define BROADCAST                        commPattern<zmqBcast>
#define BROADCAST_DESC(name,n,trasp,P)   BROADCAST::descriptor(name,n,trasp,P)
#define ALLGATHER                        commPattern<zmqAllGather>
#define ALLGATHER_DESC(name,n,trasp,P)   ALLGATHER::descriptor(name,n,trasp,P)
#define FROMANY                          commPattern<zmqFromAny>
#define FROMANY_DESC(name,n,trasp,P)     FROMANY::descriptor(name,n,trasp,P)
#define SCATTER                          commPattern<zmqScatter>
#define SCATTER_DESC(name,n,trasp,P)     SCATTER::descriptor(name,n,trasp,P)
#define ONETOMANY                        commPattern<zmq1_N>
#define ONETOMANY_DESC(name,n,trasp,P)   ONETOMANY::descriptor(name,n,trasp,P)
#define ONDEMAND                         commPattern<zmqOnDemand>
#define ONDEMAND_DESC(name,n,trasp,P)    ONDEMAND::descriptor(name,n,trasp,P)

/* TODO */
#define MANYTOONE                        commPattern<zmqN_1>
#define MANYTOONE_DESC(name,n,trasp,P)   MANYTOONE::descriptor(name,n,trasp,P)

/* --------------------------------------------------------------- */

// used in Unicast, Bcast, Scatter, On-Demand, OneToMany
struct descriptor1_N {
    typedef zmqTransportMsg_t  msg_t;
    descriptor1_N(const std::string name, const int peers, zmqTransport* const  transport, const bool P):
        name(name), socket(NULL),transport(transport),P(P),peers(peers),zmqHdrSet(false),transportIDs(peers) {
        if (transport) 
            socket = transport->newEndPoint(!P); // NOTE: we want to use ZMQ_ROUTER at producer side
        
        if (P) {
            std::stringstream identity;
            for(int i=0;i<peers;++i) {
                identity.str("");
                identity << name << ":" << i;
                transportIDs[i]=identity.str();
            }
        }
    }
    ~descriptor1_N() { close(); socket=NULL; }
    
    inline int close() {
        if (socket) return transport->deleteEndPoint(socket);
        return -1;
    }
    
    // @param addr is the ip address in the form ip/host:port
    // nodeId is the node identifier in the range [0..inf[
    //
    inline int init(const std::string& addr, const int nodeId=-1) {
        if (!socket) return -1;
        
        if (!P) {
            std::stringstream identity;
            identity.str("");
            identity << name << ":" << ((nodeId!=-1)?nodeId:transport->getProcId());
            // set durable identity. it must be set before connecting the socket
            socket->setsockopt(ZMQ_IDENTITY, identity.str().c_str(), identity.str().length()+1);
            
            std::stringstream address;
            address.str("");
            address << "tcp://" << addr;
            socket->connect(address.str().c_str());
            
#if defined(CONNECTION_PROTOCOL)
            sendHelloRecvHi();
#endif
        } else {
            int i;
            if ((i=addr.find(":"))<0) {
                printf("init: ERROR wrong ip address format for %s\n",addr.c_str());
                return -1;
            }	    
            int port = atoi((addr.substr(i+1)).c_str());
            std::stringstream address;
            address << "tcp://*:" << port;
            try {
                socket->bind(address.str().c_str());
            } catch(std::exception& e) {
                printf("initTransport: ERROR binding address (%s): %s\n",
                       address.str().c_str(), e.what());
                return -1;
            }                
#if defined(CONNECTION_PROTOCOL)
            recvHelloSendHi();
#endif
        }
        return 0;
    }

    // broadcast
    inline bool send(const msg_t& msg, int flags = 0) {
        for(int i=0;i<peers;++i) {
            msg_t _msg;
            _msg.copy(const_cast<msg_t&>(msg));
            zmq::message_t to(const_cast<char*>(transportIDs[i].c_str()), transportIDs[i].length()+1,0,0);
            try {
                if (!zmqHdrSet) if (!socket->send(to,ZMQ_SNDMORE)) return false;
                if (!socket->send(_msg, flags)) return false;
            } catch (std::exception& e) {
                return false;
            }
        }
        zmqHdrSet=false;
        return true;    
    }
    
    // broadcast
    inline bool sendmore(const msg_t& msg, int flags) {
        assert(flags==ZMQ_SNDMORE);
        bool v=send(msg,flags);
        zmqHdrSet=true;
        return v;
    }

    // unicast
    inline bool send(const msg_t& msg, const int dest, int flags) {
        assert(dest<peers);
        zmq::message_t to(const_cast<char*>(transportIDs[dest].c_str()), transportIDs[dest].length()+1,0,0);
        try {
            if (!zmqHdrSet) if (!socket->send(to,ZMQ_SNDMORE)) return false;
            if (!socket->send(const_cast<msg_t&>(msg), flags)) return false;
        } catch (std::exception& e) {
            return false;
        }
        zmqHdrSet=false;
        return true;    
    }
    // unicast
    inline bool sendmore(const msg_t& msg, const int dest, int flags) {
        assert(flags==ZMQ_SNDMORE);
        bool v=send(msg,dest,flags);
        zmqHdrSet=true;
        return v;
    }
       
    // receives a message from a ZMQ_DEALER
    inline bool recvhdr(msg_t& msg) {   
        return recv(msg);
    }
    
    // receives a message after having received the header
    inline bool recv(msg_t& msg) {   
        try {
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recv RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
                return false;
        }
        return true;
        }
    
    // returns the total number of peers
    inline const int getPeers() const { return peers;}

    const std::string        name;
    zmq::socket_t*           socket;
    zmqTransport * const     transport;
    const bool               P;
    const int                peers;
    bool                     zmqHdrSet;
    std::vector<std::string> transportIDs;
    
#if defined(CONNECTION_PROTOCOL)
    inline void sendHelloRecvHi() {
        const char hello[] = "HELLO";	
        // sending Hello
        zmqTDBG(printf("%s sending HELLO\n", name.c_str()));
        zmqTransportMsg_t req(hello,6);
        sendreq(req);
        // receive Hi
        zmqTransportMsg_t msg;
        recvreply(msg);
        zmqTDBG(printf("%s received %s\n", name.c_str(), static_cast<char*>(msg.getData())));    
    }
    
    inline bool sendreq(const msg_t& msg) {
        try {
            if (!socket->send(const_cast<msg_t&>(msg))) return false;
        } catch (std::exception& e) {
            return false;
        }
        return true;
    }
    
    inline int recvreply(msg_t& msg) {   
        try {
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recv RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;    
    }
    
    // receives a message from a ZMQ_ROUTER
    inline bool recvreq(msg_t& msg) {   
        msg_t from;
        for(int i=0;i<peers;++i) {
            try {
                if(!socket->recv(&from)) { 
                    zmqTDBG(printf("recvreq RETURN -1\n"));
                    return false;
                }
                if(!socket->recv(&msg)) { 
                    zmqTDBG(printf("recvreq RETURN -1\n"));
                    return false;
                }
                zmqTDBG(printf("%s received from %s %s\n",name.c_str(), 
                               static_cast<char*>(from.getData()), 
                               static_cast<char*>(msg.getData())));
            } catch (std::exception & e) {
                return false;
            }
        }
        return true;
    }    

    // In the on-demand pattern it is used by the producer (i.e. the on-demand consumer)
    inline bool sendReq(const msg_t& msg) {
        return sendreq(msg);
    }
    // In the on-demand pattern it is used by the consumer (i.e. the on-demand producer)
    // \param flags can be ZMQ_NOBLOCK
    inline bool recvReq(int& peer, int flags=0) {
        zmq::message_t from;
        zmq::message_t msg;
        try {
            if(!socket->recv(&from, flags)) return false;
            const std::string & f=static_cast<char*>(from.data());
            int i;
            if ((i=f.find(":"))<0) {
                printf("recvhdr: ERROR: wrong header\n");
                return false;
            }	    
            peer = atoi((f.substr(i+1)).c_str());
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recvreq RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;
    }

    inline bool sendreply(const msg_t& msg) {
        for(int i=0;i<peers;++i) {
            msg_t _msg;
            _msg.copy(const_cast<msg_t&>(msg));
            zmq::message_t to(const_cast<char*>(transportIDs[i].c_str()), transportIDs[i].length()+1,0,0);
            try {
                if (!socket->send(to,ZMQ_SNDMORE)) return false;
                if (!socket->send(_msg)) return false;
            } catch (std::exception& e) {
                return false;
            }
        }
        return true;    
    }
    
    inline void recvHelloSendHi() {
        static const char hi[] = "HI"; // need to be static
        // receives Hello and sends Hi
        zmqTransportMsg_t req;
        recvreq(req); 
        zmqTransportMsg_t rep(hi,3);
        send(rep);
        zmqTDBG(printf("%s sending HI\n", name.c_str()));    
    }
#endif
};

// used in ALL gather, collect from ANY, ManyToOne
struct descriptorN_1 {
    typedef zmqTransportMsg_t  msg_t;
    descriptorN_1(const std::string name, const int peers, zmqTransport* const  transport, const bool P):
        name(name), socket(NULL),transport(transport),P(P),peers(peers),recvHdr(true), transportIDs(peers) {
        if (transport) socket = transport->newEndPoint(P);
        
        if (!P) {
            std::stringstream identity;
            for(int i=0;i<peers;++i) {
                identity.str("");
                identity << name << ":" << i;
                transportIDs[i]=identity.str();
            }
        }
    }
    ~descriptorN_1() { close(); socket=NULL; }
    
    inline int close() {
        if (socket) return transport->deleteEndPoint(socket);
        return -1;
    }
    
    // @param addr is the ip address in the form ip/host:port
    // nodeId is the node identifier in the range [0..inf[
    //
    inline int init(const std::string& addr, const int nodeId=-1) {
        if (!socket) return -1;
        
        if (P) {
            std::stringstream identity;
            identity.str("");
            identity << name << ":" << ((nodeId!=-1)?nodeId:transport->getProcId());
            // set durable identity. it must be set before connecting the socket
            socket->setsockopt(ZMQ_IDENTITY, identity.str().c_str(), identity.str().length()+1);
            
            std::stringstream address;
            address.str("");
            address << "tcp://" << addr;
            socket->connect(address.str().c_str());
            
#if defined(CONNECTION_PROTOCOL)
            sendHelloRecvHi();
#endif
        } else {
            int i;
            if ((i=addr.find(":"))<0) {
                printf("init: ERROR wrong ip address format for %s\n",addr.c_str());
                return -1;
            }	    
            int port = atoi((addr.substr(i+1)).c_str());
            std::stringstream address;
            address << "tcp://*:" << port;
            try {
                socket->bind(address.str().c_str());
            } catch(std::exception& e) {
                printf("initTransport: ERROR binding address (%s): %s\n",
                       address.str().c_str(), e.what());
                return -1;
            }                
#if defined(CONNECTION_PROTOCOL)
            recvHelloSendHi();
#endif
        }
        return 0;
    }
    
    inline bool send(const msg_t& msg, int flags = 0) {
        try {
            if (!socket->send(const_cast<msg_t&>(msg), flags)) return false;
        } catch (std::exception& e) {
            return false;
        }
        return true;            
    }
    
    // receives a message from a ZMQ_ROUTER
    inline bool recvhdr(msg_t& msg, int& peer) {   
        zmq::message_t from;
        try {
            if(!socket->recv(&from)) { 
                zmqTDBG(printf("recvhdr RETURN -1\n"));
                return false;
            }
            const std::string & f=static_cast<char*>(from.data());
            int i;
            if ((i=f.find(":"))<0) {
                printf("recvhdr: ERROR: wrong header\n");
                return false;
            }	    
            peer = atoi((f.substr(i+1)).c_str());
            recvHdr = false;
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recvhdr RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;
    }
    
    // receives a message after having received the header
    inline bool recv(msg_t& msg, int& peer) {   
        try {
            if (recvHdr) {
                zmq::message_t from;
                if(!socket->recv(&from)) { 
                    zmqTDBG(printf("recv RETURN -1\n"));
                    return false;
                }
                
                const std::string & f=static_cast<char*>(from.data());
                int i;
                if ((i=f.find(":"))<0) {
                    printf("recvhdr: ERROR: wrong header\n");
                    return false;
                    }	    
                peer = atoi((f.substr(i+1)).c_str());
            }
            
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recv RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;
    }
    
    inline bool recvreq() {
        try {
            msg_t useless;
            if(!socket->recv(&useless)) { 
                zmqTDBG(printf("recvreq RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;
    }
    
    // reset recvHdr
    inline bool done(bool sendready=false) {
        if (sendready) {
            static const char ready[] = "READY";	
            for(int i=0;i<peers;++i) {
                msg_t msg(ready,6);
                zmq::message_t to(const_cast<char*>(transportIDs[i].c_str()), transportIDs[i].length()+1,0,0);
                try {
                    if (!socket->send(to,ZMQ_SNDMORE)) return false;
                    if (!socket->send(msg)) return false;
                } catch (std::exception& e) {
                    return false;
                }
            }
        }
        recvHdr=true;
        return true;    
    }
    
    // returns the number of partners of the single communication
    inline const int getPeers() const { return peers;}
    
    const std::string        name;
    zmq::socket_t*           socket;
    zmqTransport * const     transport;
    const bool               P;
    const int                peers;
    bool                     recvHdr;
    std::vector<std::string> transportIDs;
    
#if defined(CONNECTION_PROTOCOL)
    inline void sendHelloRecvHi() {
        const char hello[] = "HELLO";	
        // sending Hello
        zmqTDBG(printf("%s sending HELLO\n", name.c_str()));
        zmqTransportMsg_t req(hello,6);
        send(req);
        // receive Hi
        zmqTransportMsg_t msg;
        recvreply(msg);
        zmqTDBG(printf("%s received %s\n", name.c_str(), static_cast<char*>(msg.getData())));    
    }
    
    inline bool recvreply(msg_t& msg) {
        try {
            if(!socket->recv(&msg)) { 
                zmqTDBG(printf("recv RETURN -1\n"));
                return false;
            }
        } catch (std::exception & e) {
            return false;
        }
        return true;
    }
    
    inline bool sendreply(const msg_t& msg) {
        for(int i=0;i<peers;++i) {
            msg_t _msg;
            _msg.copy(const_cast<msg_t&>(msg));
            zmq::message_t to(const_cast<char*>(transportIDs[i].c_str()), transportIDs[i].length()+1,0,0);
            try {
                if (!socket->send(to,ZMQ_SNDMORE)) return false;
                if (!socket->send(_msg)) return false;
            } catch (std::exception& e) {
                return false;
            }
        }
        return true;    
    }
    
    inline void recvHelloSendHi() {
        static const char hi[] = "HI"; // need to be static
        // receives Hello and sends Hi
        int useless;
        for(int i=0;i<peers;++i) {
            zmqTransportMsg_t req;
            recv(req,useless);
            zmqTDBG(printf("%s received %s\n",name.c_str(), static_cast<char*>(req.getData())));
        }
        zmqTransportMsg_t rep(hi,3);
        sendreply(rep);
        zmqTDBG(printf("%s sending HI\n", name.c_str()));    
    }
#endif
};

/* --------------------------------------------------------------- */

// 
//  ZeroMQ implementation of the 1 to 1 communication patter
//
class zmq1_1 {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef msg_t              tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptor1_N      descriptor;
    //typedef descriptorN_1      descriptor;

    zmq1_1():desc(NULL),active(false) {}

    // point to point communication implemented using OMQ
    zmq1_1(descriptor* D):desc(D),active(false) {}

    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }

    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }

    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        // we force 0 to be the nodeId for the consumer
        if(!desc->init(address,(desc->P)?nodeId:0)) active = true; 
        return active;
    }

    // sends one message 
    inline bool put(const tosend_t& msg) { return desc->send(msg, 0, 0); }
    
    inline bool putmore(const tosend_t& msg) { return desc->sendmore(msg,0,ZMQ_SNDMORE);}

    inline bool put(const msg_t& msg, const int) { return desc->send(msg, 0, 0); }

    // receives the message header (should be called before get)
    inline bool gethdr(torecv_t& msg, int& peer) { peer=0; return desc->recvhdr(msg); }

    // receives one message
    inline bool get(torecv_t& msg, int=0) { return desc->recv(msg); }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return 1;}
    inline const int putToPerform() const { return 1;}

    inline void done() { }

    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }

protected:
    descriptor* desc;
    bool        active;
};

// 
//  ZeroMQ implementation of the broadcast communication patter
//
class zmqBcast {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef msg_t              tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptor1_N      descriptor;

    zmqBcast():desc(NULL),active(false) {}
    zmqBcast(descriptor* D):desc(D),active(false) {}

    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }

    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }

    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) active = true;       
        return active;
    }

    // sends one message 
    inline bool put(const tosend_t& msg) { return desc->send(msg, 0); }
    
    inline bool putmore(const tosend_t& msg) { return desc->sendmore(msg,ZMQ_SNDMORE);}


    inline bool put(const msg_t& msg, const int to) { 
        return desc->send(msg, to, 0); 
    }

    inline bool putmore(const msg_t& msg, const int to) { 
        return desc->sendmore(msg,to,ZMQ_SNDMORE);
    }

    // receives the message header (should be called before get)
    inline bool gethdr(torecv_t& msg, int& peer) { peer=0; return desc->recvhdr(msg); }

    // receives one message
    inline bool get(torecv_t& msg, int=0) { return desc->recv(msg); }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return 1;}
    inline const int putToPerform() const { return desc->getPeers();}

    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }

    inline void done() {}

protected:
    descriptor* desc;
    bool        active;
};
    
    
// 
//  ZeroMQ implementation of the ALL_GATHER communication patter
//
class zmqAllGather {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef msg_t              tosend_t;
    typedef svector<msg_t>     torecv_t;
    typedef descriptorN_1      descriptor;


    zmqAllGather():desc(NULL),active(false) {}
    zmqAllGather(descriptor* D):desc(D),active(false) {}
    
    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }
    
    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }
    
    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) active = true;       
        if (!desc->P) done();
        return active;
    }
    
    // sends one message 
    inline bool put(const tosend_t& msg) { 
        if (!desc->recvreq()) return false;
        return desc->send(msg, 0); 
    }
    
    inline bool putmore(const tosend_t& msg) { return desc->send(msg,ZMQ_SNDMORE);}
    
    // receives the message header ONLY from one peer (should be called before get)
    inline bool gethdr(msg_t& msg, int& peer) { 
        return desc->recvhdr(msg, peer);
    }
    // receives one message ONLY from one peer (gethdr should be called before)
    inline bool get(msg_t& msg, int& peer) {
        return desc->recv(msg,peer);
    }
    // receives one message
    inline bool get(torecv_t& msg) { 
        const int peers=desc->getPeers();
        int useless;
        msg.resize(peers);
        for(int i=0;i<peers;++i)
            if (!desc->recv(msg[i],useless)) return false;        
        done();
        return true;
    }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return desc->getPeers();}
    inline const int putToPerform() const { return 1;}

    inline void done() { desc->done(true); }
    
    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (desc->P) if (!desc->recvreq()) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }
        
protected:
    descriptor* desc;
    bool        active;
};

    
// 
//  ZeroMQ implementation of the collect from ANY communication patter
//
class zmqFromAny {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef msg_t              tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptorN_1      descriptor;

    zmqFromAny():desc(NULL),active(false) {}
    zmqFromAny(descriptor* D):desc(D),active(false) {}
    
    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }
    
    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }
    
    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) active = true;       
        return active;
    }
    
    // sends one message 
    inline bool put(const tosend_t& msg) {
        return desc->send(msg, 0); 
    }
    
    inline bool putmore(const tosend_t& msg) { 
        return desc->send(msg,ZMQ_SNDMORE);
    }
    
    inline bool put(const msg_t& msg, const int) {
        return desc->send(msg, 0); 
    }

    // receives the message header (should be called before get)
    inline bool gethdr(msg_t& msg, int& peer) { 
        return desc->recvhdr(msg,peer);
    }
    
    // receives one message
    inline bool get(msg_t& msg, int& peer) { 
        return desc->recv(msg,peer);
    }
    // receives one message
    inline bool get(msg_t& msg) { 
        int useless=0;
        return desc->recv(msg,useless);
    }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return 1;}
    inline const int putToPerform() const { return 1;}

    inline void done() { desc->done(); }
    
    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }
        
protected:
    descriptor* desc;
    bool        active;
};


// 
//  ZeroMQ implementation of the scatter communication patter
//
class zmqScatter {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef svector<msg_t>     tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptor1_N      descriptor;

    zmqScatter():desc(NULL),active(false) {}
    zmqScatter(descriptor* D):desc(D),active(false) {}

    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }

    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }

    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) active = true;       
        return active;
    }

    // sends one message 
    inline bool put(const tosend_t& msg) { 
        const int peers=desc->getPeers();
        assert(msg.size()==(size_t)peers);
        for(int i=0;i<peers;++i) 
            if (!desc->send(msg[i], i, 0)) return false;
        return true;
    }
    
    inline bool put(const msg_t& msg, const int to) { 
        return desc->send(msg, to, 0); 
    }

    inline bool putmore(const msg_t& msg, const int to) { 
        return desc->sendmore(msg,to,ZMQ_SNDMORE);
    }

    // receives the message header (should be called before get)
    inline bool gethdr(torecv_t& msg, int& peer) { peer=0; return desc->recvhdr(msg); }

    // receives one message
    inline bool get(torecv_t& msg, int=0) { return desc->recv(msg); }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return 1;}
    inline const int putToPerform() const { return desc->getPeers();}

    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }

    inline void done() {}

protected:
    descriptor* desc;
    bool        active;
};


// 
//  ZeroMQ implementation of the On-Demand communication patter
//
class zmqOnDemand {
protected:
    inline bool sendReq() {
        static const char ready[] = "R";
        msg_t request(ready,2);
        if (!desc->sendReq(request)) return false;
        requestSent=true;
        return true;
    }

public:
    typedef zmqTransportMsg_t  msg_t;
    typedef zmqTransport       TransportImpl;
    typedef msg_t              tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptor1_N      descriptor;

    zmqOnDemand():desc(NULL),active(false),to(-1),requestsToWait(0),requestSent(false) {}
    zmqOnDemand(descriptor* D):desc(D),active(false),to(-1),requestsToWait(0),requestSent(false) {}

    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }

    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }

    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) {
            if (!desc->P) if (!sendReq()) return false;
            active = true;   
        }

        To.resize(desc->getPeers());
        for(int i=0;i<desc->getPeers();++i) To[i]=false;

        return active;
    }

    // sends one message 
    inline bool put(const tosend_t& msg) { 
        // FIX: It would be better to use a non-blocking calls to receive requests, and
        //      in case recvReq returns false the msg should be inserted into a local queue.
        //      It is required an extra threads for handling incoming requests.....
        if (to<0) if (!desc->recvReq(to)) return false;
        int r = desc->send(msg, to, 0); 
        to=-1;
        return r;
    }
    // sends one message, more messages have to come
    inline bool putmore(const tosend_t& msg) { 
        // see FIX comment above
        if (to<0) if (!desc->recvReq(to)) return false;
        return desc->sendmore(msg, to, ZMQ_SNDMORE);
    }

    inline bool put(const tosend_t& msg, const int dest) {
        if (dest<0) {  // sends the same message to all peers, usefull for termination
            for(int i=0;i<desc->getPeers();++i) {
                to = i;
                tosend_t _msg;
                _msg.copy(const_cast<msg_t&>(msg));
                if (!desc->send(_msg, to, 0)) { to=-1; return false;}
                ++requestsToWait;
            }
            return true;
        }
        if (To[dest]) {
            if (!desc->send(msg, dest, 0)) { to=-1; return false;}
            To[dest]=false;
            return true;
        }
        do {
            if (!desc->recvReq(to)) { to=-1; return false;}
            if (to == dest) {
                if (!desc->send(msg, to, 0)) { to=-1; return false;}
                return true;
            }
            assert(To[to]==false);
            To[to]=true;
        } while(1);

        // not reached
        return false;
    }

    // receives the message header (should be called before get)
    inline bool gethdr(torecv_t& msg, int& peer) { 
        peer=0; 
        bool r = desc->recvhdr(msg); 
        if (r) sendReq();
        return r;
    }

    // receives one message part
    inline bool get(torecv_t& msg) { 
        bool r=desc->recv(msg); 
        if (!requestSent && r) sendReq();
        return r;
    }

    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (desc->P) {
            int useless;
            requestsToWait+=desc->getPeers();
            for(int i=0;i<desc->getPeers();++i)
                if (To[i]) --requestsToWait;
            for(int i=0;i<requestsToWait;++i) {
                if (!desc->recvReq(useless)) return false;
            }
        } 
        if (!desc->close()) return false;
        active=false;
        return true;
    }

    // returns the number of peers for one single communication
    inline const int getToWait() const { return 1;}
    inline const int putToPerform() const { return 1;}

    inline void done() { requestSent=false; }

protected:
    descriptor*   desc;
    bool          active;
    int           to;
    int           requestsToWait;
    bool          requestSent;
    svector<bool> To;
};



// 
//  ZeroMQ implementation of the one to many communication patter. This pattern can
//  be used to dynamically change among UNICAST, BROADCAST and SCATTER patterns.
//
//  ***TO BE COMPLETED AND TESTED***
//
class zmq1_N {
public:
    typedef zmqTransportMsg_t  msg_t;
    typedef svector<msg_t>     tosend_t;
    typedef msg_t              torecv_t;
    typedef descriptor1_N      descriptor;
    
    zmq1_N():desc(NULL),active(false) {}    
    zmq1_N(descriptor* D):desc(D),active(false) {}
    
    // sets the descriptor
    inline void setDescriptor(descriptor* D) { 
        if (desc)  desc->close(); 
        desc = D; 
    }
    
    // returns the descriptor
    inline  descriptor* getDescriptor() { return desc; }
    
    // initializes communication pattern
    inline bool init(const std::string& address,const int nodeId=-1) { 
        if (active) return false;
        if(!desc->init(address,nodeId)) active = true;       
        return active;
    }

    // sends one message 
    inline bool put(const tosend_t& msg) { 
        const int peers=desc->getPeers();
        assert(msg.size()==(size_t)peers);
        for(int i=0;i<peers;++i) 
            if (!desc->send(msg[i], i, 0)) return false;
        return true;
    }
    
    // sends one message.
    // if @param to is equal to -1 it sends the @param msg
    // in broadcast to all connected peers
    inline bool put(const msg_t& msg, const int to) { 
        return desc->send(msg, to, to); 
    }

    inline bool putmore(const msg_t& msg, const int to) { 
        return desc->sendmore(msg,to,ZMQ_SNDMORE);
    }

    // receives the message header (should be called before get)
    inline bool gethdr(torecv_t& msg, int& peer) { peer=0; return desc->recvhdr(msg); }

    // receives one message
    inline bool get(torecv_t& msg, int=0) { return desc->recv(msg); }

    // close pattern
    inline bool close() { 
        if (!active) return false;
        if (!desc->close()) return false;
        active=false;
        return true;
    }

    inline void done() {}

protected:
    descriptor* desc;
    bool        active;
};


}

#endif // _FF_zmqIMPL_HPP_
