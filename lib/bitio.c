/* bitio.c
   implementation of bitio.h.

   Part of the swftools package.
   
   Copyright (c) 2001 Matthias Kramm <kramm@quiss.org> 

   This file is distributed under the GPL, see file COPYING for details */#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include "./bitio.h"

#define ZLIB_BUFFER_SIZE 16384

struct memread_t
{
    unsigned char*data;
    int pos;
    int length;
};

struct memwrite_t
{
    unsigned char*data;
    int pos;
    int length;
};

struct zlibinflate_t
{
    z_stream zs;
    struct reader_t*input;
    unsigned char readbuffer[ZLIB_BUFFER_SIZE];
};

struct zlibdeflate_t
{
    z_stream zs;
    struct writer_t*output;
    unsigned char writebuffer[ZLIB_BUFFER_SIZE];
};

void reader_resetbits(struct reader_t*r)
{
    r->mybyte = 0;
    r->bitpos = 8;

}

static int reader_zlibinflate(struct reader_t*reader, void* data, int len);
static int reader_fileread(struct reader_t*reader, void* data, int len);
static int reader_memread(struct reader_t*reader, void* data, int len);
static void zlib_error(int ret, char* msg, z_stream*zs);

void reader_init_filereader(struct reader_t*r, int handle)
{
    r->read = reader_fileread;
    r->internal = (void*)handle;
    r->type = READER_TYPE_FILE;
    r->mybyte = 0;
    r->bitpos = 8;
}

void reader_init_memreader(struct reader_t*r, void*newdata, int newlength)
{
    struct memread_t*mr = malloc(sizeof(struct memread_t));
    mr->data = newdata;
    mr->length = newlength;
    r->read = reader_memread;
    r->internal = (void*)mr;
    r->type = READER_TYPE_MEM;
    r->mybyte = 0;
    r->bitpos = 8;
}

void reader_init_zlibinflate(struct reader_t*r, struct reader_t*input)
{
    struct zlibinflate_t*z;
    int ret;
    memset(r, 0, sizeof(struct reader_t));
    z = (struct zlibinflate_t*)malloc(sizeof(struct zlibinflate_t));
    memset(z, 0, sizeof(struct zlibinflate_t));
    r->internal = z;
    r->read = reader_zlibinflate;
    r->type = READER_TYPE_ZLIB;
    z->input = input;
    memset(&z->zs,0,sizeof(z_stream));
    z->zs.zalloc = Z_NULL;
    z->zs.zfree  = Z_NULL;
    z->zs.opaque = Z_NULL;
    ret = inflateInit(&z->zs);
    if (ret != Z_OK) zlib_error(ret, "bitio:inflate_init", &z->zs);
    reader_resetbits(r);
}

static void zlib_error(int ret, char* msg, z_stream*zs)
{
    fprintf(stderr, "%s: zlib error (%d): last zlib error: %s\n",
	  msg,
	  ret,
	  zs->msg?zs->msg:"unknown");
    perror("errno:");
    exit(1);
}

static int reader_fileread(struct reader_t*reader, void* data, int len) 
{
    return read((int)reader->internal, data, len);
}

static int reader_memread(struct reader_t*reader, void* data, int len) 
{
    struct memread_t*mr = (struct memread_t*)reader->internal;

    if(mr->length - mr->pos > len) {
	memcpy(data, &mr->data[mr->pos], len);
	mr->pos += len;
	return len;
    } else {
	memcpy(data, &mr->data[mr->pos], mr->length - mr->pos);
	mr->pos = mr->length;
	return mr->length - mr->pos;
    }
}

static int reader_zlibinflate(struct reader_t*reader, void* data, int len) 
{
    struct zlibinflate_t*z = (struct zlibinflate_t*)reader->internal;
    int ret;
    if(!z)
	return 0;
    
    z->zs.next_out = data;
    z->zs.avail_out = len;

    while(1) {
	if(!z->zs.avail_in) {
	    z->zs.avail_in = z->input->read(z->input, z->readbuffer, ZLIB_BUFFER_SIZE);
	    z->zs.next_in = z->readbuffer;
	}
	if(z->zs.avail_in)
	    ret = inflate(&z->zs, Z_NO_FLUSH);
	else
	    ret = inflate(&z->zs, Z_FINISH);
    
	if (ret != Z_OK &&
	    ret != Z_STREAM_END) zlib_error(ret, "bitio:inflate_inflate", &z->zs);

	if (ret == Z_STREAM_END) {
		int pos = z->zs.next_out - (Bytef*)data;
		ret = inflateEnd(&z->zs);
		if (ret != Z_OK) zlib_error(ret, "bitio:inflate_end", &z->zs);
		free(reader->internal);
		reader->internal = 0;
		return pos;
	}
	if(!z->zs.avail_out) {
	    break;
	}
    }
    return len;
}
unsigned int reader_readbit(struct reader_t*r)
{
    if(r->bitpos==8) 
    {
	r->bitpos=0;
        r->read(r, &r->mybyte, 1);
    }
    return (r->mybyte>>(7-r->bitpos++))&1;
}
unsigned int reader_readbits(struct reader_t*r, int num)
{
    int t;
    int val = 0;
    for(t=0;t<num;t++)
    {
	val<<=1;
	val|=reader_readbit(r);
    }
    return val;
}

static int writer_zlibdeflate_write(struct writer_t*writer, void* data, int len);
static void writer_zlibdeflate_finish(struct writer_t*writer);
static int writer_filewrite_write(struct writer_t*w, void* data, int len);
static void writer_filewrite_finish(struct writer_t*w);

