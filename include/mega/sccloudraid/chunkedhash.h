#ifndef MEGA_SCCR_CHUNKEDHASH_H
#define MEGA_SCCR_CHUNKEDHASH_H 1

#include <array>

//#define SEGSIZE 131072

namespace mega::SCCR {

class ChunkedHash
{
public:
    static const int HASHLEN = 16;

    // suitable to treat like POD type
    struct Hash 
    {
        union hash_t
        {
            std::array<uint8_t, ChunkedHash::HASHLEN> array;
            uint128_t uint128;
            unsigned char uchars[HASHLEN];
            long longs[2];

            static_assert(sizeof array == HASHLEN && sizeof uint128 == HASHLEN && sizeof uchars == HASHLEN && sizeof longs == HASHLEN, "Size mismatch");
        } u;

        Hash() {}
        Hash(uint8_t* p)
        {
            memcpy(u.array.data(), p, ChunkedHash::HASHLEN);
        }

        // satisfy most existing usages by auto conversion to unsigned char*
        operator unsigned char* () { return u.uchars; }
    };
    static_assert(sizeof(Hash) == HASHLEN, "Size mismatch");

private:

#pragma pack(push,1)
    struct HashChunk
    {
        Hash hash;
        int complete;
        
        HashChunk()
        {
            memset(hash, 0, sizeof hash);
            complete = 0;
        }
    } __attribute__ ((aligned(16)));
#pragma pack(pop)

    typedef std::map<off_t, struct HashChunk> chunk_map;

    static off_t chunkfloor(off_t, int* = NULL);
    static off_t chunkceil(off_t);
    
public:
    Hash hash;                      // current aggregate hash   
    off_t pos;                      // at this position

    chunk_map chunks;

    static AES aes;

    int update(off_t, off_t, off_t, const unsigned char*);
    
    int setpos(off_t, off_t, unsigned char*);
    int aggregate(off_t);
    
    static int checkchunk(off_t, off_t, off_t);

    bool dumpstate(FILE*);
    bool readstate(FILE*);

    void init();
    
    ChunkedHash(off_t);
    ChunkedHash();
};

} // namespace

#endif
