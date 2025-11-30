package com.github.marschall.jnicriticalreporter;

import static java.nio.charset.StandardCharsets.UTF_8;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import jdk.jfr.Recording;
import jdk.jfr.consumer.RecordingFile;

class CompressionTests {

  private static final String JNI_CRITICAL_EVENT = "com.github.marschall.jnicriticalreporter.Event";
  private Recording recording;

  @BeforeEach
  void startRecording() {
    this.recording = new Recording();
    this.recording.enable(JNI_CRITICAL_EVENT);
    long oneMb = 1L * 1024L * 1024L;
    this.recording.setMaxSize(oneMb);
    this.recording.start();
  }

  @AfterEach
  void stopRecording() throws IOException {
    var recordingPath = Path.of("target", "CompressionTests.jfr");
    this.recording.dump(recordingPath);
    this.recording.close();
    AtomicInteger eventCount = new AtomicInteger(0);
    try (var recordingFile = new RecordingFile(recordingPath)) {
      while (recordingFile.hasMoreEvents()) {
        var event = recordingFile.readEvent();
        var eventType = event.getEventType();
        if (eventType.getName().equals(JNI_CRITICAL_EVENT)) {
          eventCount.incrementAndGet();
        }
      }
    }
    assertTrue(eventCount.get() > 0, "no events encountered");
  }

  @Test
  void roundTrip() throws IOException {
    byte[] input = CompressionTests.class.getName().getBytes(UTF_8);
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    String entryName = "name";
    try (ZipOutputStream deflater = new ZipOutputStream(bos)) {
      deflater.putNextEntry(new ZipEntry(entryName));
      deflater.write(input);
    }
    byte[] compressed = bos.toByteArray();
    try (ZipInputStream inflater = new ZipInputStream(new ByteArrayInputStream(compressed))) {

      ZipEntry entry = inflater.getNextEntry();
      assertNotNull(entry);
      assertEquals(entryName, entry.getName());
      byte[] decompressed = inflater.readAllBytes();
      assertArrayEquals(input, decompressed);

      entry = inflater.getNextEntry();
      assertNull(entry);
    }
  }

}
