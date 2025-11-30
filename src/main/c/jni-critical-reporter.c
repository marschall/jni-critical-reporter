#include <jni.h>
#include <jvmti.h>
#include <string.h>
#include <stdio.h>


// global cached JNI data to reduce lookup time
struct JfrInfo {
  // our instance of jdk.jfr.EventFactory
  // JNI global reference
  jobject eventFactory;
  // jdk.jfr.EventFactory#newEvent()
  jmethodID newEventMethod;
  // jdk.jfr.Event#set(int, java.lang.Object)
  jmethodID setMethod;
  // jdk.jfr.Event#begin()
  jmethodID beginMethod;
  // jdk.jfr.Event#commit()
  jmethodID commitMethod;
  // Boolean.TRUE
  jobject trueObject;
  // Boolean.FALSE
  jobject falseObject;
  // "GetStringCritical"
  jstring getStringCritical;
  // "GetPrimitiveArrayCritical"
  jstring getPrimitiveArrayCritical;
};

// thread local info about the current JNI critical
// need for committing the event
struct CallInfo {
  // event started before Get*Critical
  // JNI global reference
  jobject event;
  jboolean *isCopy;
  jboolean witness;
};

jniNativeInterface *originalJNIFunctions = NULL;
jniNativeInterface *redirectedJNIFunctions = NULL;
struct JfrInfo jfrInfo;

//thread_local
// __declspec(thread)
__thread int criticals = 0;
__thread struct CallInfo callInfo;


jint newAnnotationElement(JNIEnv *env, const char *annotationTypeClassName, const char *value, jobject *result) {
  // new AnnotationElement(annotationTypeClassName.class, value)
  jclass annotationTypeClass = (*env)->FindClass(env, annotationTypeClassName);
  if (annotationTypeClass == NULL) {
    fprintf(stderr, "FindClass(%s) failed\n", annotationTypeClassName);
    return JNI_ERR;
  }

  jstring valueString = (*env)->NewStringUTF(env, value);
  if (valueString == NULL) {
    fprintf(stderr, "NewStringUTF(%s) failed\n", value);
    return JNI_ERR;
  }

  jclass annotationElementClass = (*env)->FindClass(env, "jdk/jfr/AnnotationElement");
  if (annotationElementClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/AnnotationElement) failed\n");
    return JNI_ERR;
  }
  jmethodID annotationElementConstructor = (*env)->GetMethodID(env, annotationElementClass,
                                                                    "<init>", "(Ljava/lang/Class;Ljava/lang/Object;)V");
  if (annotationElementConstructor == NULL) {
    fprintf(stderr, "GetMethodID(jdk/jfr/AnnotationElement#<init>) failed\n");
    return JNI_ERR;
  }

  jobject annotationElement = (*env)->NewObject(env, annotationElementClass, annotationElementConstructor,
                                                     annotationTypeClass, valueString);
  if (annotationElement == NULL) {
    fprintf(stderr, "new %s() failed\n", annotationTypeClassName);
    return JNI_ERR;
  }
  *result = annotationElement;
  // annotationElementConstructor jmethodID does not need to be freed
  (*env)->DeleteLocalRef(env, annotationElementClass);
  (*env)->DeleteLocalRef(env, annotationTypeClass);
  (*env)->DeleteLocalRef(env, valueString);
  return JNI_OK;
}

