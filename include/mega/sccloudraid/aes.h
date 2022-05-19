#ifndef MEGA_SCCR_AES_H
#define MEGA_SCCR_AES_H 1 
 
#include <gcrypt.h>

namespace mega::SCCR {

class AES
{
    gcry_cipher_hd_t hd;
    
public:
    void setkey(const char*);
    void encrypt(unsigned char*);
    void decrypt(unsigned char*);
    
    AES();
};

} // namespace

#endif
