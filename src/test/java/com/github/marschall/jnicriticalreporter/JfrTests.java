package com.github.marschall.jnicriticalreporter;

import java.util.List;

import org.junit.jupiter.api.Test;

import jdk.jfr.AnnotationElement;
import jdk.jfr.Category;
import jdk.jfr.Description;
import jdk.jfr.Event;
import jdk.jfr.EventFactory;
import jdk.jfr.Label;
import jdk.jfr.Name;
import jdk.jfr.ValueDescriptor;


public class JfrTests {

  @Test
  void customEvent() {
    EventFactory eventFactory = EventFactory.create(getEventAnnotations(), getValueDescriptors());
    Event event = eventFactory.newEvent();
    event.set(0, true);
    event.set(1, "GetPrimitiveArrayCritical");
    event.begin();
    
    event.commit();
  }
  
  private static List<AnnotationElement> getEventAnnotations() {
    String[] category = { "JNI" };
    return List.of(
      new AnnotationElement(Name.class, "com.github.marschall.jnicriticalreporter.Event"),
      new AnnotationElement(Label.class, "JNI Critical"),
      new AnnotationElement(Description.class, "Lists invocation of JNI critical methods"),
      new AnnotationElement(Category.class, category));
//    new AnnotationElement(StackTrace.class, true));
  }
  
  private static List<ValueDescriptor> getValueDescriptors() {
    List<AnnotationElement> isCopyAnnotations = List.of(
        new AnnotationElement(Label.class, "IsCopy"),
        new AnnotationElement(Description.class, "Whether the memory was copied"));
    ValueDescriptor isCopyDescriptor = new ValueDescriptor(boolean.class, "isCopy", isCopyAnnotations);

    List<AnnotationElement> methodNameAnnotations = List.of(
        new AnnotationElement(Label.class, "Method Name"),
        new AnnotationElement(Description.class, "Name of the JNI critical method"));
    ValueDescriptor methodNameDescriptor = new ValueDescriptor(String.class, "methodName", methodNameAnnotations);

    return List.of(isCopyDescriptor, methodNameDescriptor);
  }

}
