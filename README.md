JNI Ciritical Reporter
======================

Reports usage of [JNI criticals](https://shipilev.net/jvm/anatomy-quarks/9-jni-critical-gclocker/) in Java programs using JFR events.

Implementation
----------------


The project ist implemented as a native JVMTI agent. We [redirect](https://docs.oracle.com/en/java/javase/25/docs/specs/jvmti.html#SetJNIFunctionTable) `GetStringCritical` and `GetPrimitiveArrayCritical` in the JNI function table. We cache handles to keep JNI lookups to a minimum.

Features
---------

The project currently reports the following information

- stack trace, default JFR mechanism
- star time, duration, end time, default JFR mechanism
- whether the underlying data was copied
- mehtod used, `GetStringCritical` or `GetPrimitiveArrayCritical`

Limitations
-----------

Array and String length are currently not reported. For nested critical sections only the other one is reported to comply with JNI guidelines.

Usage
-----

As the agent is a native binary you have to compile it from source

```
mvn verify
```

and the use it

```sh
java -agentpath:
```

Use Cases
-----------

This project may be of interest to you in the following cases:

1. You want track down JNI critical usage in your application, for example because you have `GCLocker Initiated GC`.
1. You want to asses the effectiveness of JNI critical usage in your application. You want to know in how many cases a copy was done vs when it was avoided.


