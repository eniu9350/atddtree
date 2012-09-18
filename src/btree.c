#include "btree.h"

void bt_putid(unsigned char *dest, uid id)
{
int i = BtId;

	while( i-- )
		dest[i] = (unsigned char)id, id >>= 8;
}

uid bt_getid(unsigned char *src)
{
uid id = 0;
int i;

	for( i = 0; i < BtId; i++ )
		id <<= 8, id |= *src++; 

	return id;
}

// place write, read, or parent lock on requested page_no.

BTERR bt_lockpage(BtDb *bt, uid page_no, BtLock mode)
{
off64_t off = page_no << bt->page_bits;
#ifdef unix
int flag = PROT_READ | ( bt->mode == BT_ro ? 0 : PROT_WRITE );
struct flock lock[1];
#else
uint flags = 0, len;
OVERLAPPED ovl[1];
#endif

	if( mode == BtLockRead || mode == BtLockWrite )
		off +=  sizeof(*bt->page);	// use second segment

	if( mode == BtLockParent )
		off +=  2 * sizeof(*bt->page);	// use third segment

#ifdef unix
	memset (lock, 0, sizeof(lock));

	lock->l_start = off;
	lock->l_type = (mode == BtLockDelete || mode == BtLockWrite || mode == BtLockParent) ? F_WRLCK : F_RDLCK;
	lock->l_len = sizeof(*bt->page);
	lock->l_whence = 0;

	if( fcntl (bt->idx, F_SETLKW, lock) < 0 )
		return bt->err = BTERR_lock;

	return 0;
#else
	memset (ovl, 0, sizeof(ovl));
	ovl->OffsetHigh = (uint)(off >> 32);
	ovl->Offset = (uint)off;
	len = sizeof(*bt->page);

	//	use large offsets to
	//	simulate advisory locking

	ovl->OffsetHigh |= 0x80000000;

	if( mode == BtLockDelete || mode == BtLockWrite || mode == BtLockParent )
		flags |= LOCKFILE_EXCLUSIVE_LOCK;

	if( LockFileEx (bt->idx, flags, 0, len, 0L, ovl) )
		return bt->err = 0;

	return bt->err = BTERR_lock;
#endif 
}

// remove write, read, or parent lock on requested page_no.

BTERR bt_unlockpage(BtDb *bt, uid page_no, BtLock mode)
{
off64_t off = page_no << bt->page_bits;
#ifdef unix
struct flock lock[1];
#else
OVERLAPPED ovl[1];
uint len;
#endif

	if( mode == BtLockRead || mode == BtLockWrite )
		off +=  sizeof(*bt->page);	// use second segment

	if( mode == BtLockParent )
		off +=  2 * sizeof(*bt->page);	// use third segment

#ifdef unix
	memset (lock, 0, sizeof(lock));

	lock->l_start = off;
	lock->l_type = F_UNLCK;
	lock->l_len = sizeof(*bt->page);
	lock->l_whence = 0;

	if( fcntl (bt->idx, F_SETLK, lock) < 0 )
		return bt->err = BTERR_lock;
#else
	memset (ovl, 0, sizeof(ovl));
	ovl->OffsetHigh = (uint)(off >> 32);
	ovl->Offset = (uint)off;
	len = sizeof(*bt->page);

	//	use large offsets to
	//	simulate advisory locking

	ovl->OffsetHigh |= 0x80000000;

	if( !UnlockFileEx (bt->idx, 0, len, 0, ovl) )
		return GetLastError(), bt->err = BTERR_lock;
#endif

	return bt->err = 0;
}

//	close and release memory

void bt_close (BtDb *bt)
{
BtHash *hash;
#ifdef unix
	// release mapped pages

	if( hash = bt->lrufirst )
		do munmap (hash->page, (bt->hashmask+1) << bt->page_bits);
		while(hash = hash->lrunext);

	if ( bt->mem )
		free (bt->mem);
	close (bt->idx);
	free (bt);
#else
	if( hash = bt->lrufirst )
	  do
	  {
		FlushViewOfFile(hash->page, 0);
		UnmapViewOfFile(hash->page);
		CloseHandle(hash->hmap);
	  } while(hash = hash->lrunext);

	if ( bt->mem)
		VirtualFree (bt->mem, 0, MEM_RELEASE);
	FlushFileBuffers(bt->idx);
	CloseHandle(bt->idx);
	GlobalFree (bt);
#endif
}

//  open/create new btree
//	call with file_name, BT_openmode, bits in page size (e.g. 16),
//		size of mapped page cache (e.g. 8192) or zero for no mapping.