jint getEventAnnotations(JNIEnv *env, jobject *result) {

  // String[] category = { "JNI" };
  jstring categoryString = (*env)->NewStringUTF(env, "JNI");
  if (categoryString == NULL) {
    fprintf(stderr, "NewStringUTF(JNI) failed\n");
    return JNI_ERR;
  }
  jclass stringClass = (*env)->FindClass(env, "java/lang/String");
  if (stringClass == NULL) {
    fprintf(stderr, "FindClass(java/lang/String) failed\n");
    return JNI_ERR;
  }
  jobjectArray categoryArray = (*env)->NewObjectArray(env, 1, stringClass, categoryString);
  if (categoryArray == NULL) {
    fprintf(stderr, "NewObjectArray(1, java/lang/String) failed\n");
    return JNI_ERR;
  }
  (*env)->DeleteLocalRef(env, categoryString);
  (*env)->DeleteLocalRef(env, stringClass);

  jobject nameElement;
  jint name_result = newAnnotationElement(env, "jdk/jfr/Name", "com.github.marschall.jnicriticalreporter.Event", &nameElement);
  if (name_result != JNI_OK) {
    fprintf(stderr, "new AnnotationElement(Name.class failed\n");
    return JNI_ERR;
  }

  jobject labelElement;
  jint label_result = newAnnotationElement(env, "jdk/jfr/Label", "JNI Critical", &labelElement);
  if (label_result != JNI_OK) {
    fprintf(stderr, "new AnnotationElement(Label.class failed\n");
    return JNI_ERR;
  }

  jobject descriptionElement;
  jint description_result = newAnnotationElement(env, "jdk/jfr/Description", "Lists invocation of JNI critical methods", &descriptionElement);
  if (description_result != JNI_OK) {
    fprintf(stderr, "new AnnotationElement(Description.class failed\n");
    return JNI_ERR;
  }

  // new AnnotationElement
  jclass categoryClass = (*env)->FindClass(env, "jdk/jfr/Category");
  if (categoryClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/Category) failed\n");
    return JNI_ERR;
  }
  jclass annotationElementClass = (*env)->FindClass(env, "jdk/jfr/AnnotationElement");
  if (annotationElementClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/AnnotationElement) failed\n");
    return JNI_ERR;
  }
  jmethodID annotationElementConstructor = (*env)->GetMethodID(env, annotationElementClass,
                                                               "<init>", "(Ljava/lang/Class;Ljava/lang/Object;)V");
  if (annotationElementConstructor == NULL) {
    fprintf(stderr, "GetMethodID(jdk/jfr/AnnotationElement#<init>) failed\n");
    return JNI_ERR;
  }
  jobject categoryElement = (*env)->NewObject(env, annotationElementClass, annotationElementConstructor,
                                                   categoryClass, categoryArray);
  if (categoryElement == NULL) {
    fprintf(stderr, "NewObject failed\n");
    return JNI_ERR;
  }

  (*env)->DeleteLocalRef(env, categoryClass);
  (*env)->DeleteLocalRef(env, categoryArray);
  (*env)->DeleteLocalRef(env, annotationElementClass);

  // List.of

  jclass listClass = (*env)->FindClass(env, "java/util/List");
  if (listClass == NULL) {
    fprintf(stderr, "FindClass(java/util/List) failed\n");
    return JNI_ERR;
  }
  jmethodID listOfMethod = (*env)->GetStaticMethodID(env, listClass,
                                                     "of", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Ljava/util/List;");
  if (listOfMethod == NULL) {
    fprintf(stderr, "GetMethodID(List#of(Object, Object, Object, Object)) failed\n");
    return JNI_ERR;
  }
  jobject listOfAnnoationElement = (*env)->CallStaticObjectMethod(env, listClass, listOfMethod,
                                                                       nameElement, labelElement, descriptionElement, categoryElement);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    fprintf(stderr, "List.of() threw\n");
    return JNI_ERR;
  }
  *result = listOfAnnoationElement;
  (*env)->DeleteLocalRef(env, nameElement);
  (*env)->DeleteLocalRef(env, labelElement);
  (*env)->DeleteLocalRef(env, descriptionElement);
  (*env)->DeleteLocalRef(env, categoryElement);
  (*env)->DeleteLocalRef(env, listClass);
  return JNI_OK;
}

jint newListOf2(JNIEnv *env, jobject first, jobject second, jobject *result) {
   // return List.of(first, second)

  jclass listClass = (*env)->FindClass(env, "java/util/List");
  if (listClass == NULL) {
    fprintf(stderr, "FindClass(java/util/List) failed\n");
    return JNI_ERR;
  }
  jmethodID listOfMethod = (*env)->GetStaticMethodID(env, listClass,
                                                     "of", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/util/List;");
  if (listOfMethod == NULL) {
    fprintf(stderr, "GetMethodID(List#of(Object, Object)) failed\n");
    return JNI_ERR;
  }
  jobject list = (*env)->CallStaticObjectMethod(env, listClass, listOfMethod,
                                                                   first, second);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    fprintf(stderr, "List.of() threw\n");
    return JNI_ERR;
  }
  // listOfMethod jmethodID does not need to be freed
  (*env)->DeleteLocalRef(env, listClass);

  *result = list;
  return JNI_OK;
}

