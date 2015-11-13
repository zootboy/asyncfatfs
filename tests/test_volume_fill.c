#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sdcard_sim.h"
#include "sdcard.h"
#include "sdcard_standard.h"
#include "asyncfatfs.h"

#define LOG_ENTRY_WRITE_MAX 100000000

typedef enum {
    TEST_STAGE_INIT = 0,
    TEST_STAGE_CREATE_LOG_DIRECTORY = 0,
    TEST_STAGE_CREATE_LOG_FILE,
    TEST_STAGE_WRITE_LOG,
    TEST_STAGE_CLOSE_LOG,
    TEST_STAGE_OPEN_LOG_FOR_READ,
    TEST_STAGE_READ_LOG,
    TEST_STAGE_IDLE,
    TEST_STAGE_COMPLETE
} testStage_e;

static testStage_e testStage = TEST_STAGE_INIT;
static afatfsFilePtr_t testFile;

static int writeLogFileNumber = 0;
static int readLogFileNumber = 0;

static int writeLogEntryCount = 0;
static int readLogEntryCount = 0;

static uint32_t writtenBytesThisFile = 0;
static uint32_t readBytesThisFile = 0;

static uint32_t writtenBytesTotal = 0;
static uint32_t readBytesTotal = 0;

void printFSState(afatfsFilesystemState_e state)
{
    switch (state) {
       case AFATFS_FILESYSTEM_STATE_UNKNOWN:
           printf("Filesystem in unknown state\n");
       break;
       case AFATFS_FILESYSTEM_STATE_READY:
           printf("Filesystem online!\n");
       break;
       case AFATFS_FILESYSTEM_STATE_FATAL:
           printf("Fatal error\n");
           exit(-1);
       break;
       case AFATFS_FILESYSTEM_STATE_INITIALIZATION:
           printf(".");
       break;
       default:
           printf("Filesystem in unknown state %d!\n", (int) state);
       break;
   }
}

void logFileCreatedForWrite(afatfsFilePtr_t file)
{
    if (file) {
        testFile = file;

        testStage = TEST_STAGE_WRITE_LOG;
        fprintf(stderr, "Writing log entries to LOG%05d.TXT...\n", writeLogFileNumber);
    } else {
        fprintf(stderr, "Creating testfile failed\n");
        testStage = TEST_STAGE_COMPLETE;
    }
}

void logDirCreated(afatfsFilePtr_t dir)
{
    if (!dir) {
        fprintf(stderr, "Creating 'logs' directory failed\n");
        exit(EXIT_FAILURE);
    }

    afatfs_chdir(dir);

    afatfs_fclose(dir);

    testStage = TEST_STAGE_CREATE_LOG_FILE;
}

void logFileOpenedForRead(afatfsFilePtr_t file)
{
    if (file) {
        testFile = file;

        testStage = TEST_STAGE_READ_LOG;
        fprintf(stderr, "Validating LOG%05d.TXT...\n", readLogFileNumber);
    } else {
        fprintf(stderr, "Opening log for read failed\n");
        testStage = TEST_STAGE_COMPLETE;
    }
}


