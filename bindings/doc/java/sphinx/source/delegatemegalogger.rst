==================
DelegateMegaLogger
==================

class DelegateMegaLogger
extends MegaLogger

The Class DelegateMegaLogger.

-------------

-------------
Field Summary
-------------
+-------------------------------------+--------------------------+
| Modifier and Type	              |  Field and Description   |
+=====================================+==========================+
|(package private) MegaLoggerInterface|	 listener                |
|                                     |  The listener.           |
+-------------------------------------+--------------------------+

------------

-------------------
Constructor Summary
-------------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~
Constructor and Description
~~~~~~~~~~~~~~~~~~~~~~~~~~~
DelegateMegaLogger(MegaLoggerInterface listener)
Instantiates a new delegate mega logger.

-------------------

--------------
Method Summary
--------------
+--------------------+----------------------------------------------------------------------------------------------+
| Modifier and Type  |	Method and Description                                                                      |
+====================+==============================================================================================+
| void	             |   log(java.lang.String time, int loglevel, java.lang.String source, java.lang.String message)|
|                    |    Log.                                                                                      |
+--------------------+----------------------------------------------------------------------------------------------+

----------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

---------------

------------
Field Detail
------------

~~~~~~~~
listener
~~~~~~~~

MegaLoggerInterface listener
The listener.

-------------------

------------------
Constructor Detail
------------------

~~~~~~~~~~~~~~~~~~
DelegateMegaLogger
~~~~~~~~~~~~~~~~~~
DelegateMegaLogger(MegaLoggerInterface listener)
Instantiates a new delegate mega logger.
Parameters:
listener - the listener

-------------------

-------------
Method Detail
-------------

~~~
log
~~~
public void log(java.lang.String time,int loglevel,java.lang.String source,java.lang.String message)
Log.
Parameters:
time - the time
loglevel - the loglevel
source - the source
message - the message