jint newAnnotationElements(JNIEnv *env, const char *label, const char *description, jobject *result) {
   // return List.of(
  // new AnnotationElement(Label.class, label)
  // new AnnotationElement(Description.class, description)

  jobject labelElement;
  jint label_result = newAnnotationElement(env, "jdk/jfr/Label", label, &labelElement);
  if (label_result != JNI_OK) {
    fprintf(stderr, "new AnnotationElement(Label.class failed\n");
    return JNI_ERR;
  }
  jobject descriptionElement;
  jint description_result = newAnnotationElement(env, "jdk/jfr/Description", description, &descriptionElement);
  if (description_result != JNI_OK) {
    fprintf(stderr, "new AnnotationElement(Description.class failed\n");
    return JNI_ERR;
  }

  // List.of(labelElement, descriptionElement)

  jint return_value = newListOf2(env, labelElement, descriptionElement, result);
  (*env)->DeleteLocalRef(env, labelElement);
  (*env)->DeleteLocalRef(env, descriptionElement);
  return return_value;
}

jint getBooleanField(JNIEnv *env, const char *fieldName, jobject *result) {

  jclass clazz = (*env)->FindClass(env, "java/lang/Boolean");
  if (clazz == NULL) {
    fprintf(stderr, "FindClass(java/lang/Boolean) failed\n");
    return JNI_ERR;
  }
  jfieldID typeField = (*env)->GetStaticFieldID(env, clazz, fieldName, "Ljava/lang/Boolean;");
  if (typeField == NULL) {
    fprintf(stderr, "GetStaticFieldID(%s#TYPE) failed\n", fieldName);
    return JNI_ERR;
  }
  jobject fieldValue = (*env)->GetStaticObjectField(env, clazz, typeField);
  (*env)->DeleteLocalRef(env, clazz);
  *result = fieldValue;
  return JNI_OK;
}

jint getTypeField(JNIEnv *env, const char *className, jclass *result) {

  jclass clazz = (*env)->FindClass(env, className);
  if (clazz == NULL) {
    fprintf(stderr, "FindClass(%s) failed\n", className);
    return JNI_ERR;
  }
  jfieldID typeField = (*env)->GetStaticFieldID(env, clazz, "TYPE", "Ljava/lang/Class;");
  if (typeField == NULL) {
    fprintf(stderr, "GetStaticFieldID(%s#TYPE) failed\n", className);
    return JNI_ERR;
  }
  jobject fieldValue = (*env)->GetStaticObjectField(env, clazz, typeField);
  (*env)->DeleteLocalRef(env, clazz);
  *result = fieldValue;
  return JNI_OK;
}

jint resolveType(JNIEnv *env, const char *className, jclass *result) {
  if (strcmp(className, "Z") == 0) {
    return getTypeField(env, "java/lang/Boolean", result);
  } else if (strcmp(className, "B") == 0) {
    return getTypeField(env, "java/lang/Byte", result);
  } else if (strcmp(className, "C") == 0) {
    return getTypeField(env, "java/lang/Character", result);
  } else if (strcmp(className, "S") == 0) {
    return getTypeField(env, "java/lang/Short", result);
  } else if (strcmp(className, "I") == 0) {
    return getTypeField(env, "java/lang/Integer", result);
  } else if (strcmp(className, "J") == 0) {
    return getTypeField(env, "java/lang/Long", result);
  } else if (strcmp(className, "F") == 0) {
    return getTypeField(env, "java/lang/Float", result);
  } else if (strcmp(className, "D") == 0) {
    return getTypeField(env, "java/lang/Double", result);
  } else {
    jclass valueClass = (*env)->FindClass(env, className);
    if (valueClass == NULL) {
      fprintf(stderr, "FindClass(%s) failed\n", className);
      return JNI_ERR;
    }
    *result = valueClass;
  }
  return JNI_OK;
}

