#include "types.h"
#include <stdint.h>
#define _XOPEN_SOURCE 700

#include "newfs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options newfs_options;			 /* 全局选项 */
struct newfs_super super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = newfs_init,						 /* mount文件系统 */		
	.destroy = newfs_destroy,				 /* umount文件系统 */
	.mkdir = newfs_mkdir,					 /* 建目录，mkdir */
	.getattr = newfs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = newfs_readdir,				 /* 填充dentrys */
	.mknod = newfs_mknod,					 /* 创建文件，touch相关 */
	.write = newfs_write,								  	 /* 写入文件 */
	.read = newfs_read,								  	 /* 读文件 */
	.utimens = newfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = newfs_truncate,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* newfs_init(struct fuse_conn_info * conn_info)
{
	int fd = ddriver_open((char*)newfs_options.device);
	assert(fd > 0);

	int sz_io=0, sz_disk=0, io_per_block=2;
	assert(ddriver_ioctl(fd, IOC_REQ_DEVICE_IO_SZ, &sz_io) == 0);
    assert(ddriver_ioctl(fd, IOC_REQ_DEVICE_SIZE, &sz_disk) == 0);

	super.fd = fd; super.sz_io = sz_io; super.sz_disk = sz_disk;
	super.io_per_block = io_per_block; super.sz_block = sz_io * io_per_block;
	assert(newfs_driver_read_range(0, &super, 0, sizeof(super)) == 0);

	super.fd = fd; super.sz_io = sz_io; super.sz_disk = sz_disk;
	super.io_per_block = io_per_block; super.sz_block = sz_io * io_per_block;

	assert(super.imap = malloc(super.sz_block));
	assert(super.dmap = malloc(super.sz_block));
	super.is_mounted = true;

	newfs_dentry *root_dentry = newfs_make_dentry("/", DIR);
	if(super.magic != NEWFS_MAGIC) {
		// build
		NEWFS_DEBUG("building newfs\n");
		super.magic = NEWFS_MAGIC;

		super.sz_block = super.sz_io * super.io_per_block;
		super.tot_block = super.sz_disk / super.sz_block;

		super.imap_off = 1;
		super.imap_blks = 1;
		super.dmap_off = super.imap_off + super.imap_blks;
		super.dmap_blks = 1;
		
		super.ino_off = super.dmap_off + super.dmap_blks;
		super.ino_per_block = super.sz_block / sizeof(struct newfs_inode_d);
		super.ino_blks = (super.tot_block + super.ino_per_block - 1) / super.ino_per_block;
		super.den_per_block = super.sz_block / sizeof(struct newfs_dentry_d);
		super.ino_num = super.ino_per_block * super.ino_blks;


		super.data_off = super.ino_off + super.ino_blks;
		super.data_blks = super.tot_block - super.data_off;

		memset(super.imap, 0, super.sz_block);
		memset(super.dmap, 0, super.sz_block);

		// allocate root
		assert(super.root = newfs_alloc_inode(root_dentry));
		super.root->size = 0; super.root->link = 1; super.root->ftype = DIR;
		super.root->dentry = root_dentry;

		super.root_ino = super.root->ino;
		assert(newfs_sync_inode(super.root) == 0);
	} else {
		// load
		NEWFS_DEBUG("loading existing newfs\n");
		assert(newfs_driver_read(super.imap_off, super.imap) == 0);
		assert(newfs_driver_read(super.dmap_off, super.dmap) == 0);

		assert(super.root = newfs_read_inode(super.root_ino, root_dentry));
	}

	NEWFS_DEBUG("imap_blks %d, dmap_blks %d, ino_blks %d\n", super.imap_blks, super.dmap_blks, super.ino_blks);
	NEWFS_DEBUG("root_ino %d\n", super.root_ino);
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p)
{
	assert(super.is_mounted);

	assert(newfs_sync_inode(super.root) == 0);
	assert(newfs_unmap_inode(super.root) == 0); super.root = NULL;

	assert(newfs_driver_write(super.imap_off, super.imap) == 0);
	free(super.imap); super.imap = NULL;
	assert(newfs_driver_write(super.dmap_off, super.dmap) == 0);
	free(super.dmap); super.dmap = NULL;

	super.is_mounted = false;
	assert(newfs_driver_write_range(0, &super, 0, sizeof(super)) == 0);

	ddriver_close(super.fd);
	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int newfs_mkdir(const char* path, mode_t mode) {
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, true);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != DIR) {
		return -ENOTDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	char name[MAX_NAME_LEN];
	newfs_extract_stem(path, name);
	if(strcmp(name, "") == 0) {
		return -EEXIST;
	}
	for(newfs_dentry *d=t->inode->dentrys; d; d=d->next) {
		if(strcmp(d->name, name) == 0) {
			return -EEXIST;
		}
	}

	newfs_dentry *den = newfs_make_dentry(name, DIR);
	den->inode = newfs_alloc_inode(den);
	den->ino = den->inode->ino;
	NEWFS_DEBUG("mkdir %s using inode %d\n", den->name, den->ino);

	t->inode->size += sizeof(newfs_dentry_d);
	den->parent = t;
	den->next = t->inode->dentrys;
	t->inode->dentrys = den;
	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则失败
 */
int newfs_getattr(const char* path, struct stat * newfs_stat) {
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, false);
	if(t == NULL) {
		return -ENOENT;
	}

	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	if(t->ftype == DIR) {
		newfs_stat->st_mode = S_IFDIR | 0777;
	} else {
		newfs_stat->st_mode = S_IFREG | 0777;
	}

	if(t == super.root->dentry) {
		newfs_stat->st_nlink = 2;
	} else {
		newfs_stat->st_nlink = 1;
	}

	newfs_stat->st_size = t->inode->size;
	newfs_stat->st_uid = getuid();
	newfs_stat->st_gid = getgid();
	newfs_stat->st_blksize = super.sz_block;
	newfs_stat->st_blocks = (t->inode->size + super.sz_block - 1) / super.sz_block;
	newfs_stat->st_atime = time(NULL);
	newfs_stat->st_mtime = time(NULL);
	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int newfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, false);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != DIR) {
		return -ENOTDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	newfs_dentry *d = t->inode->dentrys;
	for(int i=0; i<offset && d; d=d->next, ++i) {
		NEWFS_DEBUG("skip %d %s\n", i, d->name);
	}

	for(int i=offset; d; d=d->next, ++i) {
		NEWFS_DEBUG("fill %d %s\n", i, d->name);
		if(filler(buf, d->name, NULL, i+1) != 0) {
			return 0; // buffer full
		}
	}
    return 0; // all  done
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int newfs_mknod(const char* path, mode_t mode, dev_t dev) {
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, true);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != DIR) {
		return -ENOTDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	char name[MAX_NAME_LEN];
	newfs_extract_stem(path, name);
	if(strcmp(name, "") == 0) {
		return -EEXIST;
	}
	for(newfs_dentry *d=t->inode->dentrys; d; d=d->next) {
		if(strcmp(d->name, name) == 0) {
			return -EEXIST;
		}
	}

	newfs_dentry *den = newfs_make_dentry(name, REG);
	den->inode = newfs_alloc_inode(den);
	den->ino = den->inode->ino;
	NEWFS_DEBUG("mknod %s using inode %d\n", den->name, den->ino);

	t->inode->size += sizeof(newfs_dentry_d);
	den->parent = t;
	den->next = t->inode->dentrys;
	t->inode->dentrys = den;
	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int newfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int newfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	if(size == 0) {
		return 0;
	}
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, false);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != REG) {
		return -EISDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	int newsize = t->inode->size;
	newsize = newsize > offset + size ? newsize : offset + size;
	newfs_truncate(path, newsize);

	int cnt = 0; // bytes written
	for(int i=0; i<MAX_IDX_NUM; ++i) {
		int p1 = super.sz_block * i, p2 = super.sz_block * (i+1);
		if(offset + size <= p1) {
			break;
		}
		assert(t->inode->data[i]);
		if(offset >= p2) {
			continue;
		}
		int begin = offset > p1 ? offset : p1;
		int end = offset + size < p2 ? offset + size : p2;
		memcpy(t->inode->data[i] + begin - p1, buf + cnt, end - begin);
		cnt += end - begin;
		NEWFS_DEBUG("MEMCPY %d %d %d %d\n", i, begin, end, end-begin);
	}
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int newfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	if(size == 0) {
		return 0;
	}
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, false);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != REG) {
		return -EISDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}
	NEWFS_DEBUG("file %s size %d\n", path, t->inode->size);
	if(offset + size > t->inode->size) {
		size = t->inode->size - offset;
	}
	if(size == 0) {
		return 0;
	}

	int cnt = 0; // bytes read
	for(int i=0; i<MAX_IDX_NUM; ++i) {
		int p1 = super.sz_block * i, p2 = super.sz_block * (i+1);
		if(offset + size <= p1) {
			break;
		}
		if(t->inode->data[i] == NULL) {
			NEWFS_DEBUG("warning: read empty data block %d\n", i);
			continue;
		}
		if(offset >= p2) {
			continue;
		}
		int begin = offset > p1 ? offset : p1;
		int end = offset + size < p2 ? offset + size : p2;
		memcpy(buf + cnt, t->inode->data[i] + begin - p1, end - begin);
		cnt += end - begin;
		NEWFS_DEBUG("MEMCPY %d %d %d %d\n", i, begin, end, end-begin);
	}
	return size;
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int newfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int newfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int newfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int newfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int newfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int newfs_truncate(const char* path, off_t offset) {
	newfs_dentry *t = newfs_lookup(path, super.root->dentry, false);
	if(t == NULL) {
		return -ENOENT;
	}
	if(t->ftype != REG) {
		return -EISDIR;
	}
	if(t->inode == NULL) {
		t->inode = newfs_read_inode(t->ino, t);
		assert(t->inode);
	}

	int cnt = (t->inode->size + super.sz_block - 1) / super.sz_block;
	int new_cnt = (offset + super.sz_block - 1) / super.sz_block;
	for(int i=new_cnt; i<cnt; ++i) {
		free(t->inode->data[i]); t->inode->data[i] = NULL;
	}
	for(int i=cnt; i<new_cnt; ++i) {
		t->inode->data[i] = malloc(super.sz_block);
		assert(t->inode->data[i]);
	}
	t->inode->size = offset;
	NEWFS_DEBUG("truncate %s to %d\n", path, offset);
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则失败
 */
int newfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	newfs_options.device = strdup("/home/students/210110607/ddriver");

	if (fuse_opt_parse(&args, &newfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
