#ifndef _NEWFS_H_
#define _NEWFS_H_

#include <stdint.h>
#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

#define NEWFS_MAGIC           0x11451419
#define NEWFS_DEFAULT_PERM    0777   /* 全权限打开 */

/******************************************************************************
* SECTION: newfs.c
*******************************************************************************/
void* 			   newfs_init(struct fuse_conn_info *);
void  			   newfs_destroy(void *);
int   			   newfs_mkdir(const char *, mode_t);
int   			   newfs_getattr(const char *, struct stat *);
int   			   newfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   newfs_mknod(const char *, mode_t, dev_t);
int   			   newfs_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   newfs_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   newfs_access(const char *, int);
int   			   newfs_unlink(const char *);
int   			   newfs_rmdir(const char *);
int   			   newfs_rename(const char *, const char *);
int   			   newfs_utimens(const char *, const struct timespec tv[2]);
int   			   newfs_truncate(const char *, off_t);
			
int   			   newfs_open(const char *, struct fuse_file_info *);
int   			   newfs_opendir(const char *, struct fuse_file_info *);

/******************************************************************************
* SECTION: newfs_utils.c
*******************************************************************************/
#define NEWFS_DEBUG(fmt, ...) do { printf("DEBUG: " fmt, ##__VA_ARGS__); } while(0)
#define assert(expr) do { if (!(expr)) { NEWFS_DEBUG("assert failed: %s, in %s at %s:%d\n", #expr, __func__, __FILE__, __LINE__); fuse_exit(fuse_get_context()->fuse); exit(-1); } } while(0)
#define safe_strcpy(dst, src, n) do { strncpy(dst, src, n); dst[n-1] = '\0'; } while(0)

int                newfs_driver_read(int, void*);
int                newfs_driver_read_range(int, void*, int, int);
int 			   newfs_driver_write(int, void*);
int 			   newfs_driver_write_range(int, void*, int, int);

bool               newfs_test_bit(uint8_t*, int);
void               newfs_set_bit(uint8_t*, int);
void               newfs_clear_bit(uint8_t*, int);

newfs_inode*	   newfs_alloc_inode(newfs_dentry*);
newfs_inode*       newfs_read_inode(int, newfs_dentry*);
int 			   newfs_sync_inode(newfs_inode*);
int 			   newfs_unmap_inode(newfs_inode*);

int   		       newfs_alloc_block(void);
int    		       newfs_free_block(int);

newfs_dentry*      newfs_make_dentry(const char*, FILE_TYPE);

#endif  /* _newfs_H_ */
