#ifndef MEGA_SCCR_PSTATS_H
#define MEGA_SCCR_PSTATS_H 1

namespace mega::SCCR {

struct PStats
{
    int cycle;
    long bytesin;
    long bytesout;
    int conn;
    int connssl;
    int conn6;
    int connul;
    int connulpoll;
    int newconn;
    int newssl;
    int rej;
    int rejssl;
    int keptalive;
    int missingfiles;
    int missingparts;
    int iopain;
    int raidproxyerr;
    uint64_t uploadq;
    int slavelag;
    int fd_sock;
    int fd_sockssl;
    int fd_sockun;
    int fd_filedl;
    int fd_fileul;
};

} // namespace

extern mega::SCCR::PStats pstats;

#endif
