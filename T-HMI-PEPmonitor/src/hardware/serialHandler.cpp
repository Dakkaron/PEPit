#include "serialHandler.h"
#include "prefsHandler.h"

void readFileToSerialSlow(const char *path, bool readAsHex, bool printFileSize) {
  Serial.printf("Reading file: %s\n", path);

  File file = SD_MMC.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  uint8_t byteNr = 0;
  if (printFileSize) {
    Serial.println(file.size());
  }
  Serial.println("Read from file: ");
  while (file.available()) {
    if (readAsHex) {
      char hexBuffer[3];
      sprintf(hexBuffer, "%02x", file.read());
      Serial.print(hexBuffer);
      Serial.print(" ");
      byteNr++;
      if (byteNr % 16 == 0) {
        Serial.println();
      }
    } else {
      Serial.write(file.read());
    }
  }
  Serial.println();
}

void readFileToSerial(const char *path, bool readAsHex, bool printFileSize) {
  Serial.printf("Reading file: %s\n", path);

  File file = SD_MMC.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  uint8_t byteNr = 0;
  uint32_t fileSize = file.size();
  if (printFileSize) {
    Serial.println(fileSize);
  }
  uint8_t* fileBuffer = new uint8_t[fileSize];
  file.read(fileBuffer, fileSize);

  Serial.println("Read from file: ");
  if (readAsHex) {
    for (uint32_t i=0;i<fileSize;i++) {
      char hexBuffer[3];
      sprintf(hexBuffer, "%02x", fileBuffer[i]);
      Serial.print(hexBuffer);
      Serial.print(" ");
      byteNr++;
      if (byteNr % 16 == 0) {
        Serial.println();
      }
    }
  } else {
    Serial.write(fileBuffer, fileSize);
  }
  free(fileBuffer);
  Serial.println();
}

bool dirExists(const char *dirname) {
  File root = SD_MMC.open(dirname);
  if (!root) {
    return false;
  }
  return root.isDirectory();
}


