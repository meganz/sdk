/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 2.0.11
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package nz.mega.sdk;

public class MegaError {
  private long swigCPtr;
  protected boolean swigCMemOwn;

  protected MegaError(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(MegaError obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  protected synchronized void delete() {   
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        megaJNI.delete_MegaError(swigCPtr);
      }
      swigCPtr = 0;
    }
}

  public MegaError(int errorCode) {
    this(megaJNI.new_MegaError__SWIG_0(errorCode), true);
  }

  public MegaError() {
    this(megaJNI.new_MegaError__SWIG_1(), true);
  }

  public MegaError(int errorCode, long value) {
    this(megaJNI.new_MegaError__SWIG_2(errorCode, value), true);
  }

  public MegaError(MegaError megaError) {
    this(megaJNI.new_MegaError__SWIG_3(MegaError.getCPtr(megaError), megaError), true);
  }

   MegaError copy() {
    long cPtr = megaJNI.MegaError_copy(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaError(cPtr, true);
  }

  public int getErrorCode() {
    return megaJNI.MegaError_getErrorCode(swigCPtr, this);
  }

  public long getValue() {
    return megaJNI.MegaError_getValue(swigCPtr, this);
  }

  public String getErrorString() {
    return megaJNI.MegaError_getErrorString__SWIG_0(swigCPtr, this);
  }

  public String toString() {
    return megaJNI.MegaError_toString(swigCPtr, this);
  }

  public String __str__() {
    return megaJNI.MegaError___str__(swigCPtr, this);
  }

  public String __toString() {
    return megaJNI.MegaError___toString(swigCPtr, this);
  }

  public static String getErrorString(int errorCode) {
    return megaJNI.MegaError_getErrorString__SWIG_1(errorCode);
  }

  public final static int API_OK = 0;
  public final static int API_EINTERNAL = -1;
  public final static int API_EARGS = -2;
  public final static int API_EAGAIN = -3;
  public final static int API_ERATELIMIT = -4;
  public final static int API_EFAILED = -5;
  public final static int API_ETOOMANY = -6;
  public final static int API_ERANGE = -7;
  public final static int API_EEXPIRED = -8;
  public final static int API_ENOENT = -9;
  public final static int API_ECIRCULAR = -10;
  public final static int API_EACCESS = -11;
  public final static int API_EEXIST = -12;
  public final static int API_EINCOMPLETE = -13;
  public final static int API_EKEY = -14;
  public final static int API_ESID = -15;
  public final static int API_EBLOCKED = -16;
  public final static int API_EOVERQUOTA = -17;
  public final static int API_ETEMPUNAVAIL = -18;
  public final static int API_ETOOMANYCONNECTIONS = -19;
  public final static int API_EWRITE = -20;
  public final static int API_EREAD = -21;
  public final static int API_EAPPKEY = -22;
  public final static int API_ESSL = -23;
  public final static int PAYMENT_ECARD = -101;
  public final static int PAYMENT_EBILLING = -102;
  public final static int PAYMENT_EFRAUD = -103;
  public final static int PAYMENT_ETOOMANY = -104;
  public final static int PAYMENT_EBALANCE = -105;
  public final static int PAYMENT_EGENERIC = -106;

}
