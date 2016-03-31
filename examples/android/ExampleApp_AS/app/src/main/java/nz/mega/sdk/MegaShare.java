/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 2.0.11
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package nz.mega.sdk;

public class MegaShare {
  private long swigCPtr;
  protected boolean swigCMemOwn;

  protected MegaShare(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(MegaShare obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  protected synchronized void delete() {   
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        megaJNI.delete_MegaShare(swigCPtr);
      }
      swigCPtr = 0;
    }
}

   MegaShare copy() {
    long cPtr = megaJNI.MegaShare_copy(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaShare(cPtr, true);
  }

  public String getUser() {
    return megaJNI.MegaShare_getUser(swigCPtr, this);
  }

  public long getNodeHandle() {
    return megaJNI.MegaShare_getNodeHandle(swigCPtr, this);
  }

  public int getAccess() {
    return megaJNI.MegaShare_getAccess(swigCPtr, this);
  }

  public long getTimestamp() {
    return megaJNI.MegaShare_getTimestamp(swigCPtr, this);
  }

  public MegaShare() {
    this(megaJNI.new_MegaShare(), true);
  }

  public final static int ACCESS_UNKNOWN = -1;
  public final static int ACCESS_READ = 0;
  public final static int ACCESS_READWRITE = ACCESS_READ + 1;
  public final static int ACCESS_FULL = ACCESS_READWRITE + 1;
  public final static int ACCESS_OWNER = ACCESS_FULL + 1;

}
