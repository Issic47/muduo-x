// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/base/FileUtil.h>
#include <muduo/base/Logging.h> // strerror_tl

#include <boost/static_assert.hpp>
#include <uv.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

namespace muduo 
{
namespace utilities
{

class FSReqAutoCleanup : public boost::noncopyable 
{
public:
  explicit FSReqAutoCleanup(uv_fs_t *req)
    : req_(req) 
  {}

  ~FSReqAutoCleanup() {
    if (req_) {
      uv_fs_req_cleanup(req_);
    }
  }

private:
  uv_fs_t *req_;
};

} // !namespace utilities
} // !namespace muduo

using namespace muduo;

FileUtil::AppendFile::AppendFile(StringArg filename)
  : 
// FIXME(cbj)
#if defined(_WIN32)
    fp_(::fopen(filename.c_str(), "a")),
#else
    fp_(::fopen(filename.c_str(), "ae")),  // 'e' for O_CLOEXEC
#endif
    writtenBytes_(0)
{
  assert(fp_);
#if defined(NATIVE_WIN32)
  ::setvbuf(fp_, buffer_, _IOFBF, sizeof buffer_);
#else
  ::setbuffer(fp_, buffer_, sizeof buffer_);
  // posix_fadvise POSIX_FADV_DONTNEED ?
#endif
}

FileUtil::AppendFile::~AppendFile()
{
  ::fclose(fp_);
}

void FileUtil::AppendFile::append(const char* logline, const size_t len)
{
  size_t n = write(logline, len);
  size_t remain = len - n;
  while (remain > 0)
  {
    size_t x = write(logline + n, remain);
    if (x == 0)
    {
      int err = ferror(fp_);
      if (err)
      {
        fprintf(stderr, "AppendFile::append() failed %s\n", strerror_tl(err));
      }
      break;
    }
    n += x;
    remain = len - n; // remain -= x
  }

  writtenBytes_ += len;
}

void FileUtil::AppendFile::flush()
{
  ::fflush(fp_);
}

size_t FileUtil::AppendFile::write(const char* logline, size_t len)
{
  // #undef fwrite_unlocked
  return ::fwrite_unlocked(logline, 1, len, fp_);
}

FileUtil::ReadSmallFile::ReadSmallFile(StringArg filename)
  : fd_(-1), err_(0)
{
  buf_[0] = '\0';

  uv_fs_t req;
  utilities::FSReqAutoCleanup helper(&req);
  err_ = uv_fs_open(NULL, &req, filename.c_str(), O_RDONLY, 0, NULL);
  if (err_ == 0)
  {
    fd_ = req.result;
    if (fd_ < 0) 
    {
      err_ = req.result;
    }
  }
}

FileUtil::ReadSmallFile::~ReadSmallFile()
{
  if (fd_ >= 0)
  {
    uv_fs_t req;
    err_ = uv_fs_close(NULL, &req, fd_, NULL);
    uv_fs_req_cleanup(&req);
  }
}


// return errno
template<typename String>
int FileUtil::ReadSmallFile::readToString(int maxSize,
                                          String* content,
                                          int64_t* fileSize,
                                          int64_t* modifyTime,
                                          int64_t* createTime)
{
  // FIXME(cbj):
  //BOOST_STATIC_ASSERT(sizeof(off_t) == 8);
  assert(content != NULL);
  int err = err_;
  if (fd_ >= 0)
  {
    content->clear();

    if (fileSize)
    {
      uv_fs_t fstat_req;
      utilities::FSReqAutoCleanup helper(&fstat_req);
      err = uv_fs_fstat(NULL, &fstat_req, fd_, NULL);
      if (err == 0)
      {
        if ((err=fstat_req.result) == 0) 
        {
          if (S_ISREG(fstat_req.statbuf.st_mode))
          {
            *fileSize = fstat_req.statbuf.st_size;
            content->reserve(static_cast<int>((std::min)(implicit_cast<int64_t>(maxSize), *fileSize)));
          }
          else if (S_ISDIR(fstat_req.statbuf.st_mode))
          {
            err = UV_EISDIR;
          }
          if (modifyTime)
          {
            uv_timespec_t *timespec = &fstat_req.statbuf.st_mtim;
            *modifyTime = timespec->tv_sec;
          }
          if (createTime)
          {
            uv_timespec_t *timespec = &fstat_req.statbuf.st_ctim;
            *createTime = timespec->tv_sec;
          }
        }
      }
    }

    while (content->size() < implicit_cast<size_t>(maxSize))
    {
      size_t toRead = (std::min)(implicit_cast<size_t>(maxSize) - content->size(), sizeof(buf_));
      uv_fs_t read_req;
      utilities::FSReqAutoCleanup helper(&read_req);
      
      uv_buf_t buf = uv_buf_init(buf_, toRead);
      err = uv_fs_read(NULL, &read_req, fd_, &buf, 1, -1, NULL);
      if (err) break;

      ssize_t n = read_req.result;
      if (n > 0)
      {
        content->append(buf_, n);
      }
      else
      {
        if (n < 0)
        {
          err = read_req.result;
        }
        break;
      }
    }
  }
  return err;
}

int FileUtil::ReadSmallFile::readToBuffer(int* size)
{
  int err = err_;
  if (fd_ >= 0)
  {
    uv_fs_t read_req;
    utilities::FSReqAutoCleanup helper(&read_req);
    uv_buf_t buf = uv_buf_init(buf_, sizeof(buf_) - 1);
    err = uv_fs_read(NULL, &read_req, fd_, &buf, 1, 0, NULL);
    if (err == 0) {
      ssize_t n = read_req.result;
      if (n >= 0)
      {
        if (size)
        {
          *size = static_cast<int>(n);
        }
        buf_[n] = '\0';
      }
      else
      {
        err = read_req.result;
      }
    }
  }
  return err;
}

template 
int FileUtil::readFile(StringArg filename,
                                int maxSize,
                                string* content,
                                int64_t*, int64_t*, int64_t*);

template
int FileUtil::ReadSmallFile::readToString(
    int maxSize,
    string* content,
    int64_t*, int64_t*, int64_t*);

#ifndef MUDUO_STD_STRING
template int FileUtil::readFile(StringArg filename,
                                int maxSize,
                                std::string* content,
                                int64_t*, int64_t*, int64_t*);

template int FileUtil::ReadSmallFile::readToString(
    int maxSize,
    std::string* content,
    int64_t*, int64_t*, int64_t*);
#endif

