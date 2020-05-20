/**
 * @file mega/http.h
 * @brief Generic host HTTP I/O interfaces
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_HTTP_H
#define MEGA_HTTP_H 1

#include "types.h"
#include "waiter.h"
#include "backofftimer.h"
#include "utils.h"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

namespace mega {

#ifdef _WIN32
    const char* mega_inet_ntop(int af, const void* src, char* dst, int cnt);
#else
    #define mega_inet_ntop inet_ntop
#endif

// SSL public key pinning - active key
#define APISSLMODULUS1 "\xb6\x61\xe7\xcf\x69\x2a\x84\x35\x05\xc3\x14\xbc\x95\xcf\x94\x33\x1c\x82\x67\x3b\x04\x35\x11" \
"\xa0\x8d\xc8\x9d\xbb\x9c\x79\x65\xe7\x10\xd9\x91\x80\xc7\x81\x0c\xf4\x95\xbb\xb3\x26\x9b\x97\xd2" \
"\x14\x0f\x0b\xca\xf0\x5e\x45\x7b\x32\xc6\xa4\x7d\x7a\xfe\x11\xe7\xb2\x5e\x21\x55\x23\x22\x1a\xca" \
"\x1a\xf9\x21\xe1\x4e\xb7\x82\x0d\xeb\x9d\xcb\x4e\x3d\x0b\xe4\xed\x4a\xef\xe4\xab\x0c\xec\x09\x69" \
"\xfe\xae\x43\xec\x19\x04\x3d\x5b\x68\x0f\x67\xe8\x80\xff\x9b\x03\xea\x50\xab\x16\xd7\xe0\x4c\xb4" \
"\x42\xef\x31\xe2\x32\x9f\xe4\xd5\xf4\xd8\xfd\x82\xcc\xc4\x50\xd9\x4d\xb5\xfb\x6d\xa2\xf3\xaf\x37" \
"\x67\x7f\x96\x4c\x54\x3d\x9b\x1c\xbd\x5c\x31\x6d\x10\x43\xd8\x22\x21\x01\x87\x63\x22\x89\x17\xca" \
"\x92\xcb\xcb\xec\xe8\xc7\xff\x58\xe8\x18\xc4\xce\x1b\xe5\x4f\x20\xa8\xcf\xd3\xb9\x9d\x5a\x7a\x69" \
"\xf2\xca\x48\xf8\x87\x95\x3a\x32\x70\xb3\x1a\xf0\xc4\x45\x70\x43\x58\x18\xda\x85\x29\x1d\xaf\x83" \
"\xc2\x35\xa9\xc1\x73\x76\xb4\x47\x22\x2b\x42\x9f\x93\x72\x3f\x9d\x3d\xa1\x47\x3d\xb0\x46\x37\x1b" \
"\xfd\x0e\x28\x68\xa0\xf6\x1d\x62\xb2\xdc\x69\xc7\x9b\x09\x1e\xb5\x47"

// SSL public key pinning - backup key
#define APISSLMODULUS2 "\xaf\xe6\x13\x63\xe6\x24\x7c\x6b\x3c\xfe\x61\x91\x58\x20\xf5\xb9\x91\xdb\x86\x4c\x8e\x0c\x2f" \
"\xdb\x78\x31\xac\xba\x48\x03\xcf\x07\x95\xc6\x09\xda\x5b\xf9\x7b\x60\xa2\x87\xfe\xa9\xa5\xa2\x8a" \
"\x8a\x2c\xb1\x48\xa7\x8e\x66\x24\x0a\xc7\x38\xcf\xba\xdb\x77\x1d\x0b\xe9\xbe\x00\x54\x7f\xe9\x0e" \
"\x56\xbd\xcf\x7c\x10\xf5\xc2\x5f\xc2\x2e\x8f\xbf\x36\xfe\xe0\x5e\x18\xef\xcb\x2f\x88\x95\x4d\xe2" \
"\x72\xed\xfe\x60\x58\x7c\xdf\x75\xb1\x88\x27\xf4\x1c\x9f\xea\x83\x1f\xc6\x34\xa7\x54\x3d\x59\x9d" \
"\x43\xd9\x75\xf4\x17\xcf\x99\x63\x02\xfd\xad\x0f\xc2\x8d\xe7\x0a\xcc\x0c\xda\xac\x99\xc6\xd3\xf5" \
"\xef\xa2\x1f\xd6\xdc\xdb\x98\x63\x2a\xac\x00\x94\x5f\x42\x33\x46\xb6\x10\x86\xcd\x03\x92\xb0\x23" \
"\x2f\x86\x30\x53\xf8\x04\x92\x89\x2e\x0a\x25\x3f\xfa\x4c\x69\xd6\xd7\xaf\x62\xee\xd6\xec\xf8\x96" \
"\xaf\x53\x1a\x13\x33\x38\x7e\xe1\xa9\xe0\x3f\x43\x2f\x17\x05\x90\xe1\x42\xaa\x47\x6d\xef\xdf\x75" \
"\x2e\x3c\xfd\xcf\xbb\x0b\x31\x21\xab\x81\x57\x95\xd3\x04\xf9\x52\x69\x2e\x30\xe5\x45\x2d\x23\x5f" \
"\x6f\x26\x76\x69\x7a\x12\x99\x78\xe0\x08\x87\x33\xd6\x94\xf0\x6c\x6d"

// SSL public key pinning - chat key
#define CHATSSLMODULUS "\xbe\x75\xfe\xe1\xff\xac\x69\x2b\xc8\x0c\x12\xe9\x9f\x78\x60\xc2\xa0\xe1\xf1\xf2\xec\x48\xc5" \
"\x8b\xb0\x94\xe9\x68\x02\xdd\xde\xe5\xc3\x15\x53\x55\x44\xc6\x5f\x71\xb3\xe5\x8f\xa3\x8a\x86\x75" \
"\x13\x79\x10\x25\xef\x8c\xc6\x4d\xf0\xbf\x8b\x4a\xfb\x49\x58\xae\xe7\x71\x21\xf4\x29\x58\x28\xb4" \
"\xbf\x41\xec\xa7\x81\xc8\xbe\x64\xd4\xf7\x44\xa2\x0c\x31\x6b\x7c\xfc\x33\x0a\x60\xa8\x36\x5a\xe8" \
"\xfd\xdb\x11\x44\xf8\x69\x12\x4f\x4c\x4a\x48\x2b\x4e\x0a\x44\x1b\xb7\x86\x08\xd9\x5d\x61\x2a\x8b" \
"\x51\x37\x51\x6d\x29\x8c\x4f\xfe\xc2\x84\x2d\x52\x94\xe0\xf4\x60\x5b\xdd\x8d\xda\x67\xe5\xfb\x37" \
"\x77\x51\xc3\x52\xb1\x24\x7f\x46\x3f\x3c\x62\xb5\x1e\xfa\x76\x0f\x39\xaf\x23\xd8\x93\xa9\x4a\x53" \
"\xdf\x38\x59\xde\x70\xbb\x1c\x66\xc8\xbc\xd4\xbc\x1e\xb9\x20\xa6\x62\x9a\x75\xd6\xc9\x94\x46\xcd" \
"\x09\x8f\xa3\x9e\xf9\x1f\xe8\x11\x73\x98\x66\x84\x04\x8f\x7c\xee\xc6\x28\xb3\x21\xa4\x9b\x42\xa3" \
"\xb1\x8f\x0f\xb9\x1a\x4d\xd6\xc0\x26\xa5\x42\x83\x6f\x64\xdf\x8e\x6a\x4e\xf9\x24\x50\x1f\x43\x74" \
"\x42\x43\x0d\x31\x69\xf5\xca\x47\xf8\x82\x8f\xf2\x8b\xc6\xa2\x57\x15"

// active and backup keys use the same exponent
#define APISSLEXPONENTSIZE "\x03"
#define APISSLEXPONENT "\x01\x00\x01"

#define MEGA_DNS_SERVERS "2001:678:25c:2215::554,89.44.169.136," \
                         "2001:67c:1998:2212::13,31.216.148.13," \
                         "2405:f900:3e6a:1::103,31.216.148.11," \
                         "2403:9800:c020::43,122.56.56.216"

class MEGA_API SpeedController
{
public:
    SpeedController();
    m_off_t calculateSpeed(long long numBytes = 0);
    m_off_t getMeanSpeed();

    // interval to calculate the mean speed (ds)
    static const int SPEED_MEAN_INTERVAL_DS;

    // max values to calculate the mean speed
    static const int SPEED_MAX_VALUES;

protected:
    map<dstime, m_off_t> transferBytes;
    m_off_t partialBytes;

    m_off_t meanSpeed;
    dstime lastUpdate;
    int speedCounter;
};

// generic host HTTP I/O interface
struct MEGA_API HttpIO : public EventTrigger
{
    // set whenever a network request completes successfully
    bool success;

    // post request to target URL
    virtual void post(struct HttpReq*, const char* = NULL, unsigned = 0) = 0;

    // cancel request
    virtual void cancel(HttpReq*) = 0;

    // real-time POST progress information
    virtual m_off_t postpos(void*) = 0;

    // execute I/O operations
    virtual bool doio(void) = 0;

    // lock/unlock all in-flight HttpReqs
    virtual void lock() { }
    virtual void unlock() { }

    virtual void disconnect() { }

    // track Internet connectivity issues
    dstime noinetds;
    bool inetback;
    void inetstatus(bool);
    bool inetisback();

    // timestamp of last data received (across all connections)
    dstime lastdata;
    
    // download speed
    SpeedController downloadSpeedController;
    m_off_t downloadSpeed;
    void updatedownloadspeed(m_off_t size = 0);

    // upload speed
    SpeedController uploadSpeedController;
    m_off_t uploadSpeed;
    void updateuploadspeed(m_off_t size = 0);

    // data receive timeout (ds)
    static const int NETWORKTIMEOUT;

    // request timeout (ds)
    static const int REQUESTTIMEOUT;

    // sc request timeout (ds)
    static const int SCREQUESTTIMEOUT;

    // connection timeout (ds)
    static const int CONNECTTIMEOUT;
    
    // set useragent (must be called exactly once)
    virtual void setuseragent(string*) = 0;

    // get proxy settings from the system
    virtual Proxy *getautoproxy();

    // get alternative DNS servers
    void getMEGADNSservers(string*, bool = true);

    // set max download speed
    virtual bool setmaxdownloadspeed(m_off_t bpslimit);

    // set max upload speed
    virtual bool setmaxuploadspeed(m_off_t bpslimit);

    // get max download speed
    virtual m_off_t getmaxdownloadspeed();

    // get max upload speed
    virtual m_off_t getmaxuploadspeed();

    HttpIO();
    virtual ~HttpIO() { }
};

// outgoing HTTP request
struct MEGA_API HttpReq
{
    std::atomic<reqstatus_t> status;
    m_off_t pos;

    int httpstatus;

    httpmethod_t method;
    contenttype_t type;
    int timeoutms;

    string posturl;

    bool protect; // check pinned public key
    bool minspeed;

    bool sslcheckfailed;
    string sslfakeissuer;

    string* out;
    string in;
    size_t inpurge;
    size_t outpos;

    string outbuf;

    // if the out payload includes a fetch nodes command
    bool includesFetchingNodes = false;

    byte* buf;
    m_off_t buflen, bufpos, notifiedbufpos;

    // we assume that API responses are smaller than 4 GB
    m_off_t contentlength;

    // time left related to a bandwidth overquota
    m_time_t timeleft;

    // Content-Type of the response
    string contenttype;

    // HttpIO implementation-specific identifier for this connection
    void* httpiohandle;

    // while this request is in flight, points to the application's HttpIO
    // object - NULL otherwise
    HttpIO* httpio;

    // identify different channels from different MegaClients etc in the log
    string logname;

    // set url and content type for subsequent requests
    void setreq(const char*, contenttype_t);

    // send POST request to the network
    void post(MegaClient*, const char* = NULL, unsigned = 0);

    // send GET request to the network
    void get(MegaClient*);

    // send a DNS request
    void dns(MegaClient*);

    // store chunk of incoming data with optional purging
    void put(void*, unsigned, bool = false);

    // start and size of unpurged data block - must be called with !buf and httpio locked
    char* data();
    size_t size();

    // a buffer that the HttpReq filled in.   This struct owns the buffer (so HttpReq no longer has it).
    struct http_buf_t 
    { 
        byte* datastart();
        size_t datalen();

        size_t start;
        size_t end;

        http_buf_t(byte* b, size_t s, size_t e);  // takes ownership of the byte*, which must have been allocated with new[]
        ~http_buf_t();
        void swap(http_buf_t& other);
        bool isNull();

    private: 
        byte* buf;
    };
    
    // give up ownership of the buffer for client to use.  The caller is the new owner of the http_buf_t, and the HttpReq no longer has the buffer or any info about it.
    http_buf_t* release_buf();

    // set amount of purgeable data at 0
    void purge(size_t);

    // set response content length
    void setcontentlength(m_off_t);
    
    // reserve space for incoming data
    byte* reserveput(unsigned* len);

    // disconnect open HTTP connection
    void disconnect();

    // progress information
    virtual m_off_t transferred(MegaClient*);

    // timestamp of last data sent or received
    dstime lastdata;

    // prevent raw data from being dumped in debug mode
    bool binary;

    HttpReq(bool = false);
    virtual ~HttpReq();
    void init();
};

struct MEGA_API GenericHttpReq : public HttpReq
{
    GenericHttpReq(PrnGen &rng, bool = false);

    // tag related to the request
    int tag;

    // max number of retries, including the first attempt
    // 0 = infinite retries, 1 = no retries
    int maxretries;

    // current retry number
    int numretry;

    // backoff between retries
    BackoffTimer bt;

    // true when the backoff between retries is active
    bool isbtactive;

    // backoff to control the maximum allowed time for the request
    BackoffTimer maxbt;
};

class MEGA_API EncryptByChunks
{
    // this class allows encrypting a large buffer chunk by chunk, 
    // or alternatively encrypting consecutive data by feeding it a piece at a time, 
    // from separate buffers (the algorithm chooses the size though)

public:
    // size (in bytes) of the CRC of uploaded chunks
    enum { CRCSIZE = 12 };

    EncryptByChunks(SymmCipher* k, chunkmac_map* m, uint64_t iv);

    // encryption: data must be NULL-padded to SymmCipher::BLOCKSIZE
    // (so buffer allocation size must be rounded up too)
    // len must be < 2^31
    virtual byte* nextbuffer(unsigned datasize) = 0;

    bool encrypt(m_off_t pos, m_off_t npos, string& urlSuffix);

private:
    SymmCipher* key;
    chunkmac_map* macs;
    uint64_t ctriv;     // initialization vector for CTR mode
    byte crc[CRCSIZE];
    void updateCRC(byte* data, unsigned size, unsigned offset);
};

class MEGA_API EncryptBufferByChunks : public EncryptByChunks
{
    // specialisation for encrypting a whole contiguous buffer by chunks
    byte *chunkstart;

    byte* nextbuffer(unsigned bufsize) override;

public:
    EncryptBufferByChunks(byte* b, SymmCipher* k, chunkmac_map* m, uint64_t iv);
};

// file chunk I/O
struct MEGA_API HttpReqXfer : public HttpReq
{
    unsigned size;

    virtual void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t) = 0;

    HttpReqXfer() : HttpReq(true), size(0) { }
};

// file chunk upload
struct MEGA_API HttpReqUL : public HttpReqXfer
{
    chunkmac_map mChunkmacs;

    void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t);

    m_off_t transferred(MegaClient*);

    ~HttpReqUL() { }
};

// file chunk download
struct MEGA_API HttpReqDL : public HttpReqXfer
{
    m_off_t dlpos;
    bool buffer_released;

    void prepare(const char*, SymmCipher*, uint64_t, m_off_t, m_off_t);

    HttpReqDL();
    ~HttpReqDL() { }
};

// file attribute get
struct MEGA_API HttpReqGetFA : public HttpReq
{
    ~HttpReqGetFA() { }
};
} // namespace

#endif
