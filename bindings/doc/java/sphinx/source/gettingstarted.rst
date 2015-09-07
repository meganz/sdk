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

            // Log in
            megaApiJava.login(userEmail, password);
            
	

Of course this will require retrieving the user's user name and password and passing this to the function.
There are other ways of calling this function please check these in the JavaDocs.

Once logged in you will be able to do a number of basic features. Let's start with "Create".

~~~~~~
Create
~~~~~~

Below you will see the function for the creation of a node. This is the function used to create a File or "Node" in the Mega system.

.. code:: java

	//Create/Upload
        megaApiJava.startUpload("README.md", currentWorkingDirectory);

This example shows the upload of a file called README.md to the current directory the user is working in.    
Simply calls the startUpload() method and passes the file name string and the directory.
There are other ways of calling this function please check these in the JavaDocs.

next we have "Read"

~~~~
Read
~~~~

Being able to retrieve and read the files which you have uploaded is a very handy feature, This is provided by the below piece of code.

.. code:: java

        MegaNode fileToDownload = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);

Here we have saved the file as a MegaNode this is bescause the getNodeByPath returns a MegaNode. Again the name of the file and the directory are required.
There are other ways of calling this function please check these in the JavaDocs.

You may want to upload an existing file from you local directory to the Mega Cloud, To do this you will need to "Upload" it.

~~~~~~
Upload
~~~~~~
Below you will see an example of a readme.md file being uploaded.

.. code:: java

        MegaNode oldNode = megaApiJava.getNodeByPath("README.md", currentWorkingDirectory);
        megaApiJava.startUpload("README.md", currentWorkingDirectory, this);

        if (oldNode != null) {
            // Remove the old node with the same name.
            fileName = oldNode.getName();
            megaApiJava.remove(oldNode);

If there is an old node with the same name you may want to delete that node before uploading the new node.

Oops looks like you uploaded a file you didn't want to upload or you want to "Delete" a file. That's OK because that's our next section.

~~~~~~
Delete
~~~~~~

To remove a file from the Mega Cloud simply call the below method with the node you wish to remove.

.. code:: java

            megaApiJava.remove(node, this);



And that's it your now ready to start storing your info onto the Mega Cloud.
For more detailed information we have a brief how to on each of the functions, or if you want the specifics only you can check out the JavaDoc.
