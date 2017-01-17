#ifndef MEGACMDVERSION_H
#define MEGACMDVERSION_H

#ifndef MEGACMD_MAJOR_VERSION
#define MEGACMD_MAJOR_VERSION 0
#endif
#ifndef MEGACMD_MINOR_VERSION
#define MEGACMD_MINOR_VERSION 9
#endif
#ifndef MEGACMD_MICRO_VERSION
#define MEGACMD_MICRO_VERSION 1
#endif

#ifndef MEGACMD_CODE_VERSION
#define MEGACMD_CODE_VERSION (MEGACMD_MICRO_VERSION*100+MEGACMD_MINOR_VERSION*10000+MEGACMD_MAJOR_VERSION*1000000)
#endif

const char *megacmdchangelog =
        "Initial version of megacmd""\n"
        "Features:""\n"
        "Interactive shell""\n"
        "Non interactive mode""\n"
        "Regular expresions""\n"
        "Contacts management""\n"
        "Public folders management""\n"
        "Files management""\n"
        "Synching"
        ;


#endif // VERSION_H
