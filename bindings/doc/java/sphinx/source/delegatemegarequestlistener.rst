===========================
DelegateMegaRequestListener
===========================

class DelegateMegaRequestListener
extends MegaRequestListener  

The listener interface for receiving delegateMegaRequest events. The class that is interested in processing a delegateMegaRequest event implements this interface, and the object created with that class is registered with a component using the component's addDelegateMegaRequestListener method. When the delegateMegaRequest event occurs, that object's appropriate method is invoked.

------------------------------------

-------------
Field Summary
-------------

+----------------------------------------------+-------------------------------------+
|  Modifier and Type	                       |  Field and Description              |
+==============================================+=====================================+
|(package private) MegaRequestListenerInterface|   listener                          |
|                                              |   The listener.                     |
+----------------------------------------------+-------------------------------------+
|(package private) MegaApiJava	               |   megaApi                           |
|                                              |   The mega api.                     |
+----------------------------------------------+-------------------------------------+
|(package private) boolean	               |   singleListener                    |
|                                              |   The single listener.              |
+----------------------------------------------+-------------------------------------+

-------------------------------------

-------------------
Constructor Summary
-------------------

DelegateMegaRequestListener(MegaApiJava megaApi, MegaRequestListenerInterface listener, boolean singleListener)
Instantiates a new delegate mega request listener.

-------------------------------------

--------------
Method Summary
--------------

+----------------------------------------------+------------------------------------------------------------------------+
|  Modifier and Type	                       |  Method and Description                                                |
+==============================================+========================================================================+
|(package private) MegaRequestListenerInterface|  getUserListener()                                                     |
|                                              |  Gets the user listener.                                               |
+----------------------------------------------+------------------------------------------------------------------------+
|void	                                       |  onRequestFinish(MegaApi api, MegaRequest request, MegaError e)        |
|                                              |  On request finish.                                                    |
+----------------------------------------------+------------------------------------------------------------------------+
|void	                                       | onRequestStart(MegaApi api, MegaRequest request)                       |
|                                              | On request start.                                                      |
+----------------------------------------------+------------------------------------------------------------------------+
|void	                                       | onRequestTemporaryError(MegaApi api, MegaRequest request, MegaError e) |
|                                              | On request temporary error.                                            |
+----------------------------------------------+------------------------------------------------------------------------+
|void	                                       | onRequestUpdate(MegaApi api, MegaRequest request)                      |
|                                              | On request update.                                                     |
+----------------------------------------------+------------------------------------------------------------------------+

------------------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

------------------------------

------------
Field Detail
------------

~~~~~~~
megaApi
~~~~~~~
MegaApiJava megaApi
The mega api.

~~~~~~~~
listener
~~~~~~~~
MegaRequestListenerInterface listener
The listener.

~~~~~~~~~~~~~~
singleListener
~~~~~~~~~~~~~~
boolean singleListener
The single listener.

-------------------------------------

------------------
Constructor Detail
------------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~
DelegateMegaRequestListener
~~~~~~~~~~~~~~~~~~~~~~~~~~~
DelegateMegaRequestListener(MegaApiJava megaApi,MegaRequestListenerInterface listener,boolean singleListener)
Instantiates a new delegate mega request listener.
Parameters:
megaApi - the mega api
listener - the listener
singleListener - the single listener

--------------------------------------------------

-------------
Method Detail
-------------

~~~~~~~~~~~~~~~
getUserListener
~~~~~~~~~~~~~~~
MegaRequestListenerInterface getUserListener()
Gets the user listener.
Returns:
the user listener

~~~~~~~~~~~~~~
onRequestStart
~~~~~~~~~~~~~~
public void onRequestStart(MegaApi api,MegaRequest request)
On request start.
Parameters:
api - the api
request - the request

~~~~~~~~~~~~~~~
onRequestUpdate
~~~~~~~~~~~~~~~
public void onRequestUpdate(MegaApi api,MegaRequest request)
On request update.
Parameters:
api - the api
request - the request

~~~~~~~~~~~~~~~
onRequestFinish
~~~~~~~~~~~~~~~
public void onRequestFinish(MegaApi api,MegaRequest request,MegaError e)
On request finish.
Parameters:
api - the api
request - the request
e - the e

~~~~~~~~~~~~~~~~~~~~~~~
onRequestTemporaryError
~~~~~~~~~~~~~~~~~~~~~~~
public void onRequestTemporaryError(MegaApi api,MegaRequest request,MegaError e)
On request temporary error.
Parameters:
api - the api
request - the request
e - the e