jint newValueDescriptor(JNIEnv *env, const char *fieldType, const char *fieldName, jobject annotations, jobject *result) {

  jclass valueClass;
  jint valueClass_result = resolveType(env, fieldType, &valueClass);
  if (valueClass_result != JNI_OK) {
    fprintf(stderr, "FindClass(%s) failed\n", fieldType);
    return JNI_ERR;
  }

  jstring fieldString = (*env)->NewStringUTF(env, fieldName);
  if (fieldString == NULL) {
    fprintf(stderr, "NewStringUTF(%s) failed\n", fieldName);
    return JNI_ERR;
  }

  jclass valueDescriptionClass = (*env)->FindClass(env, "jdk/jfr/ValueDescriptor");
  if (valueDescriptionClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/ValueDescriptor) failed\n");
    return JNI_ERR;
  }
  jmethodID valueDescriptorConstructor = (*env)->GetMethodID(env, valueDescriptionClass,
                                                                  "<init>", "(Ljava/lang/Class;Ljava/lang/String;Ljava/util/List;)V");
  if (valueDescriptorConstructor == NULL) {
    fprintf(stderr, "GetMethodID(jdk/jfr/AnnotationElement#<init>) failed\n");
    return JNI_ERR;
  }

  jobject valueDescriptor = (*env)->NewObject(env, valueDescriptionClass, valueDescriptorConstructor,
                                                   valueClass, fieldString, annotations);
  if (valueDescriptor == NULL) {
    fprintf(stderr, "new ValueDescriptor(%s, %s failed\n", fieldType, fieldName);
    return JNI_ERR;
  }
  *result = valueDescriptor;
  (*env)->DeleteLocalRef(env, valueClass);
  (*env)->DeleteLocalRef(env, fieldString);
  (*env)->DeleteLocalRef(env, valueDescriptionClass);
  // valueDescriptorConstructor jmethodID does not need to be freed
  return JNI_OK;
}

jint getValueDescriptors(JNIEnv *env, jobject *result) {

  jobject isCopyAnnotations;
  jint isCopy_result = newAnnotationElements(env, "IsCopy", "Whether the memory was copied", &isCopyAnnotations);
  if (isCopy_result != JNI_OK) {
    fprintf(stderr, "List<AnnotationElement> IsCopy  failed\n");
    return JNI_ERR;
  }

  jobject methodNameAnnotations;
  jint methodName_result = newAnnotationElements(env, "IsCopy", "Whether the memory was copied", &methodNameAnnotations);
  if (methodName_result != JNI_OK) {
    fprintf(stderr, "List<AnnotationElement> MethodName  failed\n");
    return JNI_ERR;
  }

  jobject isCopyDescriptor;
  jint isCopyDescriptor_result = newValueDescriptor(env, "Z", "isCopy", isCopyAnnotations, &isCopyDescriptor);
  if (isCopyDescriptor_result != JNI_OK) {
    fprintf(stderr, "new ValueDescriptor(boolean.class, isCopy  failed\n");
    return JNI_ERR;
  }

  jobject methodNameDescriptor;
  jint methodNameDescriptor_result = newValueDescriptor(env, "java/lang/String", "methodName", methodNameAnnotations, &methodNameDescriptor);
  if (methodNameDescriptor_result != JNI_OK) {
    fprintf(stderr, "new ValueDescriptor(String.class, methodName  failed\n");
    return JNI_ERR;
  }

  // List.of(isCopyDescriptor, methodNameDescriptor)

  jint return_value = newListOf2(env, isCopyDescriptor, methodNameDescriptor, result);
  (*env)->DeleteLocalRef(env, isCopyAnnotations);
  (*env)->DeleteLocalRef(env, methodNameAnnotations);
  (*env)->DeleteLocalRef(env, isCopyDescriptor);
  (*env)->DeleteLocalRef(env, methodNameDescriptor);
  return return_value;
}

