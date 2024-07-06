/**************************************************************
 * Class::  CSC-415-02 Summer 2024
 * Name:: Xuefeng Guan
 * Student ID:: 920016536
 * GitHub-Name:: XuefengGuan1
 * Project:: Assignment 5 – Buffered I/O read
 *
 * File:: b_io.c
 *
 * Description::This file adds the open, read and 
 * 				close functionalities. It can support a max 
 * 				of 20 files opened at the same time.
 *
 **************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "b_io.h"
#include "fsLowSmall.h"

#define MAXFCBS 20 // The maximum number of files open at one time

// This structure is all the information needed to maintain an open file
// It contains a pointer to a fileInfo structure and any other information
// that you need to maintain your open file.
typedef struct b_fcb
{
	fileInfo *fi; // holds the low level systems file info
	// Add any other needed variables here to track the individual open file
	char fBuffer[B_CHUNK_SIZE];
	uint64_t position;
	int remainingByte;
	int isEOF;
} b_fcb;

// static array of file control blocks
b_fcb fcbArray[MAXFCBS];

// Indicates that the file control block array has not been initialized
int startup = 0;

// Method to initialize our file system / file control blocks
// Anything else that needs one time initialization can go in this routine
void b_init()
{
	if (startup)
		return; // already initialized

	// init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
	{
		fcbArray[i].fi = NULL; // indicates a free fcbArray
	}

	startup = 1;
}

// Method to get a free File Control Block FCB element
b_io_fd b_getFCB()
{
	for (int i = 0; i < MAXFCBS; i++)
	{
		if (fcbArray[i].fi == NULL)
		{
			fcbArray[i].fi = (fileInfo *)-2; // used but not assigned
			return i;						 // Not thread safe but okay for this project
		}
	}

	return (-1); // all in use
}

// b_open is called by the "user application" to open a file. This routine is
// similar to the Linux open function.
// You will create your own file descriptor which is just an integer index into an
// array of file control blocks (fcbArray) that you maintain for each open file.
// For this assignment the flags will be read only and can be ignored.

b_io_fd b_open(char *filename, int flags)
{
	if (startup == 0)
		b_init(); // Initialize our system
	//*** TODO ***//  Write open function to return your file descriptor
	//                  You may want to allocate the buffer here as well
	//                  But make sure every file has its own buffer

	// Create a file descriptor and remember that one has got a file
	// This is where you are going to want to call GetFileInfo and b_getFCB

	// Get low-level file info
	fileInfo *fInfo = GetFileInfo(filename);

	// Get the file descriptor value, range from 0-19
	b_io_fd fd;
	if (fInfo == NULL)
	{
		perror("File entered is unable to open\n");
		return -1;
	}
	if ((fd = b_getFCB()) == -1)
	{
		perror("Get file control block failed\n");
		return -1;
	}

	// Init the structure
	fcbArray[fd].fi = fInfo;
	fcbArray[fd].position = 0;
	fcbArray[fd].remainingByte = 0;
	fcbArray[fd].isEOF = 0;

	return fd;
}

// b_read functions just like its Linux counterpart read. The user passes in
// the file descriptor (index into fcbArray), a buffer where they want you to
// place the data, and a count of how many bytes they want from the file.
// The return value is the number of bytes you have copied into their buffer.
// The return value can never be greater than the requested count, but it can
// be less only when you have run out of bytes to read. i.e. End of File
int b_read(b_io_fd fd, char *buffer, int count)
{
	//*** TODO ***//
	// Write buffered read function to return the data and # bytes read
	// You must use LBAread and you must buffer the data in B_CHUNK_SIZE byte chunks.

	if (startup == 0)
		b_init(); // Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
	{
		return (-1); // invalid file descriptor
	}

	// and check that the specified FCB is actually in use
	if (fcbArray[fd].fi == NULL) // File not open for this descriptor
	{
		return -1;
	}

	// Your Read code here - the only function you call to get data is LBAread.
	// Track which byte in the buffer you are at, and which block in the file
	int byteRead = 0;
	int toCopy;
	int blockRead;

	// Check if EOF has reached, would return 0 bytes copied
	if (fcbArray[fd].isEOF == 1)
	{
		return 0;
	}

	// Check if we're trying to read more than what's left in the file
	/* If user count is greater than what's left in the file, simply copy
	 the remaining characters in the file*/
	if (fcbArray[fd].fi->fileSize - fcbArray[fd].position < count)
	{
		count = fcbArray[fd].fi->fileSize - fcbArray[fd].position;
	}

	// If the buffer is not empty
	if (fcbArray[fd].remainingByte > 0)
	{
		// If there are more remaining bytes than user request
		if (count < fcbArray[fd].remainingByte)
		{
			toCopy = count;
		}
		else
		{
			// If there remaining bytes are not enough to fulfil user request
			toCopy = fcbArray[fd].remainingByte;
		}
		// Transfer code from our own buffer to the user buffer
		// Make sure to not give more than our buffer has, which would cause overflow
		memcpy(buffer, fcbArray[fd].fBuffer + (B_CHUNK_SIZE - fcbArray[fd].remainingByte), toCopy);
		fcbArray[fd].remainingByte -= toCopy;
		byteRead += toCopy;
		count -= toCopy;
		buffer += toCopy;
		fcbArray[fd].position += toCopy;
	}

	// Read full blocks
	while (count >= B_CHUNK_SIZE) 
	{
		/*Read a whole block of data directly to user's buffer,
		since reading it to our buffer first is redundant.*/
		blockRead = LBAread(buffer, 1, fcbArray[fd].fi->location++);  
		// Make sure to remember that EOF has reached
		if (blockRead <= 0)
		{
			fcbArray[fd].isEOF = 1;
			return byteRead;
		}
		byteRead += B_CHUNK_SIZE;
		buffer += B_CHUNK_SIZE;
		count -= B_CHUNK_SIZE;
		fcbArray[fd].position += B_CHUNK_SIZE;
	}

	// If there user still wants more bytes
	if (count > 0)
	{
		// Read a new block of data to our buffer
		blockRead = LBAread(fcbArray[fd].fBuffer, 1, fcbArray[fd].fi->location++);
		// Make sure to label end of file
		if (blockRead <= 0)
		{
			fcbArray[fd].isEOF = 1;
			return byteRead;
		}
		// Copy the rest of the user requested bytes to the user's buffer.
		memcpy(buffer, fcbArray[fd].fBuffer, count);
		fcbArray[fd].remainingByte = B_CHUNK_SIZE - count;
		byteRead += count;
		fcbArray[fd].position += count;
	}

	return byteRead;
}

// b_close frees any allocated memory and places the file control block back
// into the unused pool of file control blocks.
int b_close(b_io_fd fd)
{
	//*** TODO ***//  Release any resources
	if (fcbArray[fd].fi == NULL)
	{
		return -1; // invalid file descriptor
	}

	//Release everything, and init the specific file struct
	fcbArray[fd].fi = NULL;
	fcbArray[fd].position = 0;
	fcbArray[fd].remainingByte = 0;
	fcbArray[fd].isEOF = 0;

	return 0; // successfully closed
}