#include "ddriver.h"
#include "newfs.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern struct newfs_super super;

/// read a logical block from the device
int newfs_driver_read(int blkno, void* buf_)
{
    char *buf = buf_;
    for(int i = 0; i < super.io_per_block; i++) {
        if(ddriver_seek(super.fd, (blkno * super.io_per_block + i) * super.sz_io, SEEK_SET) < 0 ||
            ddriver_read(super.fd, buf + i * super.sz_io, super.sz_io) != super.sz_io) {
            return 1;
        }
    }
    return 0;
}

int newfs_driver_read_range(int blkno, void* dest, int begin, int end)
{
    assert(begin >= 0 && end <= super.sz_block && begin <= end);
    char *buf = malloc(super.sz_block);
    assert(buf);

    if(newfs_driver_read(blkno, buf)) {
        free(buf);
        return 1;
    }
    memcpy(dest, buf + begin, end - begin);
    free(buf);
    return 0;
}

/// write a logical block to the device
int newfs_driver_write(int blkno, void* buf_)
{
    char *buf = (char*)buf_;
    for(int i = 0; i < super.io_per_block; i++) {
        if(ddriver_seek(super.fd, (blkno * super.io_per_block + i) * super.sz_io, SEEK_SET) < 0 ||
            ddriver_write(super.fd, buf + i * super.sz_io, super.sz_io) != super.sz_io) {
            return 1;
        }
    }
    return 0;
}

int newfs_driver_write_range(int blkno, void* src, int begin, int end)
{
    assert(begin >= 0 && end <= super.sz_block && begin <= end);
    char *buf = malloc(super.sz_block);
    assert(buf);

    if(newfs_driver_read(blkno, buf)) {
        free(buf);
        return 1;
    }
    memcpy(buf + begin, src, end - begin);
    if(newfs_driver_write(blkno, buf)) {
        free(buf);
        return 1;
    }
    free(buf);
    return 0;
}

bool newfs_test_bit(uint8_t* map, int bit) {
    return map[bit / 8] >> (bit & 0x7) & 1;
}
void newfs_set_bit(uint8_t* map, int bit) {
    map[bit / 8] |= 1 << (bit & 0x7);
}
void newfs_clear_bit(uint8_t* map, int bit) {
    map[bit / 8] &= ~(1 << (bit & 0x7));
}

newfs_inode* newfs_alloc_inode(newfs_dentry *den)
{
    assert(super.is_mounted);
    for(int i = 0; i < super.ino_num; i++) {
        if(!newfs_test_bit(super.imap, i)) {
            newfs_set_bit(super.imap, i);
            newfs_inode *inode = malloc(sizeof(newfs_inode));
            assert(inode);
            memset(inode, 0, sizeof(newfs_inode));
            inode->ino = i;
            inode->dentry = den;
            return inode;
        }
    }
    return NULL;
}

static int load_dentrys(newfs_inode *inode)
{
    assert(inode);
    assert(inode->dentrys == NULL);
    
    int cnt = inode->size / sizeof(newfs_dentry);
    for(int i=0; i<MAX_IDX_NUM; ++i) {
        if(inode->direct[i] == 0) {
            break;
        }
        inode->data[i] = malloc(super.sz_block);
        assert(newfs_driver_read(inode->direct[i], inode->data[i]) == 0);

        newfs_dentry_d *dens = (newfs_dentry_d*)inode->data[i];
        for(int j=0; (i * super.den_per_block) + j < cnt && j < super.den_per_block; ++j) {
            assert(dens[j].ino > 0);
            newfs_dentry *den = malloc(sizeof(newfs_dentry));
            assert(den);
            
            den->ino = dens[j].ino;
            safe_strcpy(den->name, dens[j].name, MAX_NAME_LEN);
            den->ftype = dens[j].ftype;
            den->parent = inode->dentry;
            den->inode = NULL;
            
            // insert to list
            den->next = inode->dentrys;
            inode->dentrys = den;
        }

        free(inode->data[i]); inode->data[i] = NULL;
    }
    return 0;
}