jint lookupEventFactoryMethods(JNIEnv *env, jclass eventFactoryClass) {

  jclass eventClass = (*env)->FindClass(env, "jdk/jfr/Event");
  if (eventClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/Event) failed\n");
    return JNI_ERR;
  }
  jmethodID newEventMethod = (*env)->GetMethodID(env, eventFactoryClass, "newEvent", "()Ljdk/jfr/Event;");
  if (newEventMethod == NULL) {
     fprintf(stderr, "GetMethodID(newEvent) failed\n");
    return JNI_ERR;
  }
  jmethodID setMethod = (*env)->GetMethodID(env, eventClass, "set", "(ILjava/lang/Object;)V");
  if (setMethod == NULL) {
     fprintf(stderr, "GetMethodID(set) failed\n");
    return JNI_ERR;
  }
  jmethodID beginMethod = (*env)->GetMethodID(env, eventClass, "begin", "()V");
  if (beginMethod == NULL) {
     fprintf(stderr, "GetMethodID(begin) failed\n");
    return JNI_ERR;
  }
  jmethodID commitMethod = (*env)->GetMethodID(env, eventClass, "commit", "()V");
  if (commitMethod == NULL) {
     fprintf(stderr, "GetMethodID(commit) failed\n");
    return JNI_ERR;
  }

  jfrInfo.newEventMethod = newEventMethod;
  jfrInfo.setMethod = setMethod;
  jfrInfo.beginMethod = beginMethod;
  jfrInfo.commitMethod = commitMethod;

  jobject trueObject;
  jint getTrue_result = getBooleanField(env, "TRUE", &trueObject);
  if (getTrue_result != JNI_OK) {
    fprintf(stderr, "getBooleanField(TRUE) failed\n");
    return JNI_ERR;
  }
  jfrInfo.trueObject = (*env)->NewGlobalRef(env, trueObject);

  jobject falseObject;
  jint getFalse_result = getBooleanField(env, "FALSE", &falseObject);
  if (getFalse_result != JNI_OK) {
    fprintf(stderr, "getBooleanField(FALSE) failed\n");
    return JNI_ERR;
  }
  jfrInfo.falseObject = (*env)->NewGlobalRef(env, falseObject);

  jstring getStringCritical = (*env)->NewStringUTF(env, "GetStringCritical");
   if (getStringCritical == NULL) {
    fprintf(stderr, "NewStringUTF(GetStringCritical) failed\n");
    return JNI_ERR;
  }
  jfrInfo.getStringCritical = (*env)->NewGlobalRef(env, getStringCritical);

  jstring getPrimitiveArrayCritical = (*env)->NewStringUTF(env, "GetPrimitiveArrayCritical");
   if (getPrimitiveArrayCritical == NULL) {
    fprintf(stderr, "NewStringUTF(GetPrimitiveArrayCritical) failed\n");
    return JNI_ERR;
  }
  jfrInfo.getPrimitiveArrayCritical = (*env)->NewGlobalRef(env, getPrimitiveArrayCritical);

  (*env)->DeleteLocalRef(env, eventClass);

  return JNI_OK;
}

jint createEventFactory(JNIEnv *env) {
  jobject eventAnnotations;
  jobject valueDescriptors;

  jclass eventFactoryClass = (*env)->FindClass(env, "jdk/jfr/EventFactory");
  if (eventFactoryClass == NULL) {
    fprintf(stderr, "FindClass(jdk/jfr/EventFactory) failed\n");
    return JNI_ERR;
  }
  jmethodID createMethod = (*env)->GetStaticMethodID(env, eventFactoryClass,
                                                    "create", "(Ljava/util/List;Ljava/util/List;)Ljdk/jfr/EventFactory;");
  if (createMethod == NULL) {
    fprintf(stderr, "GetMethodID(jdk/jfr/EventFactory#create) failed\n");
    return JNI_ERR;
  }

  jint eventAnnotationsResult = getEventAnnotations(env, &eventAnnotations);
  if (eventAnnotationsResult != JNI_OK) {
    fprintf(stderr, "getEventAnnotations() failed\n");
    return JNI_ERR;
  }

  jint valueDescriptorsResult = getValueDescriptors(env, &valueDescriptors);
  if (valueDescriptorsResult != JNI_OK) {
    fprintf(stderr, "getValueDescriptors() failed\n");
    (*env)->DeleteLocalRef(env, eventAnnotations);
    return JNI_ERR;
  }

  jobject localEventFactory = (*env)->CallStaticObjectMethod(env, eventFactoryClass, createMethod,
                                                                  eventAnnotations, valueDescriptors);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    fprintf(stderr, "EventFactory.create() threw\n");
    return JNI_ERR;
  }

  (*env)->DeleteLocalRef(env, eventAnnotations);
  (*env)->DeleteLocalRef(env, valueDescriptors);

  jfrInfo.eventFactory = (*env)->NewGlobalRef(env, localEventFactory);
  jint lookupEventFactoryMethodsResult = lookupEventFactoryMethods(env, eventFactoryClass);
  if (lookupEventFactoryMethodsResult != JNI_OK) {
    fprintf(stderr, "lookupEventFactoryMethods() failed\n");
    return JNI_ERR;
  }

  (*env)->DeleteLocalRef(env, eventFactoryClass);
  (*env)->DeleteLocalRef(env, localEventFactory);

  return JNI_OK;
}

