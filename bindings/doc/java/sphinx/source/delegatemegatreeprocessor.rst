=========================
DelegateMegaTreeProcessor
=========================

class DelegateMegaTreeProcessor
extends MegaTreeProcessor

The Class DelegateMegaTreeProcessor.

----------------------------

-------------
Field Summary
-------------

+--------------------------------------------+-------------------------------------+
|Modifier and Type	                     |   Field and Description             |
+============================================+=====================================+
|(package private) MegaTreeProcessorInterface|	listener                           |
|                                            |  The listener.                      |
+--------------------------------------------+-------------------------------------+
|(package private) MegaApiJava	             |   megaApi                           |
|                                            |   The mega api.                     |
+--------------------------------------------+-------------------------------------+

----------------------------------

-------------------
Constructor Summary
-------------------

DelegateMegaTreeProcessor(MegaApiJava megaApi, MegaTreeProcessorInterface listener)
Instantiates a new delegate mega tree processor.

------------------------------------

--------------
Method Summary
--------------

+--------------------------------------------+----------------------------------------+
|Modifier and Type	                     |  Method and Description                |
+============================================+========================================+
|boolean	                             |          processMegaNode(MegaNode node)|
|                                            |   Process mega node.                   |
+--------------------------------------------+----------------------------------------+


-----------------------------------

---------------------------------------------
Methods inherited from class java.lang.Object
---------------------------------------------
clone, equals, finalize, getClass, hashCode, notify, notifyAll, toString, wait, wait, wait

------------------------------------

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
MegaTreeProcessorInterface listener
The listener.

--------------------------------------

------------------
Constructor Detail
------------------

DelegateMegaTreeProcessor
DelegateMegaTreeProcessor(MegaApiJava megaApi,MegaTreeProcessorInterface listener)
Instantiates a new delegate mega tree processor.
Parameters:
megaApi - the mega api
listener - the listener

---------------------------------------

-------------
Method Detail
-------------

processMegaNode
public boolean processMegaNode(MegaNode node)
Process mega node.
Parameters:
node - the node
Returns:
true, if successful
