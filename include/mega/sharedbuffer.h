/*
 * @file shared_buffer.h
 * @brief All of the common enums, constants and functions used in
 * the library.
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA mpENC_cpp - native multi-party
 * message encoding library.
 *
 * The mpENC_cpp library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * @author Michael Holmwood
 * @email mh@mega.co.nz
 */

#ifndef SHARED_MEGA_BUFFER_H_
#define SHARED_MEGA_BUFFER_H_

#if __cplusplus >= 201103L
#include <memory>
using std::shared_ptr;
#else
#include <tr1/memory>
using std::tr1::shared_ptr;
#endif
#include <iostream>
#include <cstring>
#include <functional>

namespace mega {

typedef enum {
    M_WMF_BASE_64,
    M_PLAIN_BYTES
} M_WireMessageFormat;

typedef enum {
    M_VS_PUBLIC,
    M_VS_PRIVATE
} Visibility;

struct SharedBuffer : public std::shared_ptr<unsigned char> {

    /**
     * @brief Create an empty (essentially null) SharedBuffer.
     */
    SharedBuffer() :
        std::shared_ptr<unsigned char>(),
        size(0),
        error(0),
        format(M_PLAIN_BYTES),
        visibility(M_VS_PUBLIC) { }

    /**
     * @brief Create a SharedBuffer of the given size.
     *
     * @param size The size of the buffer to create.
     */
    SharedBuffer(unsigned int size, Visibility visibility = M_VS_PUBLIC) :
        std::shared_ptr<unsigned char>((unsigned char*)malloc(size), DeleteBuffer),
        size(size),
        error(0),
        format(M_PLAIN_BYTES),
        visibility(visibility) { }

    /**
     * @brief Copy the supplied bytes into this SharedBuffer.
     *
     * @param buffer The bytes to copy into the buffer.
     * @param size The number of bytes to copy into the buffer.
     */
    SharedBuffer(const unsigned char *buffer, unsigned int size,
            Visibility visibility = M_VS_PUBLIC) :
        std::shared_ptr<unsigned char>((unsigned char*)malloc(size), DeleteBuffer),
        size(size),
        error(0),
        format(M_PLAIN_BYTES),
        visibility(visibility) {
        memcpy(get(), buffer, size);
    }

    /**
     * @brief Converts a string into a SharedBuffer.
     *
     * This does NOT discard the null terminator character.
     *
     * @param data The string to copy into the buffer.
     */
    SharedBuffer(std::string data) :
        std::shared_ptr<unsigned char>((unsigned char*)malloc(data.length() + 1), DeleteBuffer),
                size(data.length() + 1),
                error(0),
                format(M_PLAIN_BYTES),
                visibility(M_VS_PUBLIC) {
        memcpy(get(), data.c_str(), size);
    }

    /**
     * @brief Release the underlying pointer and reallocate.
     *
     * @param data The data to reallocte.
     * @param size The size of data.
     */
    void realloc(unsigned char *data, unsigned int size) {
        reset((unsigned char*)malloc(size));
        memcpy(get(), data, size);
        this->size = size;
    }

    /**
     * @brief Convert the buffer into a string.
     *
     * @return String representation of the buffer.
     */
    std::string
    str() { return std::string((char*)get(), size); }

    /**
     * @brief Deleter for Buffer.
     */
    static void DeleteBuffer(unsigned char *buffer) {
        free(buffer);
    }

    /**
     * @brief The size of the buffer.
     */
    unsigned int size;

    /**
     * @brief An error, if one has occurred.
     */
    int error;

    /**
     * @brief The format that the data in this SharedBuffer is in.
     */
    M_WireMessageFormat format;

    /**
     * @brief The visibility of the given attribute.
     *
     */
    Visibility visibility;

    /**
     * @brief Overrides the equality operator for SharedBuffer.
     */
    inline bool operator==(const SharedBuffer &other) const {
        if(!other || (other.size != this->size)) {
            return false;
        }

        return (memcmp(other.get(), get(), this->size) == 0);
    }

    /**
     * @brief Overrides the inequality operator for SharedBuffer.
     */
    inline bool operator!=(const SharedBuffer &other) const {
        return !operator==(other);
    }

    /**
     * @brief Overrides the index operator for SharedBuffer.
     */
    inline unsigned char operator[](const unsigned int index) {
        return get()[index];
    }

    /**
     * @brief Creates a SharedBuffer with the same data as this SharedBuffer.
     *
     * This copies the data into a new SharedBuffer, with a new memory address.
     *
     * @return A new SharedBuffer with the same data.
     */
    inline SharedBuffer clone() {
        return SharedBuffer(get(), size);
    }
};

   /**
    * @brief Create the hash for the given buffer.
    *
    * @param buffer The buffer to create the hash for.
    * @return Hash value for the buffer.
    */
   static size_t sb_hasher(const SharedBuffer buffer) {
       std::string value = std::string((char*)buffer.get(), buffer.size);
       return std::hash<std::string>()(value);
   }

   /**
    * @brief Comparison function for SharedBuffer.
    *
    * @param lhs The lhs value for the comparison.
    * @param rhs The rhs vaule for the comparison.
    * @return True if the two buffers are equal. False otherwise.
    */
   static bool sb_eqOp(const SharedBuffer &lhs, const SharedBuffer &rhs) {
       return lhs == rhs;
   }
}

#endif /* SHARED_MEGA_BUFFER_H_ */
