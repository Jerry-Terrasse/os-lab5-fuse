#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_NAME_LEN    128
#define MAX_IDX_NUM     6

typedef enum file_type {
    REG,           // 普通文件
    DIR             // 目录文件
} FILE_TYPE;

struct custom_options {
	const char*        device;
};

typedef struct newfs_inode_d {
    uint32_t  ino;         // inode号

    int       size;        // 文件大小
    int       link;        // 链接数
    FILE_TYPE ftype;       // 文件类型

    int       direct[MAX_IDX_NUM]; // 直接索引(数据块号)
} newfs_inode_d;

typedef struct newfs_inode {
    uint32_t  ino;         // inode号

    int       size;        // 文件大小
    int       link;        // 链接数
    FILE_TYPE ftype;       // 文件类型

    int       direct[MAX_IDX_NUM]; // 直接索引(数据块号, 0表示未分配)

    uint8_t*  data[MAX_IDX_NUM];   // 数据
    struct newfs_dentry* dentry;   // 此结点对应的目录项
    struct newfs_dentry* dentrys;  // 目录项(仅当为目录文件时有效, 且必定会被加载)
} newfs_inode;

typedef struct newfs_dentry_d {
    uint32_t  ino;                // inode号
    char      name[MAX_NAME_LEN]; // 文件名
    FILE_TYPE ftype;              // 文件类型
} newfs_dentry_d;

typedef struct newfs_dentry {
    uint32_t  ino;                // inode号
    char      name[MAX_NAME_LEN]; // 文件名
    FILE_TYPE ftype;              // 文件类型
    
    struct newfs_dentry* parent;  // 父目录
    struct newfs_dentry* next;    // 父目录下一个dentry
    struct newfs_inode*  inode;   // inode(可以为NULL表示未加载)
} newfs_dentry;

struct newfs_super {
    uint32_t magic;
    
    int      sz_io;     // IO块大小
    int      sz_disk;   // 磁盘大小
    int      sz_block;  // 逻辑块大小
    int      tot_block; // 逻辑块总数

    int      imap_off;  // inode位图偏移
    int      imap_blks; // inode位图占用块数
    int      dmap_off;  // 数据块位图偏移
    int      dmap_blks; // 数据块位图占用块数
    int      ino_off;   // inode区偏移
    int      ino_per_block; // 每个逻辑块包含的inode数
    int      den_per_block; // 每个逻辑块包含的目录项数
    int      ino_blks;  // inode区占用块数
    int      ino_num;   // inode总数
    int      data_off;  // 数据区偏移
    int      data_blks; // 数据区占用块数

    int      root_ino;  // 根目录inode号

    // only available in memory:
    int          fd;    // 设备文件描述符
    int          io_per_block; // 每个逻辑块包含的IO块数
    uint8_t*     imap;  // inode位图
    uint8_t*     dmap;  // 数据块位图
    newfs_inode* root;  // 根目录inode
    bool         is_mounted; // 是否已挂载
};

#endif /* _TYPES_H_ */