void beginEvent(JNIEnv *env, jstring methodName) {
  // eventFactory.newEvent()
  jobject event = (*env)->CallObjectMethod(env, jfrInfo.eventFactory, jfrInfo.newEventMethod);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    callInfo.event = NULL;
    fprintf(stderr, "EventFactory#newEvent threw\n");
    return;
  }

  // event.set(1, "GetPrimitiveArrayCritical");
  jint nameElementIndex = 1;
  (*env)->CallVoidMethod(env, event, jfrInfo.setMethod, nameElementIndex, methodName);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    callInfo.event = NULL;
    fprintf(stderr, "Eventy#set threw\n");
    return;
  }

  // event.begin();
  (*env)->CallVoidMethod(env, event, jfrInfo.beginMethod);
  if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
    callInfo.event = NULL;
    fprintf(stderr, "Event#begin threw\n");
    return;
  }

  callInfo.event = (*env)->NewGlobalRef(env, event);
  (*env)->DeleteLocalRef(env, event);
}


void endEvent(JNIEnv *env, jobject wasCopy) {
  jobject event = callInfo.event;
  if (event != NULL) {
    // event.set(0, true)
    jint nameElementIndex = 0;
    (*env)->CallVoidMethod(env, event, jfrInfo.setMethod, nameElementIndex, wasCopy);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
      callInfo.event = NULL;
      fprintf(stderr, "Eventy#set threw\n");
      return;
    }
    // event.commit();
    (*env)->CallVoidMethod(env, event, jfrInfo.commitMethod);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
      callInfo.event = NULL;
      fprintf(stderr, "Event#commit threw\n");
      return;
    }
    (*env)->DeleteGlobalRef(env, event);
  }
}

const jchar * RedirectedGetStringCritical(JNIEnv *env, jstring string, jboolean *isCopy) {
  criticals += 1;
  jboolean *actualCopy;
  if (criticals == 1) {
    if (isCopy == NULL) {
      // always request the information whether a copy was made
      actualCopy = &callInfo.witness;
    } else {
      actualCopy = isCopy;
    }
    callInfo.isCopy = actualCopy;
    beginEvent(env, jfrInfo.getStringCritical);
  } else {
   // since we don't create an event, we don't really care about whether a copy was made or not
    actualCopy = isCopy;
  }
  return originalJNIFunctions->GetStringCritical(env, string, actualCopy);
}

void endJfrEvent(JNIEnv *env) {
  jboolean wasCopy = *callInfo.isCopy;
  jobject wasCopyObject = wasCopy == JNI_TRUE ? jfrInfo.trueObject : jfrInfo.falseObject;
  endEvent(env, wasCopyObject);
}

void RedirectedReleaseStringCritical(JNIEnv *env, jstring string, const jchar *carray) {
  originalJNIFunctions->ReleaseStringCritical(env, string, carray);
  if (criticals == 1) {
    endJfrEvent(env);
  }
  criticals -= 1;
}

void * RedirectedGetPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy) {
  criticals += 1;
  jboolean *actualCopy;
  if (criticals == 1) {
    if (isCopy == NULL) {
      // always request the information whether a copy was made
      actualCopy = &callInfo.witness;
    } else {
      actualCopy = isCopy;
    }
    callInfo.isCopy = actualCopy;
    beginEvent(env, jfrInfo.getPrimitiveArrayCritical);
  } else {
   // since we don't create an event, we don't really care about whether a copy was made or not
    actualCopy = isCopy;
  }
  return originalJNIFunctions->GetPrimitiveArrayCritical(env, array, isCopy);
}

void RedirectedReleasePrimitiveArrayCritical(JNIEnv *env, jarray array, void *carray, jint mode) {
  originalJNIFunctions->ReleasePrimitiveArrayCritical(env, array, carray, mode);
  if (criticals == 1) {
    endJfrEvent(env);
  }
  criticals -= 1;
}

jint redirectJniCriticals(jvmtiEnv *jvmti) {

  jvmtiError tiErr = (*jvmti)->GetJNIFunctionTable(jvmti, &originalJNIFunctions);
  if (tiErr != JVMTI_ERROR_NONE) {
    fprintf(stderr, "GetJNIFunctionTable (JVMTI) failed with error(%d)\n", tiErr);
    return JNI_ERR;
  }
  tiErr = (*jvmti)->GetJNIFunctionTable(jvmti, &redirectedJNIFunctions);
  if (tiErr != JVMTI_ERROR_NONE) {
    fprintf(stderr, "GetJNIFunctionTable (JVMTI) failed with error(%d)\n", tiErr);
    return JNI_ERR;
  }

  redirectedJNIFunctions->GetStringCritical = RedirectedGetStringCritical;
  redirectedJNIFunctions->ReleaseStringCritical = RedirectedReleaseStringCritical;
  redirectedJNIFunctions->GetPrimitiveArrayCritical = RedirectedGetPrimitiveArrayCritical;
  redirectedJNIFunctions->ReleasePrimitiveArrayCritical = RedirectedReleasePrimitiveArrayCritical;

  tiErr = (*jvmti)->SetJNIFunctionTable(jvmti, redirectedJNIFunctions);
  //(*jvmti)->Deallocate(jvmti, (unsigned char*) redirectedJNIFunctions);
  if (tiErr != JVMTI_ERROR_NONE) {
    fprintf(stderr, "SetJNIFunctionTable (JVMTI) failed with error(%d)\n", tiErr);
    return JNI_ERR;
  }
  return JNI_OK;

}

void JNICALL cbVMStart(jvmtiEnv *jvmti_env, JNIEnv* jni_env) {
  createEventFactory(jni_env);
  redirectJniCriticals(jvmti_env);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  jvmtiEventCallbacks callbacks;
  jvmtiError          error;
  jvmtiEnv            *jvmti;
  
  jint niErr = (*jvm)->GetEnv(jvm, (void**) &jvmti, JVMTI_VERSION_11);
  if (niErr != JNI_OK) {
    fprintf(stderr, "GetEnv (JVMTI) failed with error(%d)\n", niErr);
    return JNI_ERR;
  }

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.VMStart = &cbVMStart;

  error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, (jint) sizeof(callbacks));
  if (error != JVMTI_ERROR_NONE) {
    fprintf(stderr, "SetEventCallbacks (JVMTI) failed with error(%d)\n", niErr);
    return JNI_ERR;
  }
  error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_START, (jthread) NULL);
  if (error != JVMTI_ERROR_NONE) {
    fprintf(stderr, "SetEventNotificationMode (JVMTI) failed with error(%d)\n", niErr);
    return JNI_ERR;
  }
  return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *jvm, char *options, void *reserved) {
  jvmtiEnv *jvmti;
  jint niErr = (*jvm)->GetEnv(jvm, (void**) &jvmti, JVMTI_VERSION_11);
  if (niErr != JNI_OK) {
    fprintf(stderr, "GetEnv (JVMTI) failed with error(%d)\n", niErr);
    return JNI_ERR;
  }
  return redirectJniCriticals(jvmti);
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *jvm) {
  // jvmtiError Deallocate(jvmtiEnv* env, unsigned char* mem)

  // if (eventFactory != NULL)
  // (*env)->DeleteGlobalRef(env, eventFactory);
}