bool continueTesting() {
    char testBuffer[64];
    char filename[13];
    uint32_t readBytes;

    switch (testStage) {
        case TEST_STAGE_CREATE_LOG_DIRECTORY:
            // Create a subdirectory for logging

            /*
             * The callback can be called before mkdir() returns, so set the testStage now to avoid stomping on state
             * set by the callback:
             */
            testStage = TEST_STAGE_IDLE;

            afatfs_mkdir("logs", logDirCreated);
        break;
        case TEST_STAGE_CREATE_LOG_FILE:

            if (writeLogFileNumber >= 1000) {
                testStage = TEST_STAGE_COMPLETE;
            } else {
                testStage = TEST_STAGE_IDLE;

                writeLogEntryCount = 0;
                writtenBytesThisFile = 0;

                // Write a file in contigous-append mode
                sprintf(filename, "LOG%05d.TXT", writeLogFileNumber);

                afatfs_fopen(filename, "as", logFileCreatedForWrite);
            }
        break;
        case TEST_STAGE_WRITE_LOG:
            if (writeLogEntryCount >= LOG_ENTRY_WRITE_MAX) {
                testStage = TEST_STAGE_CLOSE_LOG;
            } else {
                sprintf(testBuffer, "Log %05d entry %6d/%6d\n", writeLogFileNumber, writeLogEntryCount + 1, LOG_ENTRY_WRITE_MAX);

                uint32_t writtenBytes;

                writtenBytes = afatfs_fwrite(testFile, (uint8_t*) testBuffer, strlen(testBuffer));

                if (writtenBytes > 0) {
                    writtenBytesThisFile += writtenBytes;
                    // Only move on to the next log entry if the write succeeded entriely
                    if (writtenBytes == strlen(testBuffer)) {
                        writeLogEntryCount++;
                    }
                } else if (afatfs_isFull()) {
                    testStage = TEST_STAGE_CLOSE_LOG;
                }
            }
        break;
        case TEST_STAGE_CLOSE_LOG:
            // Wait for the file to close
            if (!afatfs_fclose(testFile)) {
                break;
            }

            writtenBytesTotal += writtenBytesThisFile;
            writeLogFileNumber++;

            testStage = TEST_STAGE_OPEN_LOG_FOR_READ;
        break;
        case TEST_STAGE_OPEN_LOG_FOR_READ:
            if (readLogFileNumber == writeLogFileNumber) {
                testStage = TEST_STAGE_COMPLETE;
            } else {
                testStage = TEST_STAGE_IDLE;

                readLogEntryCount = 0;

                sprintf(filename, "LOG%05d.TXT", readLogFileNumber);

                afatfs_fopen(filename, "r", logFileOpenedForRead);
            }
        break;
        case TEST_STAGE_READ_LOG:
            readBytes = afatfs_fread(testFile, (uint8_t*)testBuffer, sizeof(testBuffer));

            if (readBytes == 0 && afatfs_feof(testFile)) {
                afatfs_fclose(testFile);

                readBytesTotal += readBytesThisFile;

                if (readLogEntryCount < writeLogEntryCount) {
                    fprintf(stderr, "[Fail]     Wrote %u log entries but only read back %u\n", writeLogEntryCount, readLogEntryCount);
                    exit(-1);
                }

                if (readBytesThisFile < writtenBytesThisFile) {
                    fprintf(stderr, "[Fail]     Wrote %u bytes but only read back %u\n", writtenBytesThisFile, readBytesThisFile);
                    exit(-1);
                }

                if (afatfs_isFull()) {
                    testStage = TEST_STAGE_COMPLETE;
                } else {
                    testStage = TEST_STAGE_CREATE_LOG_FILE;
                }
            } else {
                readBytesThisFile += readBytes;
                // Count how many lines we see in the file and use that to decide how many "log entries" we read
                for (uint32_t i = 0; i < readBytes; i++) {
                    if (testBuffer[i] == '\n') {
                        readLogEntryCount++;
                    }
                }
            }
        break;
        case TEST_STAGE_IDLE:
            // Waiting for file operations...
        break;
        case TEST_STAGE_COMPLETE:
            fprintf(stderr, "[Success]  Logged %u bytes in %u files to fill the device\n", writtenBytesTotal, writeLogFileNumber);
            return false;
    }

    // Continue test...
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Missing argument for sdcard image filename\n");
        return EXIT_FAILURE;
    }

    if (!sdcard_sim_init(argv[1])) {
        fprintf(stderr, "sdcard_sim_init() failed\n");
        return EXIT_FAILURE;
    }

    if (!sdcard_init()) {
        fprintf(stderr, "sdcard_init() failed\n");
        return EXIT_FAILURE;
    }

    afatfs_init();

    bool keepGoing = true;

    while (keepGoing) {
        afatfs_poll();

        switch (afatfs_getFilesystemState()) {
            case AFATFS_FILESYSTEM_STATE_READY:
                if (!continueTesting()) {
                    keepGoing = false;
                    break;
                }
           break;
           case AFATFS_FILESYSTEM_STATE_FATAL:
                fprintf(stderr, "[Fail]     Fatal filesystem error\n");
                exit(-1);
           default:
               ;
        }
    }

    while (!afatfs_destroy()) {
    }

    sdcard_sim_destroy();

    return EXIT_SUCCESS;
}