# mega/megasdk
megaapi_wrap.h:18: Warning 401: Nothing known about base class 'mega::MegaGfxProcessor'. Ignored.
megaapi_wrap.h:60: Warning 401: Nothing known about base class 'mega::MegaLogger'. Ignored.
megaapi_wrap.h:74: Warning 401: Nothing known about base class 'mega::MegaTreeProcessor'. Ignored.
megaapi_wrap.h:88: Warning 401: Nothing known about base class 'mega::MegaRequestListener'. Ignored.
megaapi_wrap.h:114: Warning 401: Nothing known about base class 'mega::MegaTransferListener'. Ignored.
megaapi_wrap.h:148: Warning 401: Nothing known about base class 'mega::MegaGlobalListener'. Ignored.
megaapi_wrap.h:202: Warning 401: Nothing known about base class 'mega::MegaListener'. Ignored.
# mega/megasdk
cgo: inconsistent definitions for C.swig_type_4
cgo: inconsistent definitions for C.swig_type_2
cgo: inconsistent definitions for C.swig_type_26
cgo: inconsistent definitions for C.swig_type_3
cgo: inconsistent definitions for C.swig_type_22
cgo: inconsistent definitions for C.swig_type_24


$ cat b043/_mega_swig.go | grep swig_type_24                                                                                                                                                                                                  
typedef long long swig_type_24;
extern _Bool _wrap_SwigDirector_MegaTransferListener_onTransferData_mega_140d19983b5f0e49(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, swig_type_23 arg4, swig_type_24 arg5);
        swig_r = (bool)(C._wrap_SwigDirector_MegaTransferListener_onTransferData_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), C.uintptr_t(_swig_i_1), C.uintptr_t(_swig_i_2), *(*C.swig_type_23)(unsafe.Pointer(&_swig_i_3)), C.swig_type_24(_swig_i_4)))

$ cat b043/_mega_swig.go | grep swig_type_3                                                                                                                                                                                                   
typedef _gostring_ swig_type_3;
extern _Bool _wrap_SwigDirector_MegaGfxProcessor__swig_upcall_getBitmapData_mega_140d19983b5f0e49(uintptr_t arg1, swig_type_3 arg2, swig_type_4 arg3);
        swig_r = (bool)(C._wrap_SwigDirector_MegaGfxProcessor__swig_upcall_getBitmapData_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), *(*C.swig_type_3)(unsafe.Pointer(&_swig_i_1)), C.swig_type_4(_swig_i_2)))

$ cat b043/_mega_swig.go | grep swig_type_26                                                                                                                                                                                                  
typedef _gostring_ swig_type_26;
extern void _wrap_SwigDirector_MegaGlobalListener_onDrivePresenceChanged_mega_140d19983b5f0e49(uintptr_t arg1, uintptr_t arg2, _Bool arg3, swig_type_26 arg4);
        C._wrap_SwigDirector_MegaGlobalListener_onDrivePresenceChanged_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), C.uintptr_t(_swig_i_1), C._Bool(_swig_i_2), *(*C.swig_type_26)(unsafe.Pointer(&_swig_i_3)))

$ cat b043/_mega_swig.go | grep swig_type_22                                                                                                                                                                                                  
typedef long long swig_type_22;
extern _Bool _wrap_SwigDirector_MegaTransferListener__swig_upcall_onTransferData_mega_140d19983b5f0e49(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, swig_type_21 arg4, swig_type_22 arg5);
        swig_r = (bool)(C._wrap_SwigDirector_MegaTransferListener__swig_upcall_onTransferData_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), C.uintptr_t(_swig_i_1), C.uintptr_t(_swig_i_2), *(*C.swig_type_21)(unsafe.Pointer(&_swig_i_3)), C.swig_type_22(_swig_i_4)))

$ cat b043/_mega_swig.go | grep swig_type_4                                                                                                                                                                                                   
typedef long long swig_type_4;
extern _Bool _wrap_SwigDirector_MegaGfxProcessor__swig_upcall_getBitmapData_mega_140d19983b5f0e49(uintptr_t arg1, swig_type_3 arg2, swig_type_4 arg3);
        swig_r = (bool)(C._wrap_SwigDirector_MegaGfxProcessor__swig_upcall_getBitmapData_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), *(*C.swig_type_3)(unsafe.Pointer(&_swig_i_1)), C.swig_type_4(_swig_i_2)))

$ cat b043/_mega_swig.go | grep swig_type_2                                                                                                                                                                                                   
typedef _gostring_ swig_type_2;
extern _Bool _wrap_SwigDirector_MegaGfxProcessor_readBitmap_mega_140d19983b5f0e49(uintptr_t arg1, swig_type_2 arg2);
        swig_r = (bool)(C._wrap_SwigDirector_MegaGfxProcessor_readBitmap_mega_140d19983b5f0e49(C.uintptr_t(_swig_i_0), *(*C.swig_type_2)(unsafe.Pointer(&_swig_i_1))))
