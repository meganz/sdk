#pragma once

#include <poll.h>

#include <system_error>
#include <chrono>
#include <vector>
#include <filesystem>

namespace mega {
namespace gfx {

struct SocketUtils
{
    /**
     * @brief Compose a valid path for the unix domain socket from the name 
     *        in the format /tmp/MegaLimited<uid>/name. <uid> is the real user id.
     *
     * @param name a valid file name
     *
     * @return a path
     */
    static std::filesystem::path toSocketPath(const std::string& name);

    /**
     * @brief Remove the socket file from file system
     *
     * @param name a valid file name
     *
     * @return On success, 0 value is set in error_code. On failure, a none zero error_code is returned.
     *
     * @see toSocketPath about how the path is composed
     */
    static std::error_code removeSocketFile(const std::string& name);

    /**
     * @brief Create a stream socket and connect it to a UNIX domain socket server.
     *
     * @param socketPath The socket path that the server is listening on
     *
     * @return A pair of an error_code and a file descriptior. On Success, 0 error_code and a valid file descriptor
     *         pair is returned. On error, a non-zero error_code and -1 pair is returned.
     */
    static std::pair<std::error_code, int> connect(const std::filesystem::path& socketPath);

    /**
     * @brief Create a stream UNIX domain socket, bind it on the socketPath and listen on it.
     *
     * @param socketPath The socket path to bind on
     *
     * @return A pair of an error_code and a file descriptior. On Success, 0 error_code and a valid listening file descriptor
     *         pair is returned. On error, a non-zero error_code and -1 pair is returned.
     */
    static std::pair<std::error_code, int> listen(const std::filesystem::path& socketPath);

    /**
     * @brief Accept on the listening file descriptor with timeout
     *
     * @param listeningFd The listening file descriptor
     *
     * @param timeout The maxiumum time to wait for a connection
     *
     * @return A pair of an error_code and a file descriptior. On Success, 0 error_code and a valid file descriptor for the 
     *         new established connection pair is returned. On error, a non-zero error_code and -1 pair is returned.
     */
    static std::pair<std::error_code, int> accept(int listeningFd, std::chrono::milliseconds timeout);

    /**
     * @brief Read the exact n bytes data from the file descriptor or timeout
     *
     * @param fd The file descriptor to read from
     *
     * @param buf The buffer to store the data. The buffer is at least n bytes and caller takes ownership.
     *
     * @param n The number of bytes to read
     *
     * @param timeout The maxiumum time to wait for reading
     *
     * @return On success, 0 error_code is returned and the n bytes data is read to buf. 
     *         On error, a non-zero error_code is returned.
     */
    static std::error_code read(int fd, void* buf, size_t n, std::chrono::milliseconds timeout);

    /**
     * @brief Write the exact n bytes data to the file descriptor or timeout
     *
     * @param fd The file descriptor to write to
     *
     * @param buf The buffer stored the data.
     *
     * @param n The number of bytes to write
     *
     * @param timeout The maxiumum time to wait for writing
     *
     * @return On success, 0 error_code is returned and the count bytes data is written. 
     *         On error, a non-zero error_code is returned.
     */
    static std::error_code write(int fd, const void* data, size_t n, std::chrono::milliseconds timeout);
};

} // end of namespace
}