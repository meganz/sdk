/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 2.0.11
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package nz.mega.sdk;

public class MegaRequest {
  private long swigCPtr;
  protected boolean swigCMemOwn;

  protected MegaRequest(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(MegaRequest obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  protected synchronized void delete() {   
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        megaJNI.delete_MegaRequest(swigCPtr);
      }
      swigCPtr = 0;
    }
}

   MegaRequest copy() {
    long cPtr = megaJNI.MegaRequest_copy(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaRequest(cPtr, true);
  }

  public int getType() {
    return megaJNI.MegaRequest_getType(swigCPtr, this);
  }

  public String getRequestString() {
    return megaJNI.MegaRequest_getRequestString(swigCPtr, this);
  }

  public String toString() {
    return megaJNI.MegaRequest_toString(swigCPtr, this);
  }

  public String __str__() {
    return megaJNI.MegaRequest___str__(swigCPtr, this);
  }

  public String __toString() {
    return megaJNI.MegaRequest___toString(swigCPtr, this);
  }

  public long getNodeHandle() {
    return megaJNI.MegaRequest_getNodeHandle(swigCPtr, this);
  }

  public String getLink() {
    return megaJNI.MegaRequest_getLink(swigCPtr, this);
  }

  public long getParentHandle() {
    return megaJNI.MegaRequest_getParentHandle(swigCPtr, this);
  }

  public String getSessionKey() {
    return megaJNI.MegaRequest_getSessionKey(swigCPtr, this);
  }

  public String getName() {
    return megaJNI.MegaRequest_getName(swigCPtr, this);
  }

  public String getEmail() {
    return megaJNI.MegaRequest_getEmail(swigCPtr, this);
  }

  public String getPassword() {
    return megaJNI.MegaRequest_getPassword(swigCPtr, this);
  }

  public String getNewPassword() {
    return megaJNI.MegaRequest_getNewPassword(swigCPtr, this);
  }

  public String getPrivateKey() {
    return megaJNI.MegaRequest_getPrivateKey(swigCPtr, this);
  }

  public int getAccess() {
    return megaJNI.MegaRequest_getAccess(swigCPtr, this);
  }

  public String getFile() {
    return megaJNI.MegaRequest_getFile(swigCPtr, this);
  }

  public int getNumRetry() {
    return megaJNI.MegaRequest_getNumRetry(swigCPtr, this);
  }

  public MegaNode getPublicNode() {
    long cPtr = megaJNI.MegaRequest_getPublicNode(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaNode(cPtr, false);
  }

  public MegaNode getPublicMegaNode() {
    long cPtr = megaJNI.MegaRequest_getPublicMegaNode(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaNode(cPtr, true);
  }

  public int getParamType() {
    return megaJNI.MegaRequest_getParamType(swigCPtr, this);
  }

  public String getText() {
    return megaJNI.MegaRequest_getText(swigCPtr, this);
  }

  public long getNumber() {
    return megaJNI.MegaRequest_getNumber(swigCPtr, this);
  }

  public boolean getFlag() {
    return megaJNI.MegaRequest_getFlag(swigCPtr, this);
  }

  public long getTransferredBytes() {
    return megaJNI.MegaRequest_getTransferredBytes(swigCPtr, this);
  }

  public long getTotalBytes() {
    return megaJNI.MegaRequest_getTotalBytes(swigCPtr, this);
  }

  public MegaAccountDetails getMegaAccountDetails() {
    long cPtr = megaJNI.MegaRequest_getMegaAccountDetails(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaAccountDetails(cPtr, true);
  }

  public MegaPricing getPricing() {
    long cPtr = megaJNI.MegaRequest_getPricing(swigCPtr, this);
    return (cPtr == 0) ? null : new MegaPricing(cPtr, true);
  }

  public int getTransferTag() {
    return megaJNI.MegaRequest_getTransferTag(swigCPtr, this);
  }

  public int getNumDetails() {
    return megaJNI.MegaRequest_getNumDetails(swigCPtr, this);
  }

  public int getTag() {
    return megaJNI.MegaRequest_getTag(swigCPtr, this);
  }

  public MegaRequest() {
    this(megaJNI.new_MegaRequest(), true);
  }

  public final static int TYPE_LOGIN = 0;
  public final static int TYPE_CREATE_FOLDER = TYPE_LOGIN + 1;
  public final static int TYPE_MOVE = TYPE_CREATE_FOLDER + 1;
  public final static int TYPE_COPY = TYPE_MOVE + 1;
  public final static int TYPE_RENAME = TYPE_COPY + 1;
  public final static int TYPE_REMOVE = TYPE_RENAME + 1;
  public final static int TYPE_SHARE = TYPE_REMOVE + 1;
  public final static int TYPE_IMPORT_LINK = TYPE_SHARE + 1;
  public final static int TYPE_EXPORT = TYPE_IMPORT_LINK + 1;
  public final static int TYPE_FETCH_NODES = TYPE_EXPORT + 1;
  public final static int TYPE_ACCOUNT_DETAILS = TYPE_FETCH_NODES + 1;
  public final static int TYPE_CHANGE_PW = TYPE_ACCOUNT_DETAILS + 1;
  public final static int TYPE_UPLOAD = TYPE_CHANGE_PW + 1;
  public final static int TYPE_LOGOUT = TYPE_UPLOAD + 1;
  public final static int TYPE_GET_PUBLIC_NODE = TYPE_LOGOUT + 1;
  public final static int TYPE_GET_ATTR_FILE = TYPE_GET_PUBLIC_NODE + 1;
  public final static int TYPE_SET_ATTR_FILE = TYPE_GET_ATTR_FILE + 1;
  public final static int TYPE_GET_ATTR_USER = TYPE_SET_ATTR_FILE + 1;
  public final static int TYPE_SET_ATTR_USER = TYPE_GET_ATTR_USER + 1;
  public final static int TYPE_RETRY_PENDING_CONNECTIONS = TYPE_SET_ATTR_USER + 1;
  public final static int TYPE_ADD_CONTACT = TYPE_RETRY_PENDING_CONNECTIONS + 1;
  public final static int TYPE_REMOVE_CONTACT = TYPE_ADD_CONTACT + 1;
  public final static int TYPE_CREATE_ACCOUNT = TYPE_REMOVE_CONTACT + 1;
  public final static int TYPE_CONFIRM_ACCOUNT = TYPE_CREATE_ACCOUNT + 1;
  public final static int TYPE_QUERY_SIGNUP_LINK = TYPE_CONFIRM_ACCOUNT + 1;
  public final static int TYPE_ADD_SYNC = TYPE_QUERY_SIGNUP_LINK + 1;
  public final static int TYPE_REMOVE_SYNC = TYPE_ADD_SYNC + 1;
  public final static int TYPE_REMOVE_SYNCS = TYPE_REMOVE_SYNC + 1;
  public final static int TYPE_PAUSE_TRANSFERS = TYPE_REMOVE_SYNCS + 1;
  public final static int TYPE_CANCEL_TRANSFER = TYPE_PAUSE_TRANSFERS + 1;
  public final static int TYPE_CANCEL_TRANSFERS = TYPE_CANCEL_TRANSFER + 1;
  public final static int TYPE_DELETE = TYPE_CANCEL_TRANSFERS + 1;
  public final static int TYPE_REPORT_EVENT = TYPE_DELETE + 1;
  public final static int TYPE_CANCEL_ATTR_FILE = TYPE_REPORT_EVENT + 1;
  public final static int TYPE_GET_PRICING = TYPE_CANCEL_ATTR_FILE + 1;
  public final static int TYPE_GET_PAYMENT_ID = TYPE_GET_PRICING + 1;
  public final static int TYPE_GET_USER_DATA = TYPE_GET_PAYMENT_ID + 1;
  public final static int TYPE_LOAD_BALANCING = TYPE_GET_USER_DATA + 1;
  public final static int TYPE_KILL_SESSION = TYPE_LOAD_BALANCING + 1;
  public final static int TYPE_SUBMIT_PURCHASE_RECEIPT = TYPE_KILL_SESSION + 1;
  public final static int TYPE_CREDIT_CARD_STORE = TYPE_SUBMIT_PURCHASE_RECEIPT + 1;
  public final static int TYPE_UPGRADE_ACCOUNT = TYPE_CREDIT_CARD_STORE + 1;
  public final static int TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS = TYPE_UPGRADE_ACCOUNT + 1;
  public final static int TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS = TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS + 1;
  public final static int TYPE_GET_SESSION_TRANSFER_URL = TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS + 1;
  public final static int TYPE_GET_PAYMENT_METHODS = TYPE_GET_SESSION_TRANSFER_URL + 1;
  public final static int TYPE_INVITE_CONTACT = TYPE_GET_PAYMENT_METHODS + 1;
  public final static int TYPE_REPLY_CONTACT_REQUEST = TYPE_INVITE_CONTACT + 1;
  public final static int TYPE_SUBMIT_FEEDBACK = TYPE_REPLY_CONTACT_REQUEST + 1;
  public final static int TYPE_SEND_EVENT = TYPE_SUBMIT_FEEDBACK + 1;
  public final static int TYPE_CLEAN_RUBBISH_BIN = TYPE_SEND_EVENT + 1;
  public final static int TYPE_SET_ATTR_NODE = TYPE_CLEAN_RUBBISH_BIN + 1;
  public final static int TYPE_CHAT_CREATE = TYPE_SET_ATTR_NODE + 1;
  public final static int TYPE_CHAT_FETCH = TYPE_CHAT_CREATE + 1;
  public final static int TYPE_CHAT_INVITE = TYPE_CHAT_FETCH + 1;
  public final static int TYPE_CHAT_REMOVE = TYPE_CHAT_INVITE + 1;
  public final static int TYPE_CHAT_URL = TYPE_CHAT_REMOVE + 1;
  public final static int TYPE_CHAT_GRANT_ACCESS = TYPE_CHAT_URL + 1;
  public final static int TYPE_CHAT_REMOVE_ACCESS = TYPE_CHAT_GRANT_ACCESS + 1;
  public final static int TYPE_USE_HTTPS_ONLY = TYPE_CHAT_REMOVE_ACCESS + 1;
  public final static int TYPE_SET_PROXY = TYPE_USE_HTTPS_ONLY + 1;

}
