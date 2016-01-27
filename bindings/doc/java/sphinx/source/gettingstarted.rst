.. _gettingstarted:

===============
Getting Started
===============

------------
Introduction
------------

.. nature of binding: uses SWIG library to build, then uses API classes to improve usability of raw SWIG bindings, working async in C++

.. Use https://mega.nz/#doc as reference material

.. reST standards & markup http://sphinx-doc.org/rest.html

MEGA, `The Privacy Company`, is a Secure Cloud Storage provider that protects your data using automatically managed end-to-end encryption.

All files stored on MEGA are encrypted. All data transfers from and to MEGA are encrypted. While most cloud storage providers claim the same, MEGA is different. Unlike the industry norm where the cloud storage provider holds the decryption key, with MEGA, you control the encryption. You hold the keys and you decide who you grant or deny access to your files. We call it User Controlled Encryption (UCE).

MEGA Ltd provides a Software Development Kit (SDK) for its cloud storage services. The MEGA SDK is developed and maintained in C++. In order to facilitate development of third-party applications using MEGA services, MEGA provides bindings to the core C++ functionality for several high-level languages, including Java.

This guide describes how to install the MEGA SDK Java bindings and explains the basic principles of use.

^^^^
SWIG
^^^^

The additional language bindings are automatically generated from C++ by the Simplified Wrapper and Interface Generator (SWIG) open source tool.

    `"SWIG is an interface compiler that connects programs written in C and C++ with scripting languages such as Perl, Python, Ruby, and Tcl. It works by taking the declarations found in C/C++ header files and using them to generate the wrapper code that scripting languages need to access the underlying C/C++ code."` 

For more information, please visit: http://www.swig.org/

Updates to the SDK are carried out by modifying the C++ code then re-generating the bindings using SWIG. This has the benefit of adding functionality to all language bindings from a single central source, without the need to add the change to each language's bindings manually.

^^^^^^^^^^^^
API Bindings
^^^^^^^^^^^^

In order to make the automatically generated SWIG bindings more usable for developers, language native Application Programming Interfaces (APIs) are developed. These APIs are written in the native language, not SWIG generated. They aim to follow native language conventions and attempt to handle C++ differences, such as garbage handling, in the background. This frees up the developer to concentrate on developing solutions, not learning how to interface with C++ in their preferred language.

^^^^^^^^^^^^
Asynchronous
^^^^^^^^^^^^

In order to speed up the process of interacting with MEGA services, common functionality is carried out on non-blocking, concurrent threads at the core C++ code level.


----------------------------------------
Installation
----------------------------------------

Before you are able to start implementing the various functionality of the MEGA bindings you will of course need to compile and install them.

:ref:`installsdk`

The Java bindings can be found in ``sdk/bindings/java/nz/mega/sdk/``.

-------------------
Concepts
-------------------

There are some features of the SDK which **must** be initiated in order to work with the functionality of the SDK.

^^^^^^^
AppKey
^^^^^^^

An appKey must be specified to start a session and use the MEGA SDK in your own code. Generate an appKey for free at https://mega.co.nz/#sdk

.. code:: java
    
    /*
     * An appKey is required to access MEGA services using the MEGA SDK.
     * You can generate an appKey for your app for free @ https://mega.co.nz/#sdk
     */
    private static final String APP_KEY = "pU5ShQxB";

^^^^^^^^
Sessions
^^^^^^^^

To access MEGA services using the MEGA SDK a session needs to be started by completing a successful log-in. To start this process, an object of the ``MegaApiJava`` class needs to be instantiated specifying an appKey and, preferably, a local path as a String to use for node cache. Local node caching enhances security as it prevents the account password from being stored by the application.

.. code:: java
 
    /*
     * The MegaApiJava object which provides access to the various 
     * MEGA storage functionality.
     */
    private MegaApiJava megaApiJava = null;
    
    ...
    
    // Base path to store cache locally
    String localPath = System.getProperty("user.dir");
    
    // Instantiate MegaApiJava object
    this.megaApiJava = new MegaApiJava(APP_KEY, localPath);

To complete the start of a session, the user needs to log-in_.

To take advantage of local node caching, the session key String can be identified while the user is logged in using ``MegaApiJava.dumpSession()``. This session key can then be used to log-in without having to create a new session using ``MegaApiJava.fastLogin()``.