BtDb *bt_open (char *name, uint mode, uint bits, uint nodemax)
{
BtLock lockmode = BtLockWrite;
uint lvl, attr, cacheblk;
BtPage alloc;
off64_t size;
uint amt[1];
BtKey key;
BtDb* bt;

#ifndef unix
SYSTEM_INFO sysinfo[1];
#endif

#ifdef unix
	bt = malloc (sizeof(BtDb) + nodemax * sizeof(BtHash));
	memset (bt, 0, sizeof(BtDb));

	switch (mode & 0x7fff)
	{
	case BT_fl:
	case BT_rw:
		bt->idx = open ((char*)name, O_RDWR | O_CREAT, 0666);
		break;

	case BT_ro:
	default:
		bt->idx = open ((char*)name, O_RDONLY);
		lockmode = BtLockRead;
		break;
	}
	if( bt->idx == -1 )
		return free(bt), NULL;
	
	if( nodemax )
		cacheblk = 4096;	// page size for unix
	else
		cacheblk = 0;

#else
	bt = GlobalAlloc (GMEM_FIXED|GMEM_ZEROINIT, sizeof(BtDb) + nodemax * sizeof(BtHash));
	attr = FILE_ATTRIBUTE_NORMAL;
	switch (mode & 0x7fff)
	{
	case BT_fl:
		attr |= FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING;

	case BT_rw:
		bt->idx = CreateFile(name, GENERIC_READ| GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, attr, NULL);
		break;

	case BT_ro:
	default:
		bt->idx = CreateFile(name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, attr, NULL);
		lockmode = BtLockRead;
		break;
	}
	if( bt->idx == INVALID_HANDLE_VALUE )
		return GlobalFree(bt), NULL;

	// normalize cacheblk to multiple of sysinfo->dwAllocationGranularity
	GetSystemInfo(sysinfo);

	if( nodemax )
		cacheblk = sysinfo->dwAllocationGranularity;
	else
		cacheblk = 0;
#endif

	// determine sanity of page size

	if( bits > BT_maxbits )
		bits = BT_maxbits;
	else if( bits < BT_minbits )
		bits = BT_minbits;

	if ( bt_lockpage(bt, ALLOC_page, lockmode) )
		return bt_close (bt), NULL;

#ifdef unix
	*amt = 0;

	// read minimum page size to get root info

	if( size = lseek (bt->idx, 0L, 2) ) {
		alloc = malloc (BT_minpage);
		pread(bt->idx, alloc, BT_minpage, 0);
		bits = alloc->bits;
		free (alloc);
	} else if( mode == BT_ro )
		return bt_close (bt), NULL;
#else
	size = GetFileSize(bt->idx, amt);

	if( size || *amt ) {
		alloc = VirtualAlloc(NULL, BT_minpage, MEM_COMMIT, PAGE_READWRITE);
		if( !ReadFile(bt->idx, (char *)alloc, BT_minpage, amt, NULL) )
			return bt_close (bt), NULL;
		bits = alloc->bits;
		VirtualFree (alloc, 0, MEM_RELEASE);
	} else if( mode == BT_ro )
		return bt_close (bt), NULL;
#endif

	bt->page_size = 1 << bits;
	bt->page_bits = bits;

	bt->nodemax = nodemax;
	bt->mode = mode;

	// setup cache mapping

	if( cacheblk ) {
		if( cacheblk < bt->page_size )
			cacheblk = bt->page_size;

		bt->hashmask = (cacheblk >> bits) - 1;
		bt->mapped_io = 1;
	}

#ifdef unix
	bt->mem = malloc (5 *bt->page_size);
#else
	bt->mem = VirtualAlloc(NULL, 5 * bt->page_size, MEM_COMMIT, PAGE_READWRITE);
#endif
	bt->frame = (BtPage)bt->mem;
	bt->cursor = (BtPage)(bt->mem + bt->page_size);
	bt->page = (BtPage)(bt->mem + 2 * bt->page_size);
	bt->alloc = (BtPage)(bt->mem + 3 * bt->page_size);
	bt->temp = (BtPage)(bt->mem + 4 * bt->page_size);

	if( size || *amt ) {
		if ( bt_unlockpage(bt, ALLOC_page, lockmode) )
			return bt_close (bt), NULL;

		return bt;
	}

	// initializes an empty b-tree with root page and page of leaves

	memset (bt->alloc, 0, bt->page_size);
	bt_putid(bt->alloc->right, MIN_lvl+1);
	bt->alloc->bits = bt->page_bits;

#ifdef unix
	if( write (bt->idx, bt->alloc, bt->page_size) < bt->page_size )
		return bt_close (bt), NULL;
#else
	if( !WriteFile (bt->idx, (char *)bt->alloc, bt->page_size, amt, NULL) )
		return bt_close (bt), NULL;

	if( *amt < bt->page_size )
		return bt_close (bt), NULL;
#endif

	memset (bt->frame, 0, bt->page_size);
	bt->frame->bits = bt->page_bits;

	for( lvl=MIN_lvl; lvl--; ) {
		slotptr(bt->frame, 1)->off = bt->page_size - 3;
		bt_putid(slotptr(bt->frame, 1)->id, lvl ? MIN_lvl - lvl + 1 : 0);		// next(lower) page number
		key = keyptr(bt->frame, 1);
		key->len = 2;			// create stopper key
		key->key[0] = 0xff;
		key->key[1] = 0xff;
		bt->frame->min = bt->page_size - 3;
		bt->frame->lvl = lvl;
		bt->frame->cnt = 1;
		bt->frame->act = 1;
#ifdef unix
		if( write (bt->idx, bt->frame, bt->page_size) < bt->page_size )
			return bt_close (bt), NULL;
#else
		if( !WriteFile (bt->idx, (char *)bt->frame, bt->page_size, amt, NULL) )
			return bt_close (bt), NULL;

		if( *amt < bt->page_size )
			return bt_close (bt), NULL;
#endif
	}

	// create empty page area by writing last page of first
	// cache area (other pages are zeroed by O/S)

	if( bt->mapped_io && bt->hashmask > 2 ) {
		memset(bt->frame, 0, bt->page_size);

#ifdef unix
		pwrite(bt->idx, bt->frame, bt->page_size, bt->hashmask << bt->page_bits);
#else
		SetFilePointer (bt->idx, bt->hashmask << bt->page_bits, NULL, FILE_BEGIN);
		if( !WriteFile (bt->idx, (char *)bt->frame, bt->page_size, amt, NULL) )
			return bt_close (bt), NULL;
		if( *amt < bt->page_size )
			return bt_close (bt), NULL;
#endif
	}

	if( bt_unlockpage(bt, ALLOC_page, lockmode) )
		return bt_close (bt), NULL;

	return bt;
}

