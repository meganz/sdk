======================================
Interface MegaRequestListenerInterface
======================================

All Known Subinterfaces:
MegaListenerInterface

public interface MegaRequestListenerInterface
The Interface MegaRequestListenerInterface.

---------------------------------------------------

--------------
Method Summary
--------------

+--------------------+-------------------------------------------------------------------------------+
|Modifier and Type   |  Method and Description                                                       |
+====================+===============================================================================+
|void	             |   onRequestFinish(MegaApiJava api, MegaRequest request, MegaError e)          |
|                    |   On request finish.                                                          |
+--------------------+-------------------------------------------------------------------------------+
|void	             |   onRequestStart(MegaApiJava api, MegaRequest request)                        |
|                    |   On request start.                                                           |
+--------------------+-------------------------------------------------------------------------------+
|void	             |   onRequestTemporaryError(MegaApiJava api, MegaRequest request, MegaError e)  |
|                    |   On request temporary error.                                                 |
+--------------------+-------------------------------------------------------------------------------+
|void	             |   onRequestUpdate(MegaApiJava api, MegaRequest request)                       |
|                    |   On request update.                                                          |
+--------------------+-------------------------------------------------------------------------------+

--------------------------------------------------

-------------
Method Detail
-------------

~~~~~~~~~~~~~~
onRequestStart
~~~~~~~~~~~~~~
void onRequestStart(MegaApiJava api,MegaRequest request)
On request start.
Parameters:
api - the api
request - the request

~~~~~~~~~~~~~~~
onRequestUpdate
~~~~~~~~~~~~~~~
void onRequestUpdate(MegaApiJava api,MegaRequest request)
On request update.
Parameters:
api - the api
request - the request

~~~~~~~~~~~~~~~
onRequestFinish
~~~~~~~~~~~~~~~
void onRequestFinish(MegaApiJava api,MegaRequest request,MegaError e)
On request finish.
Parameters:
api - the api
request - the request
e - the e

~~~~~~~~~~~~~~~~~~~~~~~
onRequestTemporaryError
~~~~~~~~~~~~~~~~~~~~~~~
void onRequestTemporaryError(MegaApiJava api,MegaRequest request,MegaError e)
On request temporary error.
Parameters:
api - the api
request - the request
e - the e