To end the session, the user needs to log-out_.

Alternative Java APIs
""""""""""""""""""""""""""

Sub-classes of ``MegaApiJava`` provide implementations for graphics processing, primarily for the creation of thumbnails and previews of the user's MEGA Cloud Storage file hierarchy. These implementations pass callbacks to the graphical user interface (GUI) thread. 

Use ``MegaApiAndroid`` if developing for **Android**. Use ``MegaApiSwing`` if developing with **Swing**.

.. NOTE::
    ``MegaApiSwing`` does not have an implementation of the graphics processor yet. However, it does send callbacks to the GUI thread.



^^^^^^^^^^^^^^^^^
Nodes
^^^^^^^^^^^^^^^^^

The MEGA SDK represents files and folders as trees of Node objects. Nodes point to parent nodes, forming trees. Trees have exactly one root node. For this reason, to interact with files and folders on the MEGA Cloud Storage service, ``MegaNode`` objects are referenced. 

.. code:: java
    
    // Specify file node
    MegaNode node = megaApiJava.getNodeByPath("stringPathToNameOfFile", parentNode);
    

^^^^^^^^^
Listener
^^^^^^^^^

The ``MegaListenerInterface`` can be implemented so that request events between your application and MEGA server, or MEGA server and application, can trigger your code.

.. code:: java
    
    // Implement MEGA Listener
    public class ExampleClass implements MegaListenerInterface {
    ...
    }

The listener should then be added to the MegaApiJava object.

.. code:: java

    // Add the MEGACRUD listener object to listen for events when interacting
    // with MEGA Services
    this.megaApiJava.addRequestListener(this);
    
In this way you can, for example, check that a request was carried out successfully:

.. code:: java
    
    @Override
    public void onRequestFinish(MegaApiJava api, MegaRequest request, MegaError e) {

        // identify the MegaRequest type which has finished and triggered this event
        int requestType = request.getType();

        if (requestType == MegaRequest.TYPE_LOGOUT) {
            System.out.println("Log out completed; Result: " + 
                e.toString() + " ");
        }
    }

Request Types
"""""""""""""
Some useful request types include:
 * MegaRequest.TYPE_LOGIN
 * MegaRequest.TYPE_FETCH_NODES
 * MegaRequest.TYPE_ACCOUNT_DETAILS
 * MegaRequest.TYPE_UPLOAD
 * MegaRequest.TYPE_REMOVE
 * MegaRequest.TYPE_LOGOUT

---------------------------
Basic Functionality (CRUD)
---------------------------

The following steps will help you use the basic MEGA SDK functionality, including:
 * Login
 * **Create**
 * **Read**
 * **Update**
 * **Delete**
 * Log out


^^^^^^
Log-in
^^^^^^

The first step to access MEGA services is for the user to have have a valid account and log-in. To do this you can use the MEGA API log-in functionality. One of the ``MegaApiJava.login()`` options should be used to log into a MEGA account to successfully start a session. This will require retrieving the user's email address (MEGA user name) and password and passing this to the function.

.. code:: java

    // Log in.
    megaApiJava.login(userEmail, password);

If the log-in request succeeds, call ``MegaApiJava.fetchNodes()`` to get the account's file hierarchy from MEGA.

.. code:: java

    // The user has just logged in, so fetch the nodes of of the users account
    // object so that the MEGA API functionality can be used.
    megaApiJava.fetchNodes();

Once logged in with the file hierarchy retrieved, you will be able to carry out additional functionality. All other requests, including file management and transfers, can be used. Please see the inline JavaDoc in ``sdk/bindings/java/nz/mega/sdk/MegaApiJava`` for other ways of calling the ``login()`` function with different parameters. Let's start with "Create".

^^^^^^
Create
^^^^^^

Below is the function for the uploading a file, or creating a ``MegaNode``, on the MEGA cloud storage service.

.. code:: java

    // Instantiate a MegaNode as the logged in user's root directory.
    MegaNode parentDirectory = megaApiJava.getRootNode();

.. code:: java

    // Create (a.k.a Upload Node).
    megaApiJava.startUpload("localPath/README.md", parentDirectory, this);

This example shows the upload of a file called ``README.md`` to a parent directory on the MEGA Cloud Storage service. It simply calls the ``startUpload()`` method and passes the local path of the file as a String. The destination parent directory in the user's MEGA cloud storage file hierarchy is specified as a ``MegaNode`` object. A ``MegaListener`` listener_ object is specified to enable monitoring of the upload event. As our ``ExampleClass`` implements ``MegaListenerInterface``, the listener is specified as ``this``.

Please see the inline JavaDoc in ``sdk/bindings/java/nz/mega/sdk/MegaApiJava`` for other ways of calling the ``startUpload()`` function with different parameters. Next we look at "Read".

^^^^
Read
^^^^

Being able to retrieve uploaded files is an important feature which can be achieved using the methods below:

.. code:: java

    // Instantiate a MegaNode as the target file to download from the logged
    // in user's root directory.
    MegaNode fileToDownload = megaApiJava.getNodeByPath("README.md", parentDirectory);

.. code:: java

    // Read (a.k.a Download Node).
    megaApiJava.startDownload(fileToDownload, "README_returned.rst", this);

This example shows reading a file called ``README.md`` from a directory, specified as ``parentDirectory``, on the MEGA Cloud Storage service.

The desired file to be downloaded is represented by an instantiated node object which is passed to the ``startDownload()`` method. The local path of where to store the file is specified as a String. If this path is a local folder, it must end with a '\\' or '/' character. In this case, the file name in MEGA will be used to store a file inside that folder. If the path does not finish with one of these characters, the file will be downloaded with the specified name to the specified path. This is the case in our example where the returned file is downloaded to the application's root folder as ``README_returned.rst``.

A ``MegaListener`` listener_ object is specified to enable monitoring of the download event. Once again, as our ``ExampleClass`` implements ``MegaListenerInterface``, the listener is specified as ``this``.

Please see the inline JavaDoc in ``sdk/bindings/java/nz/mega/sdk/MegaApiJava`` for other ways of calling the ``startDownload()`` function with different parameters.


^^^^^^
Update
^^^^^^
A special case presents itself when replacing a file on the MEGA Cloud Storage with a file of the same name from your local directory. Below is an example of the readme.md file being uploaded for second time.

.. NOTE::
    Uploading a node with the same name does not overwrite the existing node. Instead, a second file with the same name is created.

.. code:: java

    // Instantiate a MegaNode as the target file to replace on the logged in
    // user's root directory.
    MegaNode oldNode = megaApiJava.getNodeByPath("README.md", parentDirectory);
    
.. code:: java
    
    // Update
    megaApiJava.startUpload("README.md", parentDirectory, this);

If there is an old node with the same name you may want to delete that node before updating with the new node. This is the topic of the next section.

^^^^^^
Delete
^^^^^^

To delete a file from the MEGA Cloud Storage service simply call the ``remove()`` method, specifying the node you wish to remove.

.. code:: java

    // Check if the file is already present on MEGA.
    if (oldNode != null) {
        // Remove the old node with the same name.
        megaApiJava.remove(oldNode);
    }

To tidy up, any unwanted files created by the application can be removed using the the ``remove()`` method as above. All that remains is to close the session.

^^^^^^^
Log-out
^^^^^^^

.. @TODO How to tidy up (if necessary) when ending the application's MEGA session.

Call ``logout()`` to close the MEGA session.

.. code:: java
    
    megaApiJava.logout();

Ensure the ``logout()`` request has completed to guarantee that the session has been invalidated. This can be confirmed by waiting for a ``MegaRequest.TYPE_LOGOUT`` to trigger the ``onRequestFinish()`` listener method as demonstrated in Listener_.

After using ``MegaApiJava.logout()`` you can reuse the same ``MegaApiJava`` object to log in to another MEGA account.

``locallogout()`` can be used to log out without invalidating the current session. In this way the session can be resumed using log-in_.


---------------------------
Fin
---------------------------

And that's it. You are now ready to develop in Java for the MEGA Cloud Storage service.

For more specific information you can check out the inline JavaDoc in the Java binding classes, particularly ``sdk/bindings/java/nz/mega/sdk/MegaApiJava``. For a detailed, C++ specific explanation, please visit: https://mega.nz/#doc