//  compare two keys, returning > 0, = 0, or < 0
//  as the comparison value

int keycmp (BtKey key1, unsigned char *key2, uint len2)
{
uint len1 = key1->len;
int ans;

	if( ans = memcmp (key1->key, key2, len1 > len2 ? len2 : len1) )
		return ans;

	if( len1 > len2 )
		return 1;
	if( len1 < len2 )
		return -1;

	return 0;
}

//  Update current page of btree by writing file contents
//	or flushing mapped area to disk.

BTERR bt_update (BtDb *bt, BtPage page, uid page_no)
{
off64_t off = page_no << bt->page_bits;

#ifdef unix
    if ( !bt->mapped_io )
	 if ( pwrite(bt->idx, page, bt->page_size, off) != bt->page_size )
		 return bt->err = BTERR_wrt;
#else
uint amt[1];
	if ( !bt->mapped_io )
	{
		SetFilePointer (bt->idx, (long)off, (long*)(&off)+1, FILE_BEGIN);
		if( !WriteFile (bt->idx, (char *)page, bt->page_size, amt, NULL) )
			return GetLastError(), bt->err = BTERR_wrt;

		if( *amt < bt->page_size )
			return GetLastError(), bt->err = BTERR_wrt;
	} 
	else if ( bt->mode == BT_fl ) {
			FlushViewOfFile(page, bt->page_size);
			FlushFileBuffers(bt->idx);
	}
#endif
	return 0;
}

// find page in cache 

BtHash *bt_findhash(BtDb *bt, uid page_no)
{
BtHash *hash;
uint idx;

	// compute cache block first page and hash idx 

	page_no &= ~bt->hashmask;
	idx = (uint)(page_no * BT_hashprime % BT_hashsize);

	if( bt->cache[idx] ) 
		hash = bt->nodes + bt->cache[idx];
	else
		return NULL;

	do if( hash->page_no == page_no )
		 break;
	while(hash = hash->hashnext );

	return hash;
}

// add page cache entry to hash index

void bt_linkhash(BtDb *bt, BtHash *node, uid page_no)
{
uint idx = (uint)((page_no & ~bt->hashmask) * BT_hashprime % BT_hashsize);
BtHash *hash;

	if( bt->cache[idx] ) {
		node->hashnext = hash = bt->nodes + bt->cache[idx];
		hash->hashprev = node;
	}

	node->hashprev = NULL;
	bt->cache[idx] = (ushort)(node - bt->nodes);
}

// remove cache entry from hash table

void bt_unlinkhash(BtDb *bt, BtHash *node)
{
uint idx = (uint)((node->page_no & ~bt->hashmask) * BT_hashprime % BT_hashsize);
BtHash *hash;

	// unlink node
	if( hash = node->hashprev )
		hash->hashnext = node->hashnext;
	else if( hash = node->hashnext )
		bt->cache[idx] = (ushort)(hash - bt->nodes);
	else
		bt->cache[idx] = 0;

	if( hash = node->hashnext )
		hash->hashprev = node->hashprev;
}

// add cache page to lru chain and map pages

BtPage bt_linklru(BtDb *bt, BtHash *hash, uid page_no)
{
int flag;
off64_t off = (page_no & ~bt->hashmask) << bt->page_bits;
off64_t limit = off + ((bt->hashmask+1) << bt->page_bits);
BtHash *node;

	memset(hash, 0, sizeof(BtHash));
	hash->page_no = (page_no & ~bt->hashmask);
	bt_linkhash(bt, hash, page_no);

	if( node = hash->lrunext = bt->lrufirst )
		node->lruprev = hash;
	else
		bt->lrulast = hash;

	bt->lrufirst = hash;

#ifdef unix
	flag = PROT_READ | ( bt->mode == BT_ro ? 0 : PROT_WRITE );
	hash->page = (BtPage)mmap (0, (bt->hashmask+1) << bt->page_bits, flag, MAP_SHARED, bt->idx, off);
	if( (int)hash->page == -1 )
		return bt->err = BTERR_map, (BtPage)NULL;

#else
	flag = ( bt->mode == BT_ro ? PAGE_READONLY : PAGE_READWRITE );
	hash->hmap = CreateFileMapping(bt->idx, NULL, flag,	(DWORD)(limit >> 32), (DWORD)limit, NULL);
	if( !hash->hmap )
		return bt->err = BTERR_map, NULL;

	flag = ( bt->mode == BT_ro ? FILE_MAP_READ : FILE_MAP_WRITE );
	hash->page = MapViewOfFile(hash->hmap, flag, (DWORD)(off >> 32), (DWORD)off, (bt->hashmask+1) << bt->page_bits);
	if( !hash->page )
		return bt->err = BTERR_map, NULL;
#endif

	return (BtPage)((char*)hash->page + ((uint)(page_no & bt->hashmask) << bt->page_bits));
}

