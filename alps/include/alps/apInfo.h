
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __APINFO_H__
#define __APINFO_H__

#ident "$Id: apInfo.h 6080 2009-07-29 20:00:30Z albing $"

#include <stdint.h>
#include <time.h>

/*
 * This structure is in a shared file named
 * ALPS_SHARED_APPINFO created by apsched, and
 * in a shared file named ALPS_SHARED_PENDINFO
 * created by apsysd.
 *
 * ALPS_SHARED_AINEW is the name used during file construction by apsched.
 * This file is renamed to ALPS_SHARED_APPINFO once complete.
 * ALPS_SHARED_PENDNEW is the name used during file construction by
 * apsysd.  This file is renamed to ALPS_SHARED_PENDINFO once complete.
 */
#define ALPS_SHARED_APPINFO "appinfo"
#define ALPS_SHARED_AINEW   "appinfoNew"
#define ALPS_SHARED_APPINFO_REFRESH	120

#define ALPS_SHARED_PENDINFO "pendInfo"
#define ALPS_SHARED_PENDNEW "pendInfoNew"

/*
 * bitmask flags which can be set in the appInfo_t flags field; additional
 * bits (ALPS_WHY_xxx) which can be set in the flags field are defined
 * elsewhere.  Those bits define why the placement was denied.
 */
#define ALPS_DISPLAY_APSCHED		0x01000000
#define ALPS_DISPLAY_APSYS		0x02000000

/*
 * Architecture types
 */
typedef enum {
    arch_BAD = 0,
    arch_BW = 1,
    arch_XT,
    arch_Unknown,
    arch_NumArch
} alps_archType_t;

#define NARCHS 2	/* actual # of archs - used to size arrays */
/*
 * Convert an alps_archType_t to a zero-based index
 */
#define archToIdx(ARCH) ({				\
    int idx = (ARCH) - arch_BW;				\
    if (idx < 0 || idx >= NARCHS) {			\
	PANIC("Bad arch %d (idx %d)\n", ARCH, idx);	\
    }							\
    idx;						\
})

/*
 * These should be used as case independent strings
 */
#define ALPS_ARCH_BW	"bw"
#define ALPS_ARCH_X2	"x2"
#define ALPS_ARCH_XT	"xt"
#define ALPS_ARCH_XT3	"xt3"
#define ALPS_ARCH_XT4	"xt4"

/*
 * Details for each application are supplied through these structures.
 * This is designed for shared memory so all references are character
 * offsets from the beginning of the shared memory space.
 */

/*
 * This structure must appear at offset 0 of the shared memory space.
 * The size_t fields at the beginning are used by external programs
 * to check that the structure declaration in the file
 * matches the program's expectations. The IsApinfoCompatible() macro
 * checks this.
 */
typedef struct {
    size_t headerSz;		/* sizeof(appInfoHdr_t) */
    size_t apInfoSz;		/* sizeof(appInfo_t) */
    size_t cmdDetSz;		/* sizeof(cmdDetail_t) */
    size_t plistSz;		/* sizeof(placeList_t) */
    time_t created;		/* time when file contents were written */
    size_t apStart;		/* offset of first appInfo_t entry */
    int    apNum;		/* number of appInfo_t entries */
    
    /* expansion space - unions are needed to freeze alignment */
    union { uint64_t pad; }	   pad0;
    union { uint64_t pad; }	   pad1;
    union { uint64_t pad; }	   pad2;
    union { uint64_t pad; }	   pad3;
    union { uint64_t pad; }	   pad4;
    union { uint64_t pad; }	   pad5;
    union { uint64_t pad; }	   pad6;
    union { uint64_t pad; }	   pad7;

} appInfoHdr_t;

typedef struct {
    int   flags;		/* command-specific flags */
    int   width;		/* number of PEs for this command */
    int   depth;		/* processors per PE */
    int   fixedPerNode;		/* user set per-node PE count */
    int   memory;		/* per PE memory limit in megabytes */
    alps_archType_t arch; 	/* architecture type */
    int   nodeCnt;		/* number of nodes allocated */
    char  cmd[32];		/* a.out name */

    int	  padint0;		/* padint* are deprecated X2 fields */
    int	  padint1;
    int	  padint2;
    int	  padint3;
    int	  padint4;
    
    uint16_t pesPerSeg;		/* -S value */
    uint16_t nodeSegCnt;	/* -sn value */
    uint32_t segBits;		/* -sl 0,1 - each bit is a segment number */

    /* expansion space - unions are needed to freeze alignment */
    union { uint64_t pad; } pad0;
    union { uint64_t pad; } pad1;
    union { uint64_t pad; } pad2;
    union { uint64_t pad; } pad3;
    union { uint64_t pad; } pad4;
    union { uint64_t pad; } pad5;
    union { uint64_t pad; } pad6;

} cmdDetail_t;

/*
 * The first CPU number is the node + first CPU in placeList_t[0]
 */
typedef struct {
    int       cmdIx;		/* cmdInfo_t entry this PE belongs to */
    int       nid;		/* NID this PE is assigned to */
    short     X2PeMap;		/* X2 PE map */
    short     padshort0;
    int       procMask;		/* need all 32 bits for XT emulation mode */
    int       padint0;
    /* expansion space - unions are needed to freeze alignment */
    union { uint64_t pad; }	   pad1;

} placeList_t;

typedef struct {
    uint64_t  apid;
    uint64_t  pagg;
    uint64_t  flags;		/* RECFLAG_* flags */
    time_t    timePlaced;
    time_t    timeSubmitted;
    int64_t   account;
    uint      resId;
    int	      fanout;		/* control tree fanout width */
    size_t    cmdDetail;	/* offset of first cmdDetail_t entry */
    int       numCmds;		/* entries in cmdDetail */
    int	      reqType;		/* most recent request type ALPS_RES_* values */
    uid_t     uid;
    gid_t     gid;
    int       timeLim;
    int       slicePri;		/* time slicing priority */
    /*
     * If RECFLAG_USERNL is set in 'flags', this nid list may not be
     * discarded by recovery for confirm retry.
     */
    size_t    places;		/* offset of first placeList_t entry */
    int	      numPlaces;	/* entries in places */
    int       conTime;		/* connect time for context switched apps */

    time_t timeChkptd;		/* time of latest successful checkpoint */
    
#if XT_GNI
    uint16_t  pTag;		/* 8-bit NTT-unique value used by drivers */
    uint16_t  nttGran;		/* NTT granularity (1-32; 0=no NTT) */
    uint32_t  cookie;		/* per-system unique value used by libs */
#else
    /* expansion space - unions are needed to freeze alignment */
    union { uint64_t pad; }	   pad0;
#endif
    union { uint64_t pad; }	   pad1;
    union { uint64_t pad; }	   pad2;
    union { uint64_t pad; }	   pad3;
    union { uint64_t pad; }	   pad4;
    union { uint64_t pad; }	   pad5;
    union { uint64_t pad; }	   pad6;

} appInfo_t;

/*
 * Check that the file structure matches the format of the using program.
 * Returns true if a match or false if not.
 * P is a pointer to the appInfoHdr_t structure.
 */
#define IsApinfoCompatible(P) \
	((P)->headerSz == sizeof(appInfoHdr_t) && \
	 (P)->apInfoSz == sizeof(appInfo_t) && \
	 (P)->cmdDetSz == sizeof(cmdDetail_t) && \
	 (P)->plistSz == sizeof(placeList_t))

#endif /* __APINFO_H__ */
