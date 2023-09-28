sed -i 's,#include "megaapi.h",#include "../../../include/megaapi.h",' megasdk/mega_wrap.cpp


#$(LIBRARY_PATH): $(GO_SOURCE)
#	go build -o $@ -buildmode=c-shared $(GOBINDIR)

ln -s '../../src/.libs/libmega.so' 'libmega.so'
#ln -s '.libs/_mega.so' '_mega.so'
go build -o megasdk/libmega_go.so -buildmode=c-shared megasdk