//	find or place requested page in page-cache
//	return memory address where page is located.

BtPage bt_hashpage(BtDb *bt, uid page_no)
{
BtHash *hash, *node, *next;
BtPage page;

	// find page in cache and move to top of lru list  

	if( hash = bt_findhash(bt, page_no) ) {
		page = (BtPage)((char*)hash->page + ((uint)(page_no & bt->hashmask) << bt->page_bits));
		// swap node in lru list
		if( node = hash->lruprev ) {
			if( next = node->lrunext = hash->lrunext )
				next->lruprev = node;
			else
				bt->lrulast = node;

			if( next = hash->lrunext = bt->lrufirst )
				next->lruprev = hash;
			else
				return bt->err = BTERR_hash, (BtPage)NULL;

			hash->lruprev = NULL;
			bt->lrufirst = hash;
		}
		return page;
	}

	// map pages and add to cache entry

	if( bt->nodecnt < bt->nodemax ) {
		hash = bt->nodes + ++bt->nodecnt;
		return bt_linklru(bt, hash, page_no);
	}

	// hash table is already full, replace last lru entry from the cache

	if( hash = bt->lrulast ) {
		// unlink from lru list
		if( node = bt->lrulast = hash->lruprev )
			node->lrunext = NULL;
		else
			return bt->err = BTERR_hash, (BtPage)NULL;

#ifdef unix
		munmap (hash->page, (bt->hashmask+1) << bt->page_bits);
#else
		FlushViewOfFile(hash->page, 0);
		UnmapViewOfFile(hash->page);
		CloseHandle(hash->hmap);
#endif
		// unlink from hash table

		bt_unlinkhash(bt, hash);

		// map and add to cache

		return bt_linklru(bt, hash, page_no);
	}

	return bt->err = BTERR_hash, (BtPage)NULL;
}

//  map a btree page onto current page

BTERR bt_mappage (BtDb *bt, BtPage *page, uid page_no)
{
off64_t off = page_no << bt->page_bits;
#ifndef unix
int amt[1];
#endif
	
	if( bt->mapped_io ) {
		bt->err = 0;
		*page = bt_hashpage(bt, page_no);
		return bt->err;
	}
#ifdef unix
	if ( pread(bt->idx, *page, bt->page_size, off) < bt->page_size )
		return bt->err = BTERR_map;
#else
	SetFilePointer (bt->idx, (long)off, (long*)(&off)+1, FILE_BEGIN);

	if( !ReadFile(bt->idx, *page, bt->page_size, amt, NULL) )
		return bt->err = BTERR_map;

	if( *amt <  bt->page_size )
		return bt->err = BTERR_map;
#endif
	return 0;
}

//	deallocate a deleted page 
//	place on free chain out of allocator page

BTERR bt_freepage(BtDb *bt, uid page_no)
{
	//  obtain delete lock on deleted node

	if( bt_lockpage(bt, page_no, BtLockDelete) )
		return bt->err;

	//  obtain write lock on deleted node

	if( bt_lockpage(bt, page_no, BtLockWrite) )
		return bt->err;

	if( bt_mappage (bt, &bt->temp, page_no) )
		return bt->err;

	//	lock allocation page

	if ( bt_lockpage(bt, ALLOC_page, BtLockWrite) )
		return bt->err;

	if( bt_mappage (bt, &bt->alloc, ALLOC_page) )
		return bt->err;

	//	store chain in second right
	bt_putid(bt->temp->right, bt_getid(bt->alloc[1].right));
	bt_putid(bt->alloc[1].right, page_no);

	if( bt_update(bt, bt->alloc, ALLOC_page) )
		return bt->err;
	if( bt_update(bt, bt->temp, page_no) )
		return bt->err;

	// unlock page zero 

	if( bt_unlockpage(bt, ALLOC_page, BtLockWrite) )
		return bt->err;

	//  remove write lock on deleted node

	if( bt_unlockpage(bt, page_no, BtLockWrite) )
		return bt->err;

	//  remove delete lock on deleted node

	if( bt_unlockpage(bt, page_no, BtLockDelete) )
		return bt->err;

	return 0;
}

//	allocate a new page and write page into it

uid bt_newpage(BtDb *bt, BtPage page)
{
uid new_page;
char *pmap;
int reuse;

	// lock page zero

	if ( bt_lockpage(bt, ALLOC_page, BtLockWrite) )
		return 0;

	if( bt_mappage (bt, &bt->alloc, ALLOC_page) )
		return 0;

	// use empty chain first
	// else allocate empty page

	if( new_page = bt_getid(bt->alloc[1].right) ) {
		if( bt_mappage (bt, &bt->temp, new_page) )
			return 0;	// don't unlock on error
		bt_putid(bt->alloc[1].right, bt_getid(bt->temp->right));
		reuse = 1;
	} else {
		new_page = bt_getid(bt->alloc->right);
		bt_putid(bt->alloc->right, new_page+1);
		reuse = 0;
	}

	if( bt_update(bt, bt->alloc, ALLOC_page) )
		return 0;	// don't unlock on error

	// unlock page zero 

	if ( bt_unlockpage(bt, ALLOC_page, BtLockWrite) )
		return 0;

	if( !bt->mapped_io ) {
		if( bt_update(bt, page, new_page) )
			return 0;	//don't unlock on error

		return new_page;
	}

#ifdef unix
	if ( pwrite(bt->idx, page, bt->page_size, new_page << bt->page_bits) < bt->page_size )
		return bt->err = BTERR_wrt, 0;

	// if writing first page of hash block, zero last page in the block

	if ( !reuse && bt->hashmask > 0 && (new_page & bt->hashmask) == 0 )
	{
		// use temp buffer to write zeros
		memset(bt->temp, 0, bt->page_size);
		if ( pwrite(bt->idx,bt->temp, bt->page_size, (new_page | bt->hashmask) << bt->page_bits) < bt->page_size )
			return bt->err = BTERR_wrt, 0;
	}
#else
	//	bring new page into page-cache and copy page.
	//	this will extend the file into the new pages.

	if( !(pmap = (char*)bt_hashpage(bt, new_page & ~bt->hashmask)) )
		return 0;

	memcpy(pmap+((new_page & bt->hashmask) << bt->page_bits), page, bt->page_size);
#endif

	return new_page;
}

