/*
 * CrudExample.java
 *
 * This file is part of the Mega SDK Java bindings example code.
 *
 * Created: 2015-07-09 Guy K. Kloss <gk@mega.co.nz>
 * Changed:
 *
 * (c) 2015 by Mega Limited, Auckland, New Zealand
 *     https://mega.nz/
 *     Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

package nz.mega.bindingsample;

import java.io.Console;
import java.util.ArrayList;
import java.util.logging.Logger;

import nz.mega.sdk.MegaAccountDetails;
import nz.mega.sdk.MegaApiJava;
import nz.mega.sdk.MegaError;
import nz.mega.sdk.MegaGlobalListenerInterface;
import nz.mega.sdk.MegaNode;
import nz.mega.sdk.MegaRequest;
import nz.mega.sdk.MegaRequestListenerInterface;
import nz.mega.sdk.MegaTransfer;
import nz.mega.sdk.MegaTransferListenerInterface;
import nz.mega.sdk.MegaUser;
import nz.mega.sdk.MegaContactRequest;

/**
 * A simple example implementation that demonstrates a CRUD (create, read,
 * update, delete) scenario for file storage.
 * 
 * Note: To "force" the fully asynchronous Mega API into a mocked synchronous
 *       mode of operation, a condition variable was used to signal to and wait
 *       for synchronisation points according to this tutorial:
 *       http://tutorials.jenkov.com/java-concurrency/thread-signaling.html
 *
 * @author Guy K. Kloss
 */