newfs_inode* newfs_read_inode(int ino, newfs_dentry *den)
{
    int blkno = super.ino_off + ino / super.ino_per_block;
    int offset = (ino % super.ino_per_block) * sizeof(newfs_inode_d);
    
    newfs_inode *inode = malloc(sizeof(newfs_inode));
    assert(inode);
    newfs_inode_d inode_d;
    if(newfs_driver_read_range(blkno, &inode_d, offset, offset + sizeof(newfs_inode_d))) {
        free(inode);
        return NULL;
    }

    inode->ino = ino;
    inode->size = inode_d.size;
    inode->link = inode_d.link;
    inode->ftype = inode_d.ftype;
    memcpy(inode->direct, inode_d.direct, sizeof(inode->direct));
    inode->dentry = den;

    if(inode->ftype == DIR) {
        load_dentrys(inode);
    }
    return inode;
}

int newfs_sync_inode(newfs_inode *u)
{
    newfs_inode_d d;
    d.ino = u->ino; d.size = u->size; d.link = u->link; d.ftype = u->ftype;
    memcpy(d.direct, u->direct, sizeof(d.direct));

    if(u->ftype == DIR) {
        // write sub-nodes to disk
        newfs_dentry *v = u->dentrys;
        for(; v; v = v->next) {
            if(v->inode) {
                assert(newfs_sync_inode(v->inode) == 0);
            }
        }

        // write dentrys to disk
        int capacity = 0;
        for(;capacity < MAX_IDX_NUM && u->data[capacity]; ++capacity);
        int need = (u->size + sizeof(newfs_dentry_d) - 1) / sizeof(newfs_dentry_d);
        need = (need + super.den_per_block - 1) / super.den_per_block;

        for(; capacity < need; ++capacity) {
            u->direct[capacity] = newfs_alloc_block();
            assert(u->direct[capacity] > 0);
        }
        for(; capacity > need; --capacity) {
            newfs_free_block(u->direct[capacity - 1]);
            u->direct[capacity - 1] = 0;
        }

        newfs_dentry_d *buf = malloc(super.sz_block);
        assert(buf);
        v = u->dentrys;
        for(int i=0; i<capacity; ++i) {
            memset(buf, 0, super.sz_block);
            for(int j=0; j<super.den_per_block && v; ++j, v = v->next) {
                buf[j].ino = v->ino;
                safe_strcpy(buf[j].name, v->name, MAX_NAME_LEN);
                buf[j].ftype = v->ftype;
            }
            assert(newfs_driver_write(u->direct[i], buf) == 0);
        }
        free(buf);
    } else {
        assert(u->size == 0); // normal file is not implemented
    }

    // write `d` to disk
    int blkno = super.ino_off + d.ino / super.ino_per_block;
    int offset = (d.ino % super.ino_per_block) * sizeof(newfs_inode_d);
    assert(newfs_driver_write_range(blkno, &d, offset, offset + sizeof(newfs_inode_d)) == 0);

    return 0;
}

int newfs_unmap_inode(newfs_inode *u)
{
    if(u->ftype == REG) {
        assert(u->size == 0); // normal file is not implemented
        free(u);
        return 0;
    }

    // unmap sub-nodes & sub-dentrys
    newfs_dentry *v = u->dentrys;
    for(; v;) {
        if(v->inode) {
            assert(newfs_unmap_inode(v->inode) == 0); v->inode = NULL;
        }
        newfs_dentry* nxt = v->next;
        free(v); v = nxt;
    }

    free(u);
    return 0;
}

int newfs_alloc_block(void)
{
    assert(super.is_mounted);
    for(int i = 0; i < super.data_blks; i++) {
        if(!newfs_test_bit(super.dmap, i)) {
            newfs_set_bit(super.dmap, i);
            return i;
        }
    }
    return -1;
}

int newfs_free_block(int blkno)
{
    assert(super.is_mounted);
    assert(blkno >= 0 && blkno < super.data_blks);
    newfs_clear_bit(super.dmap, blkno);
    return 0;
}

newfs_dentry* newfs_make_dentry(const char* name, FILE_TYPE ftype)
{
    newfs_dentry *den = malloc(sizeof(newfs_dentry));
    assert(den);
    memset(den, 0x00, sizeof(newfs_dentry));
    safe_strcpy(den->name, name, MAX_NAME_LEN);
    den->ftype = ftype;
    den->next = den->parent = NULL;
    return den;
}