//  find slot in page for given key at a given level

int bt_findslot (BtDb *bt, unsigned char *key, uint len)
{
uint diff, higher = bt->page->cnt, low = 1, slot;
uint good = 0;

	//	make stopper key an infinite fence value

	if( bt_getid (bt->page->right) )
		higher++;
	else
		good++;

	//	low is the next candidate, higher is already
	//	tested as .ge. the given key, loop ends when they meet

	while( diff = higher - low ) {
		slot = low + ( diff >> 1 );
		if( keycmp (keyptr(bt->page, slot), key, len) < 0 )
			low = slot + 1;
		else
			higher = slot, good++;
	}

	//	return zero if key is on right link page

 	return good ? higher : 0;
}

//  find and load page at given level for given key
//	leave page rd or wr locked as requested

int bt_loadpage (BtDb *bt, unsigned char *key, uint len, uint lvl, uint lock)
{
uid page_no = ROOT_page, prevpage = 0;
uint drill = 0xff, slot;
uint mode, prevmode;

  //  start at root of btree and drill down

  do {
	// determine lock mode of drill level
	mode = (lock == BtLockWrite) && (drill == lvl) ? BtLockWrite : BtLockRead; 

	bt->page_no = page_no;

 	// obtain access lock using lock chaining

	if( page_no > ROOT_page )
	  if( bt_lockpage(bt, bt->page_no, BtLockAccess) )
		return 0;									

	if( prevpage )
	  if( bt_unlockpage(bt, prevpage, prevmode) )
		return 0;

 	// obtain read lock using lock chaining

	if( bt_lockpage(bt, bt->page_no, mode) )
		return 0;									

	if( page_no > ROOT_page )
	  if( bt_unlockpage(bt, bt->page_no, BtLockAccess) )
		return 0;									

	//	map/obtain page contents

	if( bt_mappage (bt, &bt->page, page_no) )
		return 0;

	// re-read and re-lock root after determining actual level of root

	if( bt->page->lvl != drill) {
		if ( bt->page_no != ROOT_page )
			return bt->err = BTERR_struct, 0;
			
		drill = bt->page->lvl;

		if( lock == BtLockWrite && drill == lvl )
		  if( bt_unlockpage(bt, page_no, mode) )
			return 0;
		  else
			continue;
	}

	//  find key on page at this level
	//  and descend to requested level

	if( !bt->page->kill && (slot = bt_findslot (bt, key, len)) ) {
	  if( drill == lvl )
		return slot;

	  while( slotptr(bt->page, slot)->dead )
		if( slot++ < bt->page->cnt )
			continue;
		else {
			page_no = bt_getid(bt->page->right);
			goto slideright;
		}

	  page_no = bt_getid(slotptr(bt->page, slot)->id);
	  drill--;
	}

	//  or slide right into next page
	//  (slide left from deleted page)

	else
		page_no = bt_getid(bt->page->right);

	//  continue down / right using overlapping locks
	//  to protect pages being killed or split.

slideright:
	prevpage = bt->page_no;
	prevmode = mode;
  } while( page_no );

  // return error on end of right chain

  bt->err = BTERR_struct;
  return 0;	// return error
}

//  find and delete key on page by marking delete flag bit
//  when page becomes empty, delete it