static int writer_filewrite_write(struct writer_t*w, void* data, int len) 
{
    return write((int)w->internal, data, len);
}
static void writer_filewrite_finish(struct writer_t*w)
{
}

static int writer_memwrite_write(struct writer_t*w, void* data, int len) 
{
    struct memread_t*mw = (struct memread_t*)w->internal;
    if(mw->length - mw->pos > len) {
	memcpy(&mw->data[mw->pos], data, len);
	mw->pos += len;
	return len;
    } else {
	memcpy(&mw->data[mw->pos], data, mw->length - mw->pos);
	mw->pos = mw->length;
	return mw->length - mw->pos;
    }
}
static void writer_memwrite_finish(struct writer_t*w)
{
    free(w->internal);
}

void writer_resetbits(struct writer_t*w)
{
    if(w->bitpos)
	w->write(w, &w->mybyte, 1);
    w->bitpos = 0;
    w->mybyte = 0;
}
void writer_init_filewriter(struct writer_t*w, int handle)
{
    memset(w, 0, sizeof(struct writer_t));
    w->write = writer_filewrite_write;
    w->finish = writer_filewrite_finish;
    w->internal = (void*)handle;
    w->type = WRITER_TYPE_FILE;
    w->bitpos = 0;
    w->mybyte = 0;
}
void writer_init_memwriter(struct writer_t*w, void*data, int len)
{
    struct memwrite_t *mr;
    mr = malloc(sizeof(struct memwrite_t));
    mr->data = data;
    mr->length = len;
    memset(w, 0, sizeof(struct writer_t));
    w->write = writer_memwrite_write;
    w->finish = writer_memwrite_finish;
    w->internal = (void*)mr;
    w->type = WRITER_TYPE_FILE;
    w->bitpos = 0;
    w->mybyte = 0;
}

void writer_init_zlibdeflate(struct writer_t*w, struct writer_t*output)
{
    struct zlibdeflate_t*z;
    int ret;
    memset(w, 0, sizeof(struct writer_t));
    z = (struct zlibdeflate_t*)malloc(sizeof(struct zlibdeflate_t));
    memset(z, 0, sizeof(struct zlibdeflate_t));
    w->internal = z;
    w->write = writer_zlibdeflate_write;
    w->finish = writer_zlibdeflate_finish;
    w->type = WRITER_TYPE_ZLIB;
    z->output = output;
    memset(&z->zs,0,sizeof(z_stream));
    z->zs.zalloc = Z_NULL;
    z->zs.zfree  = Z_NULL;
    z->zs.opaque = Z_NULL;
    ret = deflateInit(&z->zs, 9);
    if (ret != Z_OK) zlib_error(ret, "bitio:deflate_init", &z->zs);
    w->bitpos = 0;
    w->mybyte = 0;
    z->zs.next_out = z->writebuffer;
    z->zs.avail_out = ZLIB_BUFFER_SIZE;
}
static int writer_zlibdeflate_write(struct writer_t*writer, void* data, int len) 
{
    struct zlibdeflate_t*z = (struct zlibdeflate_t*)writer->internal;
    int ret;
    if(!z)
	return 0;
    
    z->zs.next_in = data;
    z->zs.avail_in = len;

    while(1) {
	ret = deflate(&z->zs, Z_NO_FLUSH);
	
	if (ret != Z_OK) zlib_error(ret, "bitio:deflate_deflate", &z->zs);

	if(z->zs.next_out != z->writebuffer) {
	    z->output->write(z->output, z->writebuffer, z->zs.next_out - (Bytef*)z->writebuffer);
	    z->zs.next_out = z->writebuffer;
	    z->zs.avail_out = ZLIB_BUFFER_SIZE;
	}

	if(!z->zs.avail_in) {
	    break;
	}
    }
    return len;
}
static void writer_zlibdeflate_finish(struct writer_t*writer)
{
    struct zlibdeflate_t*z = (struct zlibdeflate_t*)writer->internal;
    struct writer_t*output;
    int ret;
    if(!z)
	return;
    output= z->output;
    while(1) {
	ret = deflate(&z->zs, Z_FINISH);
	if (ret != Z_OK &&
	    ret != Z_STREAM_END) zlib_error(ret, "bitio:deflate_deflate", &z->zs);

	if(z->zs.next_out != z->writebuffer) {
	    z->output->write(z->output, z->writebuffer, z->zs.next_out - (Bytef*)z->writebuffer);
	    z->zs.next_out = z->writebuffer;
	    z->zs.avail_out = ZLIB_BUFFER_SIZE;
	}

	if (ret == Z_STREAM_END) {
	    break;

	}
    }
    ret = deflateEnd(&z->zs);
    if (ret != Z_OK) zlib_error(ret, "bitio:deflate_end", &z->zs);
    free(writer->internal);
    writer->internal = 0;
    output->finish(output);
}

void writer_writebit(struct writer_t*w, int bit)
{    
    if(w->bitpos==8) 
    {
        w->write(w, &w->mybyte, 1);
	w->bitpos = 0;
	w->mybyte = 0;
    }
    if(bit&1)
	w->mybyte |= 1 << (7 - w->bitpos);
    w->bitpos ++;
}
void writer_writebits(struct writer_t*w, unsigned int data, int bits)
{
    int t;
    for(t=0;t<bits;t++)
    {
	writer_writebit(w, (data >> (bits-t-1))&1);
    }
}

