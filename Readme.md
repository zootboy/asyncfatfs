## AsyncFatFS

AsyncFatFS is a minimalist, fully asynchronous FAT16 / FAT32 file IO library. When paired with a simple
asynchronous read-block/write-block API for an SD card or other storage backend, this library provides file IO
primitives (e.g. fopen, fread, fseek) in a fully non-blocking manner. All API calls either provide a callback
to notify you of operation completion, or return a status code that indicates that the operation should be
retried later. This allows AsyncFatFS to run well on embedded systems that do not have an operating system or
threading capabilities.

A special optional feature of this filesystem is a high-speed contiguous append file mode, provided by the "freefile"
support. In this mode, the largest contiguous free block on the volume is pre-allocated during filesystem 
initialisation into a file called "freespac.e". One file can be created which slowly grows from the beginning 
of this contiguous region, stealing the first part of the freefile's space. Because the freefile is contiguous, 
the FAT entries for the file need never be read. This saves on buffer space and reduces latency during file
extend operations.

### Implementing AsyncFatFS

The API for both AsyncFatFS itself, as well as the required driver functions for the storage backend, is
[documented here](doc/Using_asyncfatfs.md).

You can use the testcases in the `test/` directory as a guide to how to use the filesystem. AsyncFatFS is also used in
Cleanflight / Betaflight's "blackbox" logging system: [filesystem consumer code](https://github.com/betaflight/betaflight/blob/master/src/main/blackbox/blackbox_io.c), 
[SDCard SPI driver code](https://github.com/betaflight/betaflight/blob/master/src/main/drivers/sdcard_spi.c).

You'll notice that since most filesystem operations will fail and ask you to retry when the card is busy, it becomes 
natural to call it using a state-machine from your app's main loop - where you only advance to the next state once the 
current operation succeeds, calling afatfs_poll() in between so that the filesystem can complete its queued tasks.
