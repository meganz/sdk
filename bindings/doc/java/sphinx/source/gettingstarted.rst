===============
Getting Started
===============

These steps will help you use the basic Mega functionality, such as:
* Login
* Log out
* Create
* Read
* Upload
* Delete

------------
First Steps!
------------

~~~~~
Login
~~~~~

First to access the Mega services the user must have a valid account and login.
To do this you can use the Mega API login functionality.

.. code:: java
     
     /**
     * Logs in to the user's mega account using the credentials previously accessed.
     *
     */
    public void login() {
        System.out.println("");
        System.out.println("*** start: login ***");
        // Login.
        synchronized(megaApiJava) {
            // Log in
            megaApiJava.login(userEmail, password, this);
            // Wait for the login process to complete.
            // The login request is not finished, if the onRequestFinished()
            // method of the implemented Listener interface has not been called.
            if (!megaApiRequestFinished) {
                try {
                    megaApiJava.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
        if (megaApiJava.isLoggedIn() == 0) {
            System.out.println("Not logged in. Exiting.");
            return; // Halt if not logged in
        }
        else if (megaApiJava.isLoggedIn() > 0) {
            System.out.println("Logged in");
        }

        // Reset the api request finished flag
        megaApiRequestFinished = false;
        System.out.println("*** done: login ***");
    }
	

Of course this will require retrieving the user's user name and password and passing this to the function.
Once logged in you will be able to do a number of basic features. Let's start with "Create".

~~~~~~
Create
~~~~~~

Below you will see the function for the creation of a node. This is the function used to create a File or "Node" in the Mega system.

.. code:: java

     /**
     * Uploads a file to the user's mega account.
     */
    public void create() {
        // Upload a file (create).
        System.out.println("");
        System.out.println("*** start: upload ***");
        megaApiJava.startUpload("README.md", currentWorkingDirectory, this);
        synchronized (megaApiJava) {
            if(!megaApiRequestFinished) {
                try {
                    megaApiJava.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
        megaApiRequestFinished = false;
        System.out.println("*** done: upload ***");
    }


This example shows the upload of a file called README.md to the current directory the user is working in. Simply calls the startUpload() method and passes the file name string and the directory.

next we have "Read"

~~~~
Read
~~~~

Being able to retrieve and read the files which you have uploaded is a very handy feature, This is provided by the below piece of code.

.. code:: java

     /**
     * Downloads a file from the user's mega account.
     */
    public void read() {
        System.out.println("");
        System.out.println("*** start: download ***");
        MegaNode fileToDownload = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);
        if (fileToDownload != null) {
            megaApiJava.startDownload(fileToDownload, "README_returned.md", this);
            synchronized (megaApiJava) {
                if(!megaApiRequestFinished) {
                    try {
                        megaApiJava.wait();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
        }
        else {
            System.out.println("Node not found: README.md");
        }
        megaApiRequestFinished = false;
        System.out.println("*** done: download ***");
    }


You may want to upload an existing file from you local directory to the Mega Cloud, To do this you will need to "Upload" it.

~~~~~~
Upload
~~~~~~
Below you will see an example of a readme.md file being uploaded.

.. code:: java

     /**
     * Uploads a file to the user's mega account.
     * <p>
     * Note: A new upload  with the same name won't overwrite,
     * but create a new node with same name!
     */
    public void update() {
        // Change a file (update).

        System.out.println("");
        System.out.println("*** start: update ***");
        MegaNode oldNode = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);
        megaApiJava.startUpload("README.md", currentWorkingDirectory, this);
        synchronized (megaApiJava) {
            if(!megaApiRequestFinished) {
                try {
                    megaApiJava.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
        megaApiRequestFinished = false;
        if (oldNode != null) {
            // Remove the old node with the same name.
            fileName = oldNode.getName();
            megaApiJava.remove(oldNode, this);
            synchronized (megaApiJava) {
                if(!megaApiRequestFinished) {
                    try {
                        megaApiJava.wait();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
            System.out.println("Pre-existing file " + oldNode.getName() + " in /" +
                    currentWorkingDirectory.getName() + " removed.");
        } else {
            System.out.println("No existing file conflict, no old node needs removing");
        }
        megaApiRequestFinished = false;
        System.out.println("*** done: update ***");
    }


Oops looks like you uploaded a file you didn't want to upload or you want to "Delete" a file. That's OK because that's our next section.

~~~~~~
Delete
~~~~~~

To remove a file from the Mega Cloud first you must...
Then...
And its gone.

.. code:: java

     /**
     * Deletes a file from the user's mega account.
     */
    public void delete() {
        // Delete a file (delete).
        System.out.println("");
        System.out.println("*** start: delete ***");
        //System.out.println("CWD: " + currentWorkingDirectory.getName());
        MegaNode node = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);
        //System.out.println("node to delete " + node.getName());
        if (node != null) {
            megaApiJava.remove(node, this);
            // Make note of file being removed so it can be reported from the listener method
            fileName = node.getName();
            synchronized (megaApiJava) {
                if(!megaApiRequestFinished) {
                    try {
                        megaApiJava.wait();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
            megaApiRequestFinished = false;
        }
        else {
            System.out.println("Node not found: " + node.getName());
        }

        node = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);
        System.out.println("Is README node null and therefore not existing? " + node );
        System.out.println("*** done: delete ***");
    }


And that's it your now ready to start storing your info onto the Mega Cloud.
For more detailed information we have a brief how to on each of the functions, or if you want the specifics only you can check out the JavaDoc.