BTERR bt_deletekey (BtDb *bt, unsigned char *key, uint len, uint lvl)
{
unsigned char lowerkey[256], higherkey[256];
uint slot, tod, dirty = 0;
uid page_no, right;
BtKey ptr;

	if( slot = bt_loadpage (bt, key, len, lvl, BtLockWrite) )
		ptr = keyptr(bt->page, slot);
	else
		return bt->err;

	// if key is found delete it, otherwise ignore request

	if( !keycmp (ptr, key, len) )
		if( slotptr(bt->page, slot)->dead == 0 )
			dirty = slotptr(bt->page,slot)->dead = 1, bt->page->act--;

	// return if page is not empty, or it has no right sibling

	right = bt_getid(bt->page->right);
	page_no = bt->page_no;

	if( !right || bt->page->act )
	  if ( dirty && bt_update(bt, bt->page, page_no) )
		return bt->err;
	  else
		return bt_unlockpage(bt, page_no, BtLockWrite);

	// obtain Parent lock over write lock

	if( bt_lockpage(bt, page_no, BtLockParent) )
		return bt->err;

	// cache copy of key to delete

	ptr = keyptr(bt->page, bt->page->cnt);
	memcpy(lowerkey, ptr, ptr->len + 1);

	// lock and map right page

	if ( bt_lockpage(bt, right, BtLockWrite) )
		return bt->err;

	if( bt_mappage (bt, &bt->temp, right) )
		return bt->err;

	// pull contents of next page into current empty page 
	memcpy (bt->page, bt->temp, bt->page_size);

	//	cache copy of key to update
	ptr = keyptr(bt->temp, bt->temp->cnt);
	memcpy(higherkey, ptr, ptr->len + 1);

	//  Mark right page as deleted and point it to left page
	//	until we can post updates at higher level.

	bt_putid(bt->temp->right, page_no);
	bt->temp->kill = 1;
	bt->temp->cnt = 0;

	if ( bt_update(bt, bt->page, page_no) )
		return bt->err;

	if ( bt_update(bt, bt->temp, right) )
		return bt->err;

	if( bt_unlockpage(bt, right, BtLockWrite) )
		return bt->err;
	if( bt_unlockpage(bt, page_no, BtLockWrite) )
		return bt->err;

	//  delete old lower key to consolidated node

	if( bt_deletekey (bt, lowerkey + 1, *lowerkey, lvl + 1) )
		return bt->err;

	//  redirect higher key directly to consolidated node

	tod = (uint)time(NULL);

	if( bt_insertkey (bt, higherkey+1, *higherkey, lvl + 1, page_no, tod) )
		return bt->err;

	//	obtain write lock and
	//	add right block to free chain

	if( bt_freepage (bt, right) )
		return bt->err;

	// 	remove ParentModify lock

	if( bt_unlockpage(bt, page_no, BtLockParent) )
		return bt->err;
	
	return 0;
}

//	find key in leaf level and return row-id

uint bt_findkey (BtDb *bt, unsigned char *key, uint len)
{
uint  slot;
BtKey ptr;

	if( slot = bt_loadpage (bt, key, len, 0, BtLockRead) )
		ptr = keyptr(bt->page, slot);
	else
		return 0;

	// if key exists, return id

	if( slotptr(bt->page,slot)->dead || memcmp (ptr->key, key, len) )
	  slot = 0;

	memcpy(bt->cursor, bt->page, bt->page_size);
	if ( bt_unlockpage(bt, bt->page_no, BtLockRead) )
		return 0;

	return slot;
}

void bt_cleanpage(BtDb *bt)
{
uint nxt = bt->page_size;
BtPage page = bt->page;
uint cnt = 0, idx = 0;
uint max = page->cnt;
BtKey key;

	memcpy (bt->frame, page, bt->page_size);

	// skip page info and set rest of page to zero
	memset (page+1, 0, bt->page_size - sizeof(*page));
	page->act = 0;

	// try cleaning up page first

	while( cnt++ < max ) {
		// always leave fence key in list
		if( cnt < max && slotptr(bt->frame,cnt)->dead )
			continue;

		// copy key
		key = keyptr(bt->frame, cnt);
		nxt -= key->len + 1;
		memcpy ((unsigned char *)page + nxt, key, key->len + 1);

		// copy slot
		memcpy(slotptr(page, ++idx)->id, slotptr(bt->frame, cnt)->id, BtId);
		if( !(slotptr(page, idx)->dead = slotptr(bt->frame, cnt)->dead) )
			page->act++;
		slotptr(page, idx)->tod = slotptr(bt->frame, cnt)->tod;
		slotptr(page, idx)->off = nxt;
	}
	page->min = nxt;
	page->cnt = idx;
}

// split the root and raise the height of the btree

BTERR bt_splitroot(BtDb *bt,  unsigned char *newkey, unsigned char *oldkey, uid page_no2)
{
uint nxt = bt->page_size;
BtPage root = bt->page;
uid new_page;


printf("split root!\n");

	//  Obtain an empty page to use, and copy the current
	//  root contents into it

	if( !(new_page = bt_newpage(bt, root)) )
		return bt->err;

	// preserve the page info at the bottom
	// and set rest to zero

	memset(root+1, 0, bt->page_size - sizeof(*root));

	// insert first key on newroot page

	nxt -= *newkey + 1;
	memcpy ((unsigned char *)root + nxt, newkey, *newkey + 1);
	bt_putid(slotptr(root, 1)->id, new_page);
	slotptr(root, 1)->off = nxt;
	
	// insert second key on newroot page
	// and increase the root height

	nxt -= *oldkey + 1;
	memcpy ((unsigned char *)root + nxt, oldkey, *oldkey + 1);
	bt_putid(slotptr(root, 2)->id, page_no2);
	slotptr(root, 2)->off = nxt;

	bt_putid(root->right, 0);
	root->min = nxt;		// reset lowest used offset and key count
	root->cnt = 2;
	root->act = 2;
	root->lvl++;

	// update and release root (bt->page)

	if( bt_update(bt, root, bt->page_no) )
		return bt->err;

	return bt_unlockpage(bt, bt->page_no, BtLockWrite);
}

//  split already locked full node
//	return unlocked.

