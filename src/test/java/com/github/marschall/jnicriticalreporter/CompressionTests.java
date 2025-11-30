package com.github.marschall.jnicriticalreporter;

import static java.nio.charset.StandardCharsets.UTF_8;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

import org.junit.jupiter.api.Test;

class CompressionTests {

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
