#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include "partition.h"
//#include "tree.h"
#include <string.h>
#include "small_printf.h"

/* File function return code (FRESULT) */
typedef enum {
    FR_OK = 0,          /* 0 */
    FR_DISK_ERR,        /* 1 */
    FR_INT_ERR,         /* 2 */
    FR_NOT_READY,       /* 3 */
    FR_NO_FILE,         /* 4 */
    FR_NO_PATH,         /* 5 */
    FR_INVALID_NAME,    /* 6 */
    FR_DENIED,          /* 7 */
    FR_EXIST,           /* 8 */
    FR_INVALID_OBJECT,  /* 9 */
    FR_WRITE_PROTECTED, /* 10 */
    FR_INVALID_DRIVE,   /* 11 */
    FR_NOT_ENABLED,     /* 12 */
    FR_NO_FILESYSTEM,   /* 13 */
    FR_MKFS_ABORTED,    /* 14 */
    FR_TIMEOUT,         /* 15 */
    FR_NO_MEMORY,		/* 16 */
    FR_DISK_FULL        /* 17 */
} FRESULT;

/* File attribute bits for directory entry */

#define AM_RDO   0x01    /* Read only */
#define AM_HID   0x02    /* Hidden */
#define AM_SYS   0x04    /* System */
#define AM_VOL   0x08    /* Volume label */
#define AM_LFN   0x0F    /* LFN entry */
#define AM_DIR   0x10    /* Directory */
#define AM_ARC   0x20    /* Archive */
#define AM_MASK  0x3F    /* Mask of defined bits */

/*--------------------------------------------------------------*/
/* File access control and file status flags (FIL.flag)         */

#define FA_READ             0x01
#define FA_OPEN_EXISTING    0x00
#define FA_WRITE            0x02
#define FA_CREATE_NEW       0x04
#define FA_CREATE_ALWAYS    0x08
#define FA_OPEN_ALWAYS      0x10
#define FA__WRITTEN         0x20
#define FA__DIRTY           0x40
#define FA__ERROR           0x80


class Path;
class FileSystem;

class FileInfo
{
public:
	FileSystem *fs;  /* Reference to file system, to uniquely identify file */
    DWORD   cluster; /* Start cluster, easy for open! */
    DWORD	size;	 /* File size */
	WORD	date;	 /* Last modified date */
	WORD	time;	 /* Last modified time */
	DWORD   dir_clust;  /* Start of directory, needed to reopen dir */
	DWORD   dir_sector; /* Actual sector in which the directory item is located */
    WORD    dir_offset; /* Offset within sector where dir entry is located */
    WORD    lfsize;
    char   *lfname;
	BYTE	attrib;	 /* Attribute */
    char    extension[4];
    
	FileInfo(int namesize)
	{
		fs = NULL;
		cluster = 0;
		size = 0;
		dir_clust = 0;
		dir_sector = 2;
		dir_offset = 0;
		attrib = 0;
		lfsize = namesize;
	    if (namesize)
	        lfname = new char[namesize];
	    else
	        lfname = NULL;
    }
    
    FileInfo(FileInfo &i)
    {
        fs = i.fs;
        cluster = i.cluster;
        size = i.size;
        date = i.date;
        time = i.time;
        dir_clust = i.dir_clust;
        dir_sector = i.dir_sector;
        dir_offset = i.dir_offset;
        lfsize = i.lfsize;
        lfname = new char[lfsize];
        strncpy(lfname, i.lfname, lfsize);
        strncpy(extension, i.extension, 4);
        attrib = i.attrib;
    }
    
	~FileInfo() {
	    if(lfname)
	        delete lfname;
    }

	bool is_directory(void) {
	    return (attrib & AM_DIR);
	}
	void print_info(void) {
		printf("File System: %p\n", fs);
		printf("Cluster    : %d\n", cluster);
		printf("Size       : %d\n", size);
		printf("Date       : %d\n", date);
		printf("Time	   : %d\n", time);
		printf("LFSize     : %d\n", lfsize);
		printf("LFname     : %s\n", lfname);
		printf("Attrib:    : %b\n", attrib);
		printf("Extension  : %s\n", extension);
		printf("Dir Clust  : %d\n", dir_clust);
		printf("Dir sector : %d\n", dir_sector);
		printf("Dir offset : %d\n", dir_offset);
	}
};


class File;
class Directory;
class FileSystem
{
protected:
    Partition *prt;
public:
    FileSystem(Partition *p);
    ~FileSystem();

    static  bool check(Partition *p);        // check if file system is present on this partition
    virtual void    init(void);              // Initialize file system
    virtual FRESULT get_free (DWORD*);       // Get number of free sectors on the file system
    virtual FRESULT sync(void);              // Clean-up cached data

    // functions for reading directories
    virtual Directory *dir_open(FileInfo *); // Opens directory (creates dir object, NULL = root)
    virtual void dir_close(Directory *d);    // Closes (and destructs dir object)
    virtual FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    virtual FRESULT dir_create(FileInfo *);  // Creates a directory as specified by finfo
    
    // functions for reading and writing files
    virtual File   *file_open(FileInfo *, BYTE flags);  // Opens file (creates file object)
    virtual FRESULT file_rename(FileInfo *, char *new_name); // Renames a file
    virtual void    file_close(File *f);                // Closes file (and destructs file object)
    virtual FRESULT file_read(File *f, void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT file_write(File *f, void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT file_seek(File *f, DWORD pos);
    virtual void    file_print_info(File *f) { } // debug
    
    // Modification functions
//    virtual FRESULT rename(void);
    // ...
};

// FATFS is a derivate from this base class. This base class implements the interface

#include "directory.h"
#include "path.h"
#include "file.h"

#endif