/*
 * secureBuffer.h
 *
 *  Created on: 6/03/2015
 *      Author: michael
 */

#ifndef INCLUDE_MEGA_SECUREBUFFER_H_
#define INCLUDE_MEGA_SECUREBUFFER_H_
#include <stdexcept>

//#ifdef USE_SODIUM
#include <sodium.h>
//#endif

namespace mega {
class SecureBuffer {

    /**
     * @brief The bytes held in this buffer.
     */
    unsigned char *buffer;

    /**
     * @brief The size of the buffer.
     */
    unsigned int _size;
public:

    /**
     * @brief No-arg constructor for SecureBuffer. Returns a nullbuffer.
     */
    SecureBuffer() : buffer(nullptr), _size(0) {}

    /**
     * @brief Constructor for SecureBuffer takes the size of the buffer.
     *
     * If we are using libsodium, we can use secure functions for memory allocation/
     * deallocation.
     */
    inline SecureBuffer(unsigned int size) : _size(size) {
//#ifdef USE_SODIUM
        sodium_init();
        buffer = (unsigned char*)sodium_malloc(size);
//#else
//        buffer = (unsigned char*)malloc(_size);
//#endif
    }

    /**
     * @brief Destructor for SecureBuffer.
    **/
    virtual ~SecureBuffer() {

    }

    /**
     * @brief free the underlying bytes.
     *
     * If libsodium is available, then we free using secure deallocation.
     */
    inline void free() {
//#ifdef USE_SODIUM
        sodium_free(buffer);
//#else
//        free(buffer);
//#endif
    }

    inline void clearAndResize(int size) {
//#ifdef USE_SODIUM
        sodium_free(buffer);
        buffer = (unsigned char*)sodium_malloc(size);
//#else
//        free(buffer);
//        buffer = (unsigned char*)malloc(size);
//#endif

        _size = size;
    }

    /**
     * @brief Returns the contained pointer to bytes.
     *
     * @return Pointer to the bytes contained.
     */
    inline unsigned char *get() { return buffer; }

    /**
     * @brief Get the byte at the given index
     *
     * @return The byte at the given index.
     * @throws runtime_error if the index is out-of-bounds of this buffer.
     */
    inline unsigned char operator[](unsigned int index) {
        if(index >= _size) {
            throw std::runtime_error("Index out-of-bounds.");
        }

        return buffer[index];
    };

    inline unsigned int size() { return _size; }
};

} /* namespace mega */

#endif /* INCLUDE_MEGA_SECUREBUFFER_H_ */