BTERR bt_splitpage (BtDb *bt, uint len)
{
uint cnt = 0, idx = 0, max, nxt = bt->page_size;
unsigned char oldkey[256], lowerkey[256];
uid page_no = bt->page_no, right;
BtPage page = bt->page;
uint lvl = page->lvl;
uid new_page;
BtKey key;
uint tod;

	// perform cleanup

	bt_cleanpage(bt);

	// return if enough space now

	if( page->min >= (page->cnt + 1) * sizeof(BtSlot) + sizeof(*page) + len + 1)
	{
		if ( bt_update(bt, page, page_no) )
			return bt->err;

		return bt_unlockpage(bt, page_no, BtLockWrite);
	}

	//  split higher half of keys to bt->frame
	//	the last key (fence key) might be dead

	tod = (uint)time(NULL);

	memset (bt->frame, 0, bt->page_size);
	max = (int)page->cnt;
	cnt = max / 2;
	idx = 0;

	while( cnt++ < max ) {
		key = keyptr(page, cnt);
		nxt -= key->len + 1;
		memcpy ((unsigned char *)bt->frame + nxt, key, key->len + 1);
		memcpy(slotptr(bt->frame,++idx)->id, slotptr(page,cnt)->id, BtId);
		if( !(slotptr(bt->frame, idx)->dead = slotptr(page, cnt)->dead) )
			bt->frame->act++;
		slotptr(bt->frame, idx)->tod = slotptr(page, cnt)->tod;
		slotptr(bt->frame, idx)->off = nxt;
	}

	// remember existing fence key for new page to the right

	memcpy (oldkey, key, key->len + 1);

	bt->frame->bits = bt->page_bits;
	bt->frame->min = nxt;
	bt->frame->cnt = idx;
	bt->frame->lvl = lvl;

	// link right node

	if( page_no > ROOT_page ) {
		right = bt_getid (page->right);
		bt_putid(bt->frame->right, right);
	}

	//	get new free page and write frame to it.

	if( !(new_page = bt_newpage(bt, bt->frame)) )
		return bt->err;

	//	update lower keys to continue in old page

	memcpy (bt->frame, page, bt->page_size);
	memset (page+1, 0, bt->page_size - sizeof(*page));
	nxt = bt->page_size;
	page->act = 0;
	cnt = 0;
	idx = 0;

	//  assemble page of smaller keys
	//	(they're all active keys)

	while( cnt++ < max / 2 ) {
		key = keyptr(bt->frame, cnt);
		nxt -= key->len + 1;
		memcpy ((unsigned char *)page + nxt, key, key->len + 1);
		memcpy(slotptr(page,++idx)->id, slotptr(bt->frame,cnt)->id, BtId);
		slotptr(page, idx)->tod = slotptr(bt->frame, cnt)->tod;
		slotptr(page, idx)->off = nxt;
		page->act++;
	}

	// remember fence key for old page

	memcpy(lowerkey, key, key->len + 1);
	bt_putid(page->right, new_page);
	page->min = nxt;
	page->cnt = idx;

	// if current page is the root page, split it

	if( page_no == ROOT_page )
		return bt_splitroot (bt, lowerkey, oldkey, new_page);

	// update left (containing) node

	if( bt_update(bt, page, page_no) )
		return bt->err;

	// obtain Parent/Write locks
	// for left and right node pages

	if( bt_lockpage (bt, new_page, BtLockParent) )
		return bt->err;

	if( bt_lockpage (bt, page_no, BtLockParent) )
		return bt->err;

	//  release wr lock on left page

	if( bt_unlockpage (bt, page_no, BtLockWrite) )
		return bt->err;

	// insert new fence for reformulated left block

	if( bt_insertkey (bt, lowerkey+1, *lowerkey, lvl + 1, page_no, tod) )
		return bt->err;

	// fix old fence for newly allocated right block page

	if( bt_insertkey (bt, oldkey+1, *oldkey, lvl + 1, new_page, tod) )
		return bt->err;

	// release Parent & Write locks

	if( bt_unlockpage (bt, new_page, BtLockParent) )
		return bt->err;

	if( bt_unlockpage (bt, page_no, BtLockParent) )
		return bt->err;

	return 0;
}

//  Insert new key into the btree at requested level.
//  Level zero pages are leaf pages and are unlocked at exit.
//	Interior nodes remain locked.

BTERR bt_insertkey (BtDb *bt, unsigned char *key, uint len, uint lvl, uid id, uint tod)
{
uint slot, idx;
BtPage page;
BtKey ptr;

  while( 1 ) {
	if( slot = bt_loadpage (bt, key, len, lvl, BtLockWrite) )
		ptr = keyptr(bt->page, slot);
	else
	{
		if ( !bt->err )
			bt->err = BTERR_ovflw;
		return bt->err;
	}

	// if key already exists, update id and return

	page = bt->page;

	if( !keycmp (ptr, key, len) ) {
		slotptr(page, slot)->dead = 0;
		slotptr(page, slot)->tod = tod;
		bt_putid(slotptr(page,slot)->id, id);
		if ( bt_update(bt, bt->page, bt->page_no) )
			return bt->err;
		return bt_unlockpage(bt, bt->page_no, BtLockWrite);
	}

	// check if page has enough space

	if( page->min >= (page->cnt + 1) * sizeof(BtSlot) + sizeof(*page) + len + 1)
		break;

	if( bt_splitpage (bt, len) )
		return bt->err;
  }

  // calculate next available slot and copy key into page

  page->min -= len + 1; // reset lowest used offset
  ((unsigned char *)page)[page->min] = len;
  memcpy ((unsigned char *)page + page->min +1, key, len );

  for( idx = slot; idx < page->cnt; idx++ )
	if( slotptr(page, idx)->dead )
		break;

  // now insert key into array before slot
  // preserving the fence slot

  if( idx == page->cnt )
	idx++, page->cnt++;

  page->act++;

  while( idx > slot )
	*slotptr(page, idx) = *slotptr(page, idx -1), idx--;

  bt_putid(slotptr(page,slot)->id, id);
  slotptr(page, slot)->off = page->min;
  slotptr(page, slot)->tod = tod;
  slotptr(page, slot)->dead = 0;

  if ( bt_update(bt, bt->page, bt->page_no) )
	  return bt->err;

  return bt_unlockpage(bt, bt->page_no, BtLockWrite);
}

