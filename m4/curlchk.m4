AC_DEFUN([CURL_CHK],
[
AC_MSG_CHECKING([whether cURL is built with OpenSSL support])
AC_RUN_IFELSE([AC_LANG_PROGRAM([
    #include <curl/curl.h>
    #include <stdio.h>
    static CURLcode sslctxfun(CURL *curl, void *sslctx, void *parm) { return CURLE_OK; }
], [
        CURL *curl;
        curl_version_info_data *data;
        CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
        if (rc != CURLE_OK) {
            fprintf(stderr,"Cannot initialize curl: %d\n", rc);
            return 1;
        }
        data = curl_version_info(CURLVERSION_NOW);
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr,"Cannot create curl handle\n");
            return 1;
        }
        if (data->version_num < 0x072C00) // 7.44.0
        {
            rc = curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun);
            if (rc != CURLE_OK) {
                fprintf(stderr,"Cannot set SSL context function: %s\nIs curl built with OpenSSL?\n", curl_easy_strerror(rc));
                return 1;
            }
        }
        curl_easy_cleanup(curl);
        return 0;
])],[AC_MSG_RESULT([yes])],[
   AC_MSG_RESULT([no])
   AC_MSG_ERROR([Make sure cURL is built/linked with OpenSSL!])
],[
   AC_MSG_RESULT([unknown])
]
)
])
