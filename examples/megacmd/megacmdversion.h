#ifndef MEGACMDVERSION_H
#define MEGACMDVERSION_H

#ifndef MEGACMD_MAJOR_VERSION
#define MEGACMD_MAJOR_VERSION 0
#endif
#ifndef MEGACMD_MINOR_VERSION
#define MEGACMD_MINOR_VERSION 9
#endif
#ifndef MEGACMD_MICRO_VERSION
#define MEGACMD_MICRO_VERSION 3
#endif

#ifndef MEGACMD_CODE_VERSION
#define MEGACMD_CODE_VERSION (MEGACMD_MICRO_VERSION*100+MEGACMD_MINOR_VERSION*10000+MEGACMD_MAJOR_VERSION*1000000)
#endif

const char * const megacmdchangelog =
        "fixed mkdir in MacOS""\n"
        "added command \"https\" to force HTTPS for file transfers""\n"
        "added -n to \"users\" to show users names""\n"
        "modified greeting""\n"
        "fixed \"clear\" for Windows""\n"
        "fixed download >4GB files""\n"
        "fixed bug in asynchronous transfers""\n"
        ;


#endif // VERSION_H