//  cache page of keys into cursor and return starting slot for given key

uint bt_startkey (BtDb *bt, unsigned char *key, uint len)
{
uint slot;

	// cache page for retrieval
	if( slot = bt_loadpage (bt, key, len, 0, BtLockRead) )
		memcpy (bt->cursor, bt->page, bt->page_size);
	bt->cursor_page = bt->page_no;
	if ( bt_unlockpage(bt, bt->page_no, BtLockRead) )
		return 0;

	return slot;
}

//  return next slot for cursor page
//  or slide cursor right into next page

uint bt_nextkey (BtDb *bt, uint slot)
{
off64_t right;

  do {
	right = bt_getid(bt->cursor->right);
	while( slot++ < bt->cursor->cnt )
	  if( slotptr(bt->cursor,slot)->dead )
		continue;
	  else if( right || (slot < bt->cursor->cnt))
		return slot;
	  else
		break;

	if( !right )
		break;

	bt->cursor_page = right;

    if( bt_lockpage(bt, right,BtLockRead) )
		return 0;

	if( bt_mappage (bt, &bt->page, right) )
		break;

	memcpy (bt->cursor, bt->page, bt->page_size);
	if ( bt_unlockpage(bt, right, BtLockRead) )
		return 0;

	slot = 0;
  } while( 1 );

  return bt->err = 0;
}

BtKey bt_key(BtDb *bt, uint slot)
{
	return keyptr(bt->cursor, slot);
}

uid bt_uid(BtDb *bt, uint slot)
{
	return bt_getid(slotptr(bt->cursor,slot)->id);
}

uint bt_tod(BtDb *bt, uint slot)
{
	return slotptr(bt->cursor,slot)->tod;
}


#ifdef STANDALONE
//  standalone program to index file of keys
//  then list them onto std-out

int main (int argc, char **argv)
{
int dead, ch, cnt = 0, bits = 16;
uint slot, line = 0, off = 0;
unsigned char key[256];
uint done, start;
uint scan = 0;
uint len = 0;
uint map = 0;
BtKey ptr;
BtDb *bt;
FILE *in;

	if( argc < 4 ) {
		printf ("Usage: %s idx_file src_file Read/Write/Scan/Delete [page_bits pool_size start_line_number]", argv[0]);
		exit(0);
	}

	start = (uint)time(NULL);

	if( argc > 4 )
		bits = atoi(argv[4]);

	if( argc > 5 )
		map = atoi(argv[5]);

	if( argc > 6 )
		off = atoi(argv[6]);

	bt = bt_open ((argv[1]), BT_rw, bits, map);

	if( !bt ) {
		printf("Index Open Error %s\n", argv[1]);
		exit (1);
	}

	switch(argv[3][0]| 0x20)
	{
	case 'w':
		printf("started indexing for %s at: %ld\n", argv[2], start);
		if( argc > 2 && (in = fopen (argv[2], "rb")) )
		  while( ch = getc(in), ch != EOF )
			if( ch == '\n' )
			{
			  if( off )
		  		sprintf((char *)key+len, "%.9d", line + off), len += 9;

			  if( bt_insertkey (bt, key, len, 0, ++line, start) )
				printf("Error %d Line: %d\n", bt->err, line), exit(0);
			  len = 0;
			}
			else if( len < 245 )
				key[len++] = ch;
		printf("finished adding keys, %d \n", line);
		break;

	case 'd':
		printf("started deleting keys for %s at: %ld\n", argv[2], start);
		if( argc > 2 && (in = fopen (argv[2], "rb")) )
		  while( ch = getc(in), ch != EOF )
			if( ch == '\n' )
			{
			  if( off )
		  		sprintf((char *)key+len, "%.9d", line + off), len += 9;
			  line++;
			  if( bt_deletekey (bt, key, len, 0) )
				printf("Error %d Line: %d\n", bt->err, line), exit(0);
			  len = 0;
			}
			else if( len < 245 )
				key[len++] = ch;
		printf("finished deleting keys, %d \n", line);
		break;

	case 's':
		scan++;
		break;

	}

	dead = cnt = 0;
	len = key[0] = 0;

	printf("started reading, %d \n", line);

	if( slot = bt_startkey (bt, key, len) )
	  slot--;
	else
	  printf("Error %d in StartKey. Line: %d\n", bt->err, line), exit(0);

	while( slot = bt_nextkey (bt, slot) )
	  if( cnt++, scan ) {
			ptr = bt_key(bt, slot);
			fwrite (ptr->key, ptr->len, 1, stdout);
			fputc ('\n', stdout);
	  }

	done = (uint)time(NULL);
	printf("Done at : %ld \n Time to complete: %ld seconds\n", done, done - start);
	printf(" Total keys read %d \n", cnt);
}

#endif	//STANDALONE