void listDir(const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = SD_MMC.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.endsWith("System Volume Information")) {
      // Skipping pesky System Volume Information folder
    } else if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(fileName);
      if (levels) {
        listDir(file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(fileName);
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
  Serial.println();
  Serial.print("$ ");
}

String readSerialLine() {
  String serialRead;
  uint32_t nextYield = millis() + 1;
  while (true) { //  Read file path
    if (Serial.available() > 0) {
      char readCharacter = Serial.read();
      Serial.print(readCharacter);
      if (readCharacter == 0 || readCharacter == 10 || readCharacter == 13) {
        return serialRead;
      }
      serialRead += readCharacter;
    }
    if (millis() > nextYield) {
      nextYield = millis() + 1;
      vTaskDelay(1); // watchdog
    }
  }
}


void handleUploadFileMode() {
  Serial.println("Upload mode");
  String path = readSerialLine();
  Serial.print("Writing to file: '");
  Serial.print(path);
  Serial.println("'");

  uint32_t fileLength = atoi(readSerialLine().c_str());
  Serial.print("Number of bytes: '");
  Serial.print(fileLength);
  Serial.println("'");

  uint8_t* fileBuffer = new uint8_t[fileLength+1];

  if (SD_MMC.exists(path + ".tmp")) {
    Serial.println("Deleting existing tmp file");
    SD_MMC.remove(path + ".tmp");
  }

  File file = SD_MMC.open(path + ".tmp", FILE_WRITE, true);
  Serial.println("Starting transmission");
  Serial.flush();
  for (uint32_t i=0;i<fileLength;i++) {
    while (Serial.available()==0) {}
    fileBuffer[i] = Serial.read();
    Serial.write(fileBuffer[i]);
    Serial.flush();
  }
  fileBuffer[fileLength] = 0;
  file.write(fileBuffer, fileLength);

  free(fileBuffer);
  file.close();
  Serial.println();
  Serial.println("Done writing tmp file");

  if (SD_MMC.exists(path)) {
    Serial.println("Deleting existing file");
    SD_MMC.remove(path);
  }

  SD_MMC.rename(path + ".tmp", path);
  Serial.println("Renamed tmp file to final file");
}

void handleUploadFileModeMedium() {
  Serial.println("Upload mode");
  String path = readSerialLine();
  Serial.print("Writing to file: '");
  Serial.print(path);
  Serial.println("'");

  uint32_t fileLength = atoi(readSerialLine().c_str());
  Serial.print("Number of bytes: '");
  Serial.print(fileLength);
  Serial.println("'");

  uint8_t* fileBuffer = new uint8_t[fileLength+1];

  if (SD_MMC.exists(path)) {
    Serial.println("Deleting existing file");
    SD_MMC.remove(path);
  }

  File file = SD_MMC.open(path, FILE_WRITE, true);

  int32_t remainingBytes = fileLength;
  while (remainingBytes>0) {
    //Serial.readBytes(&(fileBuffer[fileLength-remainingBytes]), _min(remainingBytes, 512));
    //remainingBytes -= 512;
    if (Serial.available()>0) {
      char read = Serial.read();
      Serial.print(read);
      fileBuffer[fileLength-remainingBytes] = read;
      remainingBytes--;
      if (remainingBytes%1024 == 0) {
        Serial.println(remainingBytes);
        vTaskDelay(1); // watchdog
      }
    }
  }
  fileBuffer[fileLength] = 0;
  
  Serial.println("[Result:]");
  Serial.print((char*)fileBuffer);
  Serial.println("[Result end]");
  
  file.write(fileBuffer, fileLength);

  free(fileBuffer);
  file.close();
  Serial.println();
  Serial.println("Done writing file");
}

void handleUploadFileModeSlow() {
  Serial.println("Upload mode");
  String path = readSerialLine();
  Serial.print("Writing to file: '");
  Serial.print(path);
  Serial.println("'");

  uint32_t fileLength = atoi(readSerialLine().c_str());
  Serial.print("Number of bytes: '");
  Serial.print(fileLength);
  Serial.println("'");

  uint32_t bytesRead = 0;
  if (SD_MMC.exists(path)) {
    Serial.println("Deleting existing file");
    SD_MMC.remove(path);
  }

  File file = SD_MMC.open(path, FILE_WRITE, true);
  while (bytesRead<fileLength) {
    if (Serial.available() > 0) {
      char readCharacter = Serial.read();
      file.print(readCharacter);
      Serial.print(readCharacter);
      bytesRead++;
      if (bytesRead % 100 == 0) {
        Serial.println();
        Serial.println(bytesRead);
      }
    }
  }
  file.close();
  Serial.println();
  Serial.println("Done writing file");
}

void handlePrintVersion() {
  Serial.print("PEPit Version '");
  Serial.print(VERSION);
  Serial.println("'");
}

void handleLs() {
  String line = readSerialLine();
  if (line.indexOf(" ")==-1) {
    listDir(line.c_str(), 0);
  } else {
    String path = line.substring(0,line.indexOf(" "));
    uint32_t levels = atoi(line.substring(line.indexOf(" ")+1).c_str());
    listDir(path.c_str(), levels);
  }
}

void handleMkdir() {
  String line = readSerialLine();
  String path = line.substring(0,line.indexOf(" "));
  if (!SD_MMC.mkdir(path)) {
    Serial.println("Error: could not create directory!");
  }
}

void handleRmdir() {
  String line = readSerialLine();
  String path = line.substring(0,line.indexOf(" "));
  if (!SD_MMC.rmdir(path)) {
    Serial.println("Error: could not delete directory!");
  }
}

void handleRm() {
  String line = readSerialLine();
  String path = line.substring(0,line.indexOf(" "));
  if (!SD_MMC.remove(path)) {
    Serial.println("Error: could not delete file!");
  }
}

char serialCommandBuffer[16];
uint32_t serialCommandCharacterCount = 0;
void handleSerial() {
  if (Serial.available() > 0) {
    char charRead = Serial.read();
    serialCommandBuffer[serialCommandCharacterCount++] = charRead;
    Serial.print(charRead);
    serialCommandBuffer[serialCommandCharacterCount] = 0;
    if (strcasecmp(serialCommandBuffer, "ul ") == 0) {
      handleUploadFileMode();
    } else if (strcasecmp(serialCommandBuffer, "ver ") == 0) {
      handlePrintVersion();
    } else if (strcasecmp(serialCommandBuffer, "ls ") == 0) {
      handleLs();
    } else if (strcasecmp(serialCommandBuffer, "ls\n") == 0) {
      listDir("/", 0);
    } else if (strcasecmp(serialCommandBuffer, "cat ") == 0) {
      readFileToSerial(readSerialLine().c_str(), false, false);
    } else if (strcasecmp(serialCommandBuffer, "catl ") == 0) {
      readFileToSerial(readSerialLine().c_str(), false, true);
    } else if (strcasecmp(serialCommandBuffer, "catx ") == 0) {
      readFileToSerial(readSerialLine().c_str(), true, false);
    } else if (strcasecmp(serialCommandBuffer, "catxl ") == 0) {
      readFileToSerial(readSerialLine().c_str(), true, true);
    } else if (strcasecmp(serialCommandBuffer, "mkdir ") == 0) {
      handleMkdir();
    } else if (strcasecmp(serialCommandBuffer, "rmdir ") == 0) {
      handleRmdir();
    } else if (strcasecmp(serialCommandBuffer, "rm ") == 0) {
      handleRm();
    } else if (strcasecmp(serialCommandBuffer, "printnamespaces ") == 0) {
      printNamespaces();
    } else if (strcasecmp(serialCommandBuffer, "dumpnamespaces ") == 0) {
      dumpNamespaceContents();
    } else if (strcasecmp(serialCommandBuffer, "clearprefs ") == 0) {
      clearPreferencesExceptSystem();
    }

    if (charRead == 32 || charRead == 0 || charRead == 10) { // Check for space, \n or \0
      for (uint32_t i=0;i<16;i++) {
        serialCommandBuffer[i] = 0;
      }
      serialCommandCharacterCount = 0;
    }

    if (serialCommandCharacterCount>15) {
      serialCommandCharacterCount = 0;
    }
  }
}
