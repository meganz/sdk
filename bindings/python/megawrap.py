#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Temporary name as SWIG auto-generated file name is mega.

import sys
import os
import logging
import time
import threading
import json
import getpass

_wrapper_dir = os.path.join(os.getcwd(), '..', '..', 'bindings', 'python')
_libs_dir = os.path.join(_wrapper_dir, '.libs')
_shared_lib = os.path.join(_libs_dir, '_mega.so')
if os.path.isdir(_wrapper_dir) and os.path.isfile(_shared_lib):
    sys.path.insert(0, _wrapper_dir)  # mega.py
    sys.path.insert(0, _libs_dir)     # _mega.so

from mega import MegaApi



class MegaApi(object):     
        
    def __init__(self, a,b,c,d): 
        self.a = a
        self.b = b
        self.c = c
        self.d = d
        print(d)
                
        
        
        
        
    def add_listener(self, *args): return MegaApi.addListener(self, *args)
    def add_request_listener(self, *args): return MegaApi.addRequestListener(self, *args)
    def add_transfer_listener(self, *args): return MegaApi.addTransferListener(self, *args)
    def add_global_listener(self, *args): return MegaApi.addGlobalListener(self, *args)
    def remove_listener(self, *args): return MegaApi.removeListener(self, *args)
    def remove_request_listener(self, *args): return MegaApi.removeRequestListener(self, *args)
    def remove_transfer_listener(self, *args): return MegaApi.removeTransferListener(self, *args)
    def remove_global_listener(self, *args): return MegaApi.removeGlobalListener(self, *args)
    def get_current_request(self): return MegaApi.getCurrentRequest(self)
    def get_current_transfer(self): return MegaApi.getCurrentTransfer(self)
    def get_current_error(self): return MegaApi.getCurrentError(self)
    def get_current_nodes(self): return MegaApi.getCurrentNodes(self)
    def get_current_users(self): return MegaApi.getCurrentUsers(self)
    def get_base64_pw_key(self, *args): return MegaApi.getBase64PwKey(self, *args)
    def get_string_hash(self, *args): return MegaApi.getStringHash(self, *args)
    def get_session_transfer_URL(self, *args): return MegaApi.getSessionTransferURL(self, *args)
    def retry_pending_connections(self, disconnect=False, includexfers=False, listener=None): return MegaApi.retryPendingConnections(self, disconnect, includexfers, listener)
    def login(self, *args): return MegaApi.login(self, *args)
    def login_to_folder(self, *args): return MegaApi.loginToFolder(self, *args)
    def fast_login(self, *args): return MegaApi.fastLogin(self, *args)
    def kill_session(self, *args): return MegaApi.killSession(self, *args)
    def get_user_data(self, *args): return MegaApi.getUserData(self, *args)
    def dump_session(self): return MegaApi.dumpSession(self)
    def dump_XMPP_session(self): return MegaApi.dumpXMPPSession(self)
    def create_account(self, *args): return MegaApi.createAccount(self, *args)
    def fast_create_account(self, *args): return MegaApi.fastCreateAccount(self, *args)
    def query_signup_link(self, *args): return MegaApi.querySignupLink(self, *args)
    def confirm_account(self, *args): return MegaApi.confirmAccount(self, *args)
    def fast_confirm_account(self, *args): return MegaApi.fastConfirmAccount(self, *args)
    def set_proxy_settings(self, *args): return MegaApi.setProxySettings(self, *args)
    def get_auto_proxy_settings(self): return MegaApi.getAutoProxySettings(self)
    def is_logged_in(self): return MegaApi.isLoggedIn(self)
    def get_my_email(self): return MegaApi.getMyEmail(self)
    def get_my_user_handle(self): return MegaApi.getMyUserHandle(self)
    def create_folder(self, *args): return MegaApi.createFolder(self, *args)
    def move_node(self, *args): return MegaApi.moveNode(self, *args)
    def copy_node(self, *args): return MegaApi.copyNode(self, *args)
    def rename_node(self, *args): return MegaApi.renameNode(self, *args)
    def remove(self, *args): return MegaApi.remove(self, *args)
    def send_file_to_user(self, *args): return MegaApi.sendFileToUser(self, *args)
    def share(self, *args): return MegaApi.share(self, *args)
    def import_file_link(self, *args): return MegaApi.importFileLink(self, *args)
    def get_public_node(self, *args): return MegaApi.getPublicNode(self, *args)
    def get_thumbnail(self, *args): return MegaApi.getThumbnail(self, *args)
    def get_preview(self, *args): return MegaApi.getPreview(self, *args)
    def get_user_avatar(self, *args): return MegaApi.getUserAvatar(self, *args)
    def get_user_attribute(self, *args): return MegaApi.getUserAttribute(self, *args)
    def cancel_get_thumbnail(self, *args): return MegaApi.cancelGetThumbnail(self, *args)
    def cancel_get_preview(self, *args): return MegaApi.cancelGetPreview(self, *args)
    def set_thumbnail(self, *args): return MegaApi.setThumbnail(self, *args)
    def set_preview(self, *args): return MegaApi.setPreview(self, *args)
    def set_avatar(self, *args): return MegaApi.setAvatar(self, *args)
    def set_user_attribute(self, *args): return MegaApi.setUserAttribute(self, *args)
    def export_node(self, *args): return MegaApi.exportNode(self, *args)
    def disable_export(self, *args): return MegaApi.disableExport(self, *args)
    def fetch_nodes(self, listener=None): return MegaApi.fetchNodes(self, listener)
    def get_account_details(self, listener=None): return MegaApi.getAccountDetails(self, listener)
    def get_extendedAccount_details(self, sessions=False, purchases=False, transactions=False, listener=None): return MegaApi.getExtendedAccountDetails(self, sessions, purchases, transactions, listener)
    def get_pricing(self, listener=None): return MegaApi.getPricing(self, listener)
    def get_payment_id(self, *args): return MegaApi.getPaymentId(self, *args)
    def upgrade_account(self, *args): return MegaApi.upgradeAccount(self, *args)
    def submit_purchase_receipt(self, *args): return MegaApi.submitPurchaseReceipt(self, *args)
    def credit_card_store(self, *args): return MegaApi.creditCardStore(self, *args)
    def credit_card_query_subscriptions(self, listener=None): return MegaApi.creditCardQuerySubscriptions(self, listener)
    def credit_card_cancel_subscriptions(self, *args): return MegaApi.creditCardCancelSubscriptions(self, *args)
    def get_payment_methods(self, listener=None): return MegaApi.getPaymentMethods(self, listener)
    def export_master_key(self): return MegaApi.exportMasterKey(self)
    def change_password(self, *args): return MegaApi.changePassword(self, *args)
    def add_contact(self, *args): return MegaApi.addContact(self, *args)
    def invite_contact(self, *args): return MegaApi.inviteContact(self, *args)
    def reply_contact_request(self, *args): return MegaApi.replyContactRequest(self, *args)
    def remove_contact(self, *args): return MegaApi.removeContact(self, *args)
    def logout(self, listener=None): return MegaApi.logout(self, listener)
    def local_logout(self, listener=None): return MegaApi.localLogout(self, listener)
    def submit_feedback(self, *args): return MegaApi.submitFeedback(self, *args)
    def report_debug_event(self, *args): return MegaApi.reportDebugEvent(self, *args)
    def start_upload(self, *args): return MegaApi.startUpload(self, *args)
    def start_download(self, *args): return MegaApi.startDownload(self, *args)
    def start_streaming(self, *args): return MegaApi.startStreaming(self, *args)
    def cancel_transfer(self, *args): return MegaApi.cancelTransfer(self, *args)
    def cancel_transfer_by_tag(self, *args): return MegaApi.cancelTransferByTag(self, *args)
    def cancel_transfers(self, *args): return MegaApi.cancelTransfers(self, *args)
    def pause_transfers(self, *args): return MegaApi.pauseTransfers(self, *args)
    def are_tansfers_paused(self, *args): return MegaApi.areTansfersPaused(self, *args)
    def set_upload_limit(self, *args): return MegaApi.setUploadLimit(self, *args)
    def set_download_method(self, *args): return MegaApi.setDownloadMethod(self, *args)
    def set_upload_method(self, *args): return MegaApi.setUploadMethod(self, *args)
    def get_download_method(self): return MegaApi.getDownloadMethod(self)
    def get_upload_method(self): return MegaApi.getUploadMethod(self)
    def get_transfer_by_tag(self, *args): return MegaApi.getTransferByTag(self, *args)
    def get_transfers(self, *args): return MegaApi.getTransfers(self, *args)
    def update(self): return MegaApi.update(self)
    def is_waiting(self): return MegaApi.isWaiting(self)
    def get_num_pending_uploads(self): return MegaApi.getNumPendingUploads(self)
    def get_num_pending_downloads(self): return MegaApi.getNumPendingDownloads(self)
    def get_total_uploads(self): return MegaApi.getTotalUploads(self)
    def get_total_downloads(self): return MegaApi.getTotalDownloads(self)
    def reset_total_downloads(self): return MegaApi.resetTotalDownloads(self)
    def reset_total_uploads(self): return MegaApi.resetTotalUploads(self)
    def get_total_downloaded_bytes(self): return MegaApi.getTotalDownloadedBytes(self)
    def getTotalUploadedBytes(self): return MegaApi.getTotalUploadedBytes(self)
    def updateStats(self): return MegaApi.updateStats(self)
    def get_num_children(self, *args): return MegaApi.getNumChildren(self, *args)
    def get_num_child_files(self, *args): return MegaApi.getNumChildFiles(self, *args)
    def get_num_child_folders(self, *args): return MegaApi.getNumChildFolders(self, *args)
    def get_children(self, *args): return MegaApi.getChildren(self, *args)
    def get_index(self, *args): return MegaApi.getIndex(self, *args)
    def get_child_node(self, *args): return MegaApi.getChildNode(self, *args)
    def get_parent_node(self, *args): return MegaApi.getParentNode(self, *args)
    def get_node_path(self, *args): return MegaApi.getNodePath(self, *args)
    def get_node_by_path(self, *args): return MegaApi.getNodeByPath(self, *args)
    def get_node_by_handle(self, *args): return MegaApi.getNodeByHandle(self, *args)
    def get_contact_request_by_handle(self, *args): return MegaApi.getContactRequestByHandle(self, *args)
    def get_contacts(self): return MegaApi.getContacts(self)
    def get_contact(self, *args): return MegaApi.getContact(self, *args)
    def get_in_shares(self, *args): return MegaApi.getInShares(self, *args)
    def is_shared(self, *args): return MegaApi.isShared(self, *args)
    def get_out_shares(self, *args): return MegaApi.getOutShares(self, *args)
    def get_pending_out_shares(self, *args): return MegaApi.getPendingOutShares(self, *args)
    def get_incoming_contact_requests(self): return MegaApi.getIncomingContactRequests(self)
    def get_outgoing_contact_requests(self): return MegaApi.getOutgoingContactRequests(self)
    def get_access(self, *args): return MegaApi.getAccess(self, *args)
    def get_size(self, *args): return MegaApi.getSize(self, *args)
    def get_fingerprint(self, *args): return MegaApi.getFingerprint(self, *args)
    def get_node_by_fingerprint(self, *args): return MegaApi.getNodeByFingerprint(self, *args)
    def has_fingerprint(self, *args): return MegaApi.hasFingerprint(self, *args)
    def get_CRC(self, *args): return MegaApi.getCRC(self, *args)
    def get_node_by_CRC(self, *args): return MegaApi.getNodeByCRC(self, *args)
    def check_access(self, *args): return MegaApi.checkAccess(self, *args)
    def check_move(self, *args): return MegaApi.checkMove(self, *args)
    def get_root_node(self): return MegaApi.getRootNode(self)
    def get_inbox_node(self): return MegaApi.getInboxNode(self)
    def get_rubbish_node(self): return MegaApi.getRubbishNode(self)
    def search(self, *args): return MegaApi.search(self, *args)
    def process_mega_tree(self, *args): return MegaApi.processMegaTree(self, *args)
    def create_public_file_node(self, *args): return MegaApi.createPublicFileNode(self, *args)
    def create_public_folder_node(self, *args): return MegaApi.createPublicFolderNode(self, *args)
    def get_version(self): return MegaApi.getVersion(self)
    def get_user_agent(self): return MegaApi.getUserAgent(self)
    def change_api_url(self, *args): return MegaApi.changeApiUrl(self, *args)
    def escape_fs_incompatible(self, *args): return MegaApi.escapeFsIncompatible(self, *args)
    def unescape_fs_incompatible(self, *args): return MegaApi.unescapeFsIncompatible(self, *args)
    def create_thumbnail(self, *args): return MegaApi.createThumbnail(self, *args)
    def create_preview(self, *args): return MegaApi.createPreview(self, *args)
    