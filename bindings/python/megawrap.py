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

import mega



class MegaApi(object):     
        
    def __init__(self, a, b, c, d): 
        self.a = a
        self.b = b
        self.c = c
        self.d = d
        self.api = mega.MegaApi(self.a, self.b, self.c, self.d)
        
    def add_listener(self, *args): 
        return mega.MegaApi.addListener(self.api, *args)

    def add_request_listener(self, *args): 
        return mega.MegaApi.addRequestListener(self.api, *args)
    
    def add_transfer_listener(self, *args): 
        return mega.MegaApi.addTransferListener(self.api, *args)
    
    def add_global_listener(self, *args): 
        return mega.MegaApi.addGlobalListener(self.api, *args)
    
    def remove_listener(self, *args): 
        return mega.MegaApi.removeListener(self.api, *args)
    
    def remove_request_listener(self, *args): 
        return mega.MegaApi.removeRequestListener(self.api, *args)
    
    def remove_transfer_listener(self, *args): 
        return mega.MegaApi.removeTransferListener(self.api, *args)
    
    def remove_global_listener(self, *args): 
        return mega.MegaApi.removeGlobalListener(self.api, *args)
    
    def get_current_request(self): 
        return mega.MegaApi.getCurrentRequest(self.api)
    
    def get_current_transfer(self): 
        return mega.MegaApi.getCurrentTransfer(self.api)
    
    def get_current_error(self): 
        return mega.MegaApi.getCurrentError(self.api)
    
    def get_current_nodes(self): 
        return mega.MegaApi.getCurrentNodes(self.api)
    
    def get_current_users(self): 
        return mega.MegaApi.getCurrentUsers(self.api)
    
    def get_base64_pw_key(self, *args): 
        return mega.MegaApi.getBase64PwKey(self.api, *args)
    
    def get_string_hash(self, *args): 
        return mega.MegaApi.getStringHash(self.api, *args)
    
    def get_session_transfer_URL(self, *args): 
        return mega.MegaApi.getSessionTransferURL(self.api, *args)
    
    def retry_pending_connections(self, disconnect=False, includexfers=False, listener=None): 
        return mega.MegaApi.retryPendingConnections(self.api, disconnect, includexfers, listener)
    
    def login(self, *args): 
        return mega.MegaApi.login(self.api, *args)
    
    def login_to_folder(self, *args): 
        return mega.MegaApi.loginToFolder(self.api, *args)
    
    def fast_login(self, *args): 
        return mega.MegaApi.fastLogin(self.api, *args)
    
    def kill_session(self, *args): 
        return mega.MegaApi.killSession(self.api, *args)
    
    def get_user_data(self, *args): 
        return mega.MegaApi.getUserData(self.api, *args)
    
    def dump_session(self): 
        return mega.MegaApi.dumpSession(self.api)
    
    def dump_XMPP_session(self): 
        return mega.MegaApi.dumpXMPPSession(self.api)
    
    def create_account(self, *args): 
        return mega.MegaApi.createAccount(self.api, *args)
    
    def fast_create_account(self, *args): 
        return mega.MegaApi.fastCreateAccount(self.api, *args)
    
    def query_signup_link(self, *args): 
        return mega.MegaApi.querySignupLink(self.api, *args)
    
    def confirm_account(self, *args): 
        return mega.MegaApi.confirmAccount(self.api, *args)
    
    def fast_confirm_account(self, *args): 
        return mega.MegaApi.fastConfirmAccount(self.api, *args)
    
    def set_proxy_settings(self, *args): 
        return mega.MegaApi.setProxySettings(self.api, *args)
    
    def get_auto_proxy_settings(self): 
        return mega.MegaApi.getAutoProxySettings(self.api)
    
    def is_logged_in(self): 
        return mega.MegaApi.isLoggedIn(self.api)
    
    def get_my_email(self): 
        return mega.MegaApi.getMyEmail(self.api)
    
    def get_my_user_handle(self): 
        return mega.MegaApi.getMyUserHandle(self.api)
    
    def create_folder(self, *args): 
        return mega.MegaApi.createFolder(self.api, *args)
    
    def move_node(self, *args): 
        return mega.MegaApi.moveNode(self.api, *args)
    
    def copy_node(self, *args): 
        return mega.MegaApi.copyNode(self.api, *args)
    
    def rename_node(self, *args): 
        return mega.MegaApi.renameNode(self.api, *args)
    
    def remove(self, *args): 
        return mega.MegaApi.remove(self.api, *args)
    
    def send_file_to_user(self, *args): 
        return mega.MegaApi.sendFileToUser(self.api, *args)
    
    def share(self, *args): 
        return mega.MegaApi.share(self.api, *args)
    
    def import_file_link(self, *args): 
        return mega.MegaApi.importFileLink(self.api, *args)
    
    def get_public_node(self, *args): 
        return mega.MegaApi.getPublicNode(self.api, *args)
    
    def get_thumbnail(self, *args): 
        return mega.MegaApi.getThumbnail(self.api, *args)
    
    def get_preview(self, *args): 
        return mega.MegaApi.getPreview(self.api, *args)
    
    def get_user_avatar(self, *args): 
        return mega.MegaApi.getUserAvatar(self.api, *args)
    
    def get_user_attribute(self, *args): 
        return mega.MegaApi.getUserAttribute(self.api, *args)
    
    def cancel_get_thumbnail(self, *args): 
        return mega.MegaApi.cancelGetThumbnail(self.api, *args)
    
    def cancel_get_preview(self, *args): 
        return mega.MegaApi.cancelGetPreview(self.api, *args)
    
    def set_thumbnail(self, *args): 
        return mega.MegaApi.setThumbnail(self.api, *args)
    
    def set_preview(self, *args): 
        return mega.MegaApi.setPreview(self.api, *args)
    
    def set_avatar(self, *args): 
        return mega.MegaApi.setAvatar(self.api, *args)
    
    def set_user_attribute(self, *args): 
        return mega.MegaApi.setUserAttribute(self.api, *args)
    
    def export_node(self, *args): 
        return mega.MegaApi.exportNode(self.api, *args)
    
    def disable_export(self, *args): 
        return mega.MegaApi.disableExport(self.api, *args)
    
    def fetch_nodes(self, listener=None): 
        return mega.MegaApi.fetchNodes(self.api, listener)
    
    def get_account_details(self, listener=None): 
        return mega.MegaApi.getAccountDetails(self.api, listener)
    
    def get_extendedAccount_details(self, sessions=False, purchases=False, transactions=False, listener=None): 
        return mega.MegaApi.getExtendedAccountDetails(self.api, sessions, purchases, transactions, listener)
    
    def get_pricing(self, listener=None): 
        return mega.MegaApi.getPricing(self.api, listener)
    
    def get_payment_id(self, *args): 
        return mega.MegaApi.getPaymentId(self.api, *args)
    
    def upgrade_account(self, *args): 
        return mega.MegaApi.upgradeAccount(self.api, *args)
    
    def submit_purchase_receipt(self, *args): 
        return mega.MegaApi.submitPurchaseReceipt(self.api, *args)
    
    def credit_card_store(self, *args): 
        return mega.MegaApi.creditCardStore(self.api, *args)
    
    def credit_card_query_subscriptions(self, listener=None): 
        return mega.MegaApi.creditCardQuerySubscriptions(self.api, listener)
    
    def credit_card_cancel_subscriptions(self, *args): 
        return mega.MegaApi.creditCardCancelSubscriptions(self.api, *args)
    
    def get_payment_methods(self, listener=None): 
        return mega.MegaApi.getPaymentMethods(self.api, listener)
    
    def export_master_key(self): 
        return mega.MegaApi.exportMasterKey(self.api)
    
    def change_password(self, *args): 
        return mega.MegaApi.changePassword(self.api, *args)
    
    def add_contact(self, *args): 
        return mega.MegaApi.addContact(self.api, *args)
    
    def invite_contact(self, *args): 
        return mega.MegaApi.inviteContact(self.api, *args)
    
    def reply_contact_request(self, *args): 
        return mega.MegaApi.replyContactRequest(self.api, *args)
    
    def remove_contact(self, *args): 
        return mega.MegaApi.removeContact(self.api, *args)
    
    def logout(self, listener=None): 
        return mega.MegaApi.logout(self.api, listener)
    
    def local_logout(self, listener=None): 
        return mega.MegaApi.localLogout(self.api, listener)
    
    def submit_feedback(self, *args): 
        return mega.MegaApi.submitFeedback(self.api, *args)
    
    def report_debug_event(self, *args): 
        return mega.MegaApi.reportDebugEvent(self.api, *args)
    
    def start_upload(self, *args): 
        return mega.MegaApi.startUpload(self.api, *args)
    
    def start_download(self, *args): 
        return mega.MegaApi.startDownload(self.api, *args)
    
    def start_streaming(self, *args): 
        return mega.MegaApi.startStreaming(self.api, *args)
    
    def cancel_transfer(self, *args): 
        return mega.MegaApi.cancelTransfer(self.api, *args)
    
    def cancel_transfer_by_tag(self, *args): 
        return mega.MegaApi.cancelTransferByTag(self.api, *args)
    
    def cancel_transfers(self, *args): 
        return mega.MegaApi.cancelTransfers(self.api, *args)
    
    def pause_transfers(self, *args): 
        return mega.MegaApi.pauseTransfers(self.api, *args)
    
    def are_tansfers_paused(self, *args): 
        return mega.MegaApi.areTansfersPaused(self.api, *args)
    
    def set_upload_limit(self, *args): 
        return mega.MegaApi.setUploadLimit(self.api, *args)
    
    def set_download_method(self, *args): 
        return mega.MegaApi.setDownloadMethod(self.api, *args)
    
    def set_upload_method(self, *args): 
        return mega.MegaApi.setUploadMethod(self.api, *args)
    
    def get_download_method(self): 
        return mega.MegaApi.getDownloadMethod(self.api)
    
    def get_upload_method(self): 
        return mega.MegaApi.getUploadMethod(self.api)
    
    def get_transfer_by_tag(self, *args): 
        return mega.MegaApi.getTransferByTag(self.api, *args)
    
    def get_transfers(self, *args): 
        return mega.MegaApi.getTransfers(self.api, *args)
    
    def update(self): 
        return mega.MegaApi.update(self.api)
    
    def is_waiting(self): 
        return mega.MegaApi.isWaiting(self.api)
    
    def get_num_pending_uploads(self): 
        return mega.MegaApi.getNumPendingUploads(self.api)
    
    def get_num_pending_downloads(self): 
        return mega.MegaApi.getNumPendingDownloads(self.api)
    
    def get_total_uploads(self): 
        return mega.MegaApi.getTotalUploads(self.api)
    
    def get_total_downloads(self): 
        return mega.MegaApi.getTotalDownloads(self.api)
    
    def reset_total_downloads(self): 
        return mega.MegaApi.resetTotalDownloads(self.api)
    
    def reset_total_uploads(self): 
        return mega.MegaApi.resetTotalUploads(self.api)
    
    def get_total_downloaded_bytes(self): 
        return mega.MegaApi.getTotalDownloadedBytes(self.api)
    
    def getTotalUploadedBytes(self): 
        return mega.MegaApi.getTotalUploadedBytes(self.api)
    
    def updateStats(self): 
        return mega.MegaApi.updateStats(self.api)
    
    def get_num_children(self, *args): 
        return mega.MegaApi.getNumChildren(self.api, *args)
    
    def get_num_child_files(self, *args): 
        return mega.MegaApi.getNumChildFiles(self.api, *args)
    
    def get_num_child_folders(self, *args): 
        return mega.MegaApi.getNumChildFolders(self.api, *args)
    
    def get_children(self, *args): 
        return mega.MegaApi.getChildren(self.api, *args)
    
    def get_index(self, *args): 
        return mega.MegaApi.getIndex(self.api, *args)
    
    def get_child_node(self, *args): 
        return mega.MegaApi.getChildNode(self.api, *args)
    
    def get_parent_node(self, *args): 
        return mega.MegaApi.getParentNode(self, *args)
    
    def get_node_path(self, *args): 
        return mega.MegaApi.getNodePath(self.api, *args)
    
    def get_node_by_path(self, *args): 
        return mega.MegaApi.getNodeByPath(self.api, *args)
    
    def get_node_by_handle(self, *args): 
        return mega.MegaApi.getNodeByHandle(self.api, *args)
    
    def get_contact_request_by_handle(self, *args): 
        return mega.MegaApi.getContactRequestByHandle(self.api, *args)
    
    def get_contacts(self): 
        return mega.MegaApi.getContacts(self.api)
    
    def get_contact(self, *args): 
        return mega.MegaApi.getContact(self.api, *args)
    
    def get_in_shares(self, *args): 
        return mega.MegaApi.getInShares(self.api, *args)
    
    def is_shared(self, *args): 
        return mega.MegaApi.isShared(self.api, *args)
    
    def get_out_shares(self, *args): 
        return mega.MegaApi.getOutShares(self.api, *args)
    
    def get_pending_out_shares(self, *args): 
        return mega.MegaApi.getPendingOutShares(self.api, *args)
    
    def get_incoming_contact_requests(self): 
        return mega.MegaApi.getIncomingContactRequests(self.api)
    
    def get_outgoing_contact_requests(self):
        return mega.MegaApi.getOutgoingContactRequests(self.api)
    
    def get_access(self, *args): 
        return mega.MegaApi.getAccess(self.api, *args)
    
    def get_size(self, *args): 
        return mega.MegaApi.getSize(self.api, *args)
    
    def get_fingerprint(self, *args): 
        return mega.MegaApi.getFingerprint(self.api, *args)
    
    def get_node_by_fingerprint(self, *args): 
        return mega.MegaApi.getNodeByFingerprint(self.api, *args)
    
    def has_fingerprint(self, *args): 
        return mega.MegaApi.hasFingerprint(self.api, *args)
    
    def get_CRC(self, *args): 
        return mega.MegaApi.getCRC(self.api, *args)
    
    def get_node_by_CRC(self, *args): 
        return mega.MegaApi.getNodeByCRC(self.api, *args)
    
    def check_access(self, *args): 
        return mega.MegaApi.checkAccess(self.api, *args)
    
    def check_move(self, *args): 
        return mega.MegaApi.checkMove(self.api, *args)
    
    def get_root_node(self): 
        return mega.MegaApi.getRootNode(self.api)
    
    def get_inbox_node(self): 
        return mega.MegaApi.getInboxNode(self.api)
    
    def get_rubbish_node(self): 
        return mega.MegaApi.getRubbishNode(self.api)
    
    def search(self, *args): 
        return mega.MegaApi.search(self.api, *args)
    
    def process_mega_tree(self, *args): 
        return mega.MegaApi.processMegaTree(self.api, *args)
    
    def create_public_file_node(self, *args): 
        return mega.MegaApi.createPublicFileNode(self.api, *args)
    
    def create_public_folder_node(self, *args): 
        return mega.MegaApi.createPublicFolderNode(self.api, *args)
    
    def get_version(self): 
        return mega.MegaApi.getVersion(self.api)
    
    def get_user_agent(self): 
        return mega.MegaApi.getUserAgent(self.api)
    
    def change_api_url(self, *args): 
        return mega.MegaApi.changeApiUrl(self.api, *args)
    
    def escape_fs_incompatible(self, *args): 
        return mega.MegaApi.escapeFsIncompatible(self.api, *args)
    
    def unescape_fs_incompatible(self, *args): 
        return mega.MegaApi.unescapeFsIncompatible(self.api, *args)
    
    def create_thumbnail(self, *args): 
        return mega.MegaApi.createThumbnail(self.api, *args)
    
    def create_preview(self, *args): 
        return mega.MegaApi.createPreview(self.api, *args)
    