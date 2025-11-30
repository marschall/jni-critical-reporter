JNI Ciritical Reporter
======================

Reports usage of [JNI criticals](https://shipilev.net/jvm/anatomy-quarks/9-jni-critical-gclocker/) in Java programs using JFR events.

Implementation
----------------


```sh
java -agentpath:
```

Implemented as a native JVMTI agent 

Redirected the JNI function table.

We cache handles to keep JNI lookups to a minimum.

Use Cases
-----------

1. You want track down JNI critical usage in your application, for example because you have `GCLocker Initiated GC`
1. You want to asses the effectiveness of JNI critical usage in your application. You want to know in how many cases a copy was done vs when it was avoided.

Limitations
-----------

Array and String length are currently not reported.