public class CrudExample implements MegaRequestListenerInterface,
                        MegaTransferListenerInterface,
                        MegaGlobalListenerInterface {

    /** Reference to the Mega API object. */
    static MegaApiJava megaApi = null;

    /**
     * Mega SDK application key.
     * Generate one for free here: https://mega.nz/#sdk
     */
    static final String APP_KEY = "YYJwAIRI";
    
    /** Condition variable to signal asynchronous continuation. */
    private Object continueEvent = null;

    /** Signal for condition variable to indicate signalling. */
    private boolean wasSignalled = false;

    /** Root node of the logged in account. */
    private MegaNode rootNode = null;
    
    /** Java logging utility. */
    private static final Logger log = Logger.getLogger(CrudExample.class.getName());
    
    /** Constructor. */
    public CrudExample() {
        // Make a new condition variable to signal continuation.
        this.continueEvent = new Object();
        
        // Get reference to Mega API.
        if (megaApi == null) {
            String path = System.getProperty("user.dir");
            megaApi = new MegaApiJava(CrudExample.APP_KEY, path);
        }
    }
    
    public static void main(String[] args) {
        CrudExample myListener = new CrudExample();
        
        // Execute a sequence of jobs on the Mega API.
        long startTime = System.currentTimeMillis();
        
        // Log in.
        log.info("*** start: login ***");
        Console console = System.console();
        String email = console.readLine("Email: ");
        char[] passwordArray = console.readPassword("Password: ");
        String password = new String(passwordArray);
        synchronized(myListener.continueEvent) {
            megaApi.login(email, password, myListener);
            while (!myListener.wasSignalled) {
                try {
                    myListener.continueEvent.wait();
                } catch(InterruptedException e) {
                    log.warning("login interrupted: " + e.toString());
                }
            }
            myListener.wasSignalled = false;
        }
        // Set our current working directory.
        MegaNode cwd = myListener.rootNode;
        log.info("*** done: login ***");
        
        // Who am I.
        log.info("*** start: whoami ***");
        log.info("My email: " + megaApi.getMyEmail());
        synchronized(myListener.continueEvent) {
            megaApi.getAccountDetails(myListener);
            while (!myListener.wasSignalled) {
                try {
                    myListener.continueEvent.wait();
                } catch(InterruptedException e) {
                    log.warning("whoami interrupted: " + e.toString());
                }
            }
            myListener.wasSignalled = false;
        }
        log.info("*** done: whoami ***");
        
        // Make a directory.
        log.info("*** start: mkdir ***");
        MegaNode check = megaApi.getNodeByPath("sandbox", cwd);
        if (check == null) {
            synchronized(myListener.continueEvent) {
                megaApi.createFolder("sandbox", cwd, myListener);
                while (!myListener.wasSignalled) {
                    try {
                        myListener.continueEvent.wait();
                    } catch(InterruptedException e) {
                        log.warning("mkdir interrupted: " + e.toString());
                    }
                }
                myListener.wasSignalled = false;
            }
        } else {
            log.info("Path already exists: " + megaApi.getNodePath(check));
        }
        log.info("*** done: mkdir ***");

        // Now go and play in the sandbox.
        log.info("*** start: cd ***");
        MegaNode node = megaApi.getNodeByPath("sandbox", cwd);
        if (node == null) {
            log.warning("No such file or directory: sandbox");
        }
        if (node.getType() == MegaNode.TYPE_FOLDER) {
            cwd = node;
        } else {
            log.warning("Not a directory: sandbox");
        }
        log.info("*** done: cd ***");

        // Upload a file (create).
        log.info("*** start: upload ***");
        synchronized(myListener.continueEvent) {
            megaApi.startUpload("README.md"
            , cwd   /*parent node*/
            , null  /*filename*/
            , 0     /*mtime*/
            , null  /*appData*/
            , false /*isSourceTemporary*/
            , false /*startFirst*/
            , null  /*cancelToken*/
            , myListener);

            while (!myListener.wasSignalled) {
                try {
                    myListener.continueEvent.wait();
                } catch(InterruptedException e) {
                    log.warning("upload interrupted: " + e.toString());
                }
            }
            myListener.wasSignalled = false;
        }
        log.info("*** done: upload ***");

        // Download a file (read).
        log.info("*** start: download ***");
        node = megaApi.getNodeByPath("README.md", cwd);
        if (node != null) {
            synchronized(myListener.continueEvent) {
                megaApi.startDownload(node
                , "README_returned.md" /*local path*/
                , null 		/*custom name*/
                , null			/*app data*/
                , false		/*start first*/
                , null			/*cancel token*/
                , MegaTransfer.COLLISION_CHECK_FINGERPRINT /* collisionCheck*/
                , MegaTransfer.COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution*/
                , myListener);

                while (!myListener.wasSignalled) {
                    try {
                        myListener.continueEvent.wait();
                    } catch(InterruptedException e) {
                        log.warning("download interrupted: " + e.toString());
                    }
                }
                myListener.wasSignalled = false;
            }
        } else {
            log.warning("Node not found: README.md");
        }
        log.info("*** done: download ***");

        // Change a file (update).
        // Note: A new upload won't overwrite, but create a new node with same
        //       name!
        log.info("*** start: update ***");
        MegaNode oldNode = megaApi.getNodeByPath("README.md", cwd);
        synchronized(myListener.continueEvent) {
            megaApi.startUpload("README.md"
            , cwd   /*parent node*/
            , null  /*filename*/
            , 0     /*mtime*/
            , null  /*appData*/
            , false /*isSourceTemporary*/
            , false /*startFirst*/
            , null  /*cancelToken*/
            , myListener);

            while (!myListener.wasSignalled) {
                try {
                    myListener.continueEvent.wait();
                } catch(InterruptedException e) {
                    log.warning("upload interrupted: " + e.toString());
                }
            }
            myListener.wasSignalled = false;
        }
        if (oldNode != null) {
            // Remove the old node with the same name.
            synchronized(myListener.continueEvent) {
                megaApi.remove(oldNode, myListener);
                while (!myListener.wasSignalled) {
                    try {
                        myListener.continueEvent.wait();
                    } catch(InterruptedException e) {
                        log.warning("remove interrupted: " + e.toString());
                    }
                }
                myListener.wasSignalled = false;
            }
        } else {
            log.info("No old file node needs removing");
        }
        log.info("*** done: update ***");
        
        // Delete a file.
        log.info("*** start: delete ***");
        node = megaApi.getNodeByPath("README.md", cwd);
        if (node != null) {
            synchronized(myListener.continueEvent) {
                megaApi.remove(node, myListener);
                while (!myListener.wasSignalled) {
                    try {
                        myListener.continueEvent.wait();
                    } catch(InterruptedException e) {
                        log.warning("remove interrupted: " + e.toString());
                    }
                }
                myListener.wasSignalled = false;
            }
        } else {
            log.warning("Node not found: README.md");
        }
        log.info("*** done: delete ***");
    
        // Logout.
        log.info("*** start: logout ***");
        synchronized(myListener.continueEvent) {
            megaApi.logout(myListener);
            while (!myListener.wasSignalled) {
                try {
                    myListener.continueEvent.wait();
                } catch(InterruptedException e) {
                    log.warning("remove interrupted: " + e.toString());
                }
            }
            myListener.wasSignalled = false;
        }
        myListener.rootNode = null;
        log.info("*** done: logout ***");
        
        log.info("Total time taken: "
                 + ((System.currentTimeMillis() - startTime) / 1000) + " s");
    }

    
    // Implementation of listener methods.
    
    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaRequestListenerInterface#onRequestStart(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaRequest)
     */
    @Override
    public void onRequestStart(MegaApiJava api, MegaRequest request) {
        log.fine("Request start (" + request.getRequestString() + ")");
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaRequestListenerInterface#onRequestUpdate(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaRequest)
     */
    @Override
    public void onRequestUpdate(MegaApiJava api, MegaRequest request) {
        log.fine("Request update (" + request.getRequestString() + ")");

        if (request.getType() == MegaRequest.TYPE_FETCH_NODES) {
            if (request.getTotalBytes() > 0) {
                double progressValue = 100.0 * request.getTransferredBytes()
                        / request.getTotalBytes();
                if ((progressValue > 99) || (progressValue < 0)) {
                    progressValue = 100;
                }
                log.fine("Preparing nodes ... " + String.valueOf((int)progressValue) + "%");
            }
        }
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaRequestListenerInterface#onRequestFinish(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaRequest, nz.mega.sdk.MegaError)
     */
    @Override
    public void onRequestFinish(MegaApiJava api, MegaRequest request, MegaError e) {
        log.fine("Request finish (" + request.getRequestString()
                 + "); Result: " + e.toString());

        int requestType = request.getType();
        
        if (requestType == MegaRequest.TYPE_LOGIN) {
            if (e.getErrorCode() != MegaError.API_OK) {
                String errorMessage = e.getErrorString();
                if (e.getErrorCode() == MegaError.API_ENOENT) {
                    errorMessage = "Login error: Incorrect email or password!";
                }
                log.severe(errorMessage);
                return;
            }
            megaApi.fetchNodes(this);
        } else if (requestType == MegaRequest.TYPE_FETCH_NODES) {
            this.rootNode = api.getRootNode();
        } else if (requestType == MegaRequest.TYPE_ACCOUNT_DETAILS) {
            MegaAccountDetails accountDetails = request.getMegaAccountDetails();
            log.info("Account details received");
            log.info("Storage: " + accountDetails.getStorageUsed()
                     + " of " + accountDetails.getStorageMax()
                     + " (" + String.valueOf((int)(100.0 * accountDetails.getStorageUsed()
                                                   / accountDetails.getStorageMax()))
                     + " %)");
            log.info("Pro level: " + accountDetails.getProLevel());
        }

        // Send the continue event so our synchronised code continues.
        if (requestType != MegaRequest.TYPE_LOGIN) {
            synchronized(this.continueEvent){
                this.wasSignalled = true;
                this.continueEvent.notify();
            }
        }
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaRequestListenerInterface#onRequestTemporaryError(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaRequest, nz.mega.sdk.MegaError)
     */
    @Override
    public void onRequestTemporaryError(MegaApiJava api, MegaRequest request, MegaError e) {
        log.warning("Request temporary error (" + request.getRequestString()
                    + "); Error: " + e.toString());
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaTransferListenerInterface#onTransferStart(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaTransfer)
     */
    @Override
    public void onTransferStart(MegaApiJava api, MegaTransfer transfer) {
        log.fine("Transfer start: " + transfer.getFileName());
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaTransferListenerInterface#onTransferFinish(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaTransfer, nz.mega.sdk.MegaError)
     */
    @Override
    public void onTransferFinish(MegaApiJava api, MegaTransfer transfer,
                                 MegaError e) {
        log.fine("Transfer finished (" + transfer.getFileName()
                 + "); Result: " + e.toString() + " ");
        // Signal the other thread we're done.
        synchronized(this.continueEvent){
            this.wasSignalled = true;
            this.continueEvent.notify();
        }
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaTransferListenerInterface#onTransferUpdate(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaTransfer)
     */
    @Override
    public void onTransferUpdate(MegaApiJava api, MegaTransfer transfer) {
        log.fine("Transfer finished (" + transfer.getFileName()
                 + "): " + transfer.getSpeed() + " B/s ");
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaTransferListenerInterface#onTransferTemporaryError(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaTransfer, nz.mega.sdk.MegaError)
     */
    @Override
    public void onTransferTemporaryError(MegaApiJava api,
                                         MegaTransfer transfer, MegaError e) {
        log.warning("Transfer temporary error (" + transfer.getFileName()
                    + "); Error: " + e.toString());
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaTransferListenerInterface#onTransferData(nz.mega.sdk.MegaApiJava, nz.mega.sdk.MegaTransfer, byte[])
     */
    @Override
    public boolean onTransferData(MegaApiJava api, MegaTransfer transfer,
                                  byte[] buffer) {
        log.fine("Got transfer data.");
        return true;
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaGlobalListenerInterface#onUsersUpdate(nz.mega.sdk.MegaApiJava, java.util.ArrayList)
     */
    @Override
    public void onUsersUpdate(MegaApiJava api, ArrayList<MegaUser> users) {
        // TODO Auto-generated method stub
        
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaGlobalListenerInterface#onNodesUpdate(nz.mega.sdk.MegaApiJava, java.util.ArrayList)
     */
    @Override
    public void onNodesUpdate(MegaApiJava api, ArrayList<MegaNode> nodes) {
        if (nodes != null) {
            log.info("Nodes updated (" + nodes.size() + ")");
        }

        // Signal the other thread we're done.
        synchronized(this.continueEvent){
            this.wasSignalled = true;
            this.continueEvent.notify();
        }
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaGlobalListenerInterface#onContactRequestsUpdate(nz.mega.sdk.MegaApiJava, java.util.ArrayList)
     */
    @Override
    public void onContactRequestsUpdate(MegaApiJava api, ArrayList<MegaContactRequest> contactRequests) {
        if (contactRequests != null && contactRequests.size() != 0) {
            log.info("Contact request received (" + contactRequests.size() + ")");
        }
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaGlobalListenerInterface#onAccountUpdate(nz.mega.sdk.MegaApiJava)
     */
    @Override
    public void onAccountUpdate(MegaApiJava api) {
        log.info("Account updated");
    }

    /**
     * {@inheritDoc}
     *
     * @see nz.mega.sdk.MegaGlobalListenerInterface#onReloadNeeded(nz.mega.sdk.MegaApiJava)
     */
    @Override
    public void onReloadNeeded(MegaApiJava api) {
        // TODO Auto-generated method stub
        
    }
}
