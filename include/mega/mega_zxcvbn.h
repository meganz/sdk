#ifndef ZXCVBN_H_F98183CE2A01_INCLUDED
#define ZXCVBN_H_F98183CE2A01_INCLUDED
/**********************************************************************************
 * C implementation of the zxcvbn password strength estimation method.
 * Copyright (c) 2015-2017 Tony Evans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **********************************************************************************/

/* If this is defined, the dictiononary data is read from file. When undefined */
/* dictionary data is included in the source code. */
/*#define USE_DICT_FILE */

/* If this is defined, C++ builds which read dictionary data from file will use */
/* stdio FILE streams (and fopen,fread,fclose). When undefined, C++ builds will */
/* use std::ifstream to read dictionary data. Ignored for C builds (stdio FILE  */
/* streams are always used). */
/*#define USE_FILE_IO */

#ifndef __cplusplus
/* C build. Use the standard malloc/free for heap memory */
#include <stdlib.h>
#define MallocFn(T,N) ((T *)malloc((N) * sizeof(T)))
#define FreeFn(P)      free(P)

#else

/* C++ build. Use the new/delete operators for heap memory */
#define MallocFn(T,N)   (new T[N])
#define FreeFn(P)       (delete [] P)

#endif

/* Enum for the types of match returned in the Info arg to ZxcvbnMatch */
typedef enum
{
    NON_MATCH,          /* 0 */
    BRUTE_MATCH,        /* 1 */
    DICTIONARY_MATCH,   /* 2 */
    DICT_LEET_MATCH,    /* 3 */
    USER_MATCH,         /* 4 */
    USER_LEET_MATCH,    /* 5 */
    REPEATS_MATCH,      /* 6 */
    SEQUENCE_MATCH,     /* 7 */
    SPATIAL_MATCH,      /* 8 */
    DATE_MATCH,         /* 9 */
    YEAR_MATCH,         /* 10 */
    MULTIPLE_MATCH = 32 /* Added to above to indicate matching part has been repeated */
} ZxcTypeMatch_t;

/* Linked list of information returned in the Info arg to ZxcvbnMatch */
struct ZxcMatch
{
    int             Begin;   /* Char position of begining of match */
    int             Length;  /* Number of chars in the match */
    double          Entrpy;  /* The entropy of the match */
    double          MltEnpy; /* Entropy with additional allowance for multipart password */
    ZxcTypeMatch_t  Type;    /* Type of match (Spatial/Dictionary/Order/Repeat) */
    struct ZxcMatch *Next;
};
typedef struct ZxcMatch ZxcMatch_t;


#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_DICT_FILE

/**********************************************************************************
 * Read the dictionnary data from the given file. Returns 1 if OK, 0 if error.
 * Called once at program startup.
 */
int ZxcvbnInit(const char *);

/**********************************************************************************
 * Free the dictionnary data after use. Called once at program shutdown.
 */
void ZxcvbnUnInit();

#else

/* As the dictionary data is included in the source, define these functions to do nothing. */
#define ZxcvbnInit(s) 1
#define ZxcvbnUnInit() do {} while(0)

#endif

/**********************************************************************************
 * The main password matching function. May be called multiple times.
 * The parameters are:
 *  Passwd      The password to be tested. Null terminated string.
 *  UserDict    User supplied dictionary words to be considered particulary bad. Passed
 *               as a pointer to array of string pointers, with null last entry (like
 *               the argv parameter to main()). May be null or point to empty array when
 *               there are no user dictionary words.
 *  Info        The address of a pointer variable to receive information on the parts
 *               of the password. This parameter can be null if no information is wanted.
 *               The data should be freed by calling ZxcvbnFreeInfo().
 * 
 * Returns the entropy of the password (in bits).
 */
double ZxcvbnMatch(const char *Passwd, const char *UserDict[], ZxcMatch_t **Info);

/**********************************************************************************
 * Free the data returned in the Info parameter to ZxcvbnMatch().
 */
void ZxcvbnFreeInfo(ZxcMatch_t *Info);

#ifdef __cplusplus
}
#endif

#endif
