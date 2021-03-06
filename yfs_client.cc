// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
//#include "extent_protocol.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client_cache(extent_dst);
  lc = new lock_client_cache(lock_dst, new lock_release_user(ec));
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    lc->acquire(inum);
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != OK) 
    {
        printf("error getting attr\n");
        lc->release(inum);
        return false;
    }

    if (a.type == extent_protocol::T_FILE) 
    {
        printf("isfile: %lld is a file\n", inum);
        lc->release(inum);
        return true;
    } 

    //printf("isfile: %lld is a dir\n", inum);
    lc->release(inum);
    return false;
}

bool
yfs_client::isdir(inum inum)
{
    lc->acquire(inum);
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != OK) 
    {
        printf("error getting attr\n");
        lc->release(inum);
        return false;
    }

    if (a.type == extent_protocol::T_DIR) 
    {
        printf("isfile: %lld is a dir\n", inum);
        lc->release(inum);
        return true;
    } 

    //printf("isfile: %lld is a dir\n", inum);
    lc->release(inum);
    return false;
}

bool
yfs_client::islink(inum inum)
{
    lc->acquire(inum);
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != OK) 
    {
        printf("error getting attr\n");
        lc->release(inum);
        return false;
    }

    if (a.type == extent_protocol::T_SLINK) 
    {
        printf("isfile: %lld is a symlink\n", inum);
        lc->release(inum);
        return true;
    } 

    //printf("isfile: %lld is a dir\n", inum);
    lc->release(inum);
    return false;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    lc->acquire(inum);
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != OK)
    {
        r = IOERR;
    }
    else
    {
      fin.atime = a.atime;
      fin.mtime = a.mtime;
      fin.ctime = a.ctime;
      fin.size = a.size;
      printf("getfile %016llx -> sz %llu\n", inum, fin.size);
    }

    lc->release(inum);
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    lc->acquire(inum);

    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != OK)
    {
        r = IOERR;
    }
    else
    {
      din.atime = a.atime;
      din.mtime = a.mtime;
      din.ctime = a.ctime;
    }

    lc->release(inum);
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    lc->acquire(ino);

    string buf;

    if(ec->get(ino, buf) != OK)
    {
      lc->release(ino);
      return IOERR;
    }

    buf.resize(size, 0);

    if(ec->put(ino, buf) != OK)
    {
      lc->release(ino);
      return IOERR;
    }

    lc->release(ino);
    return OK;
}

int 
yfs_client::_create(inum parent, const char *name, 
                    mode_t mode, inum &ino_out, extent_protocol::types type)
{
    int r;
    string pbuf;
    string fbuf;

    r = ec->get(parent, pbuf);
    if(r != OK) return r;

    map<string, inum> list = split(pbuf);
    map<string, inum>::iterator it
      = list.find(name);
    if(it != list.end()) return EXIST;

    r = ec->create(type, ino_out);
    if(r != OK) return r;

    //if(is_dir)
    //  fbuf = "/" + string(name) + "/" + filename(ino_out);
    r = ec->put(ino_out, fbuf);    
    if(r != OK) return r;

    pbuf.append("/" + string(name) + "/" + filename(ino_out));
    r = ec->put(parent, pbuf);

    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    lc->acquire(parent);

    int r = _create(parent, name, mode, ino_out, extent_protocol::T_FILE);

    lc->release(parent);
    return r;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    lc->acquire(parent);

    string sname = name;
    if(sname == "." || sname == "..")
    {
      lc->release(parent);
      return IOERR;
    }

    int r = _create(parent, name, mode, ino_out, extent_protocol::T_DIR);

    lc->release(parent);
    return r;
}

map<string, uint64_t> yfs_client::split(string src)
{
    //format: /name/ino/name/ino ...
    map<string, uint64_t> res;
    int left = src.find('/', 0);
    if(left == -1) return res;
    left += 1;
    int right = 0;
    while(true)
    {
      right = src.find('/', left);
      if(right == -1) break;
      string tmp_name = src.substr(left, right - left);
      left = right + 1;
      
      right = src.find('/', left);
      if(right == -1) //last one
      {
        right = src.size();
        string tmp_inostr = src.substr(left, right - left);
        inum tmp_ino = n2i(tmp_inostr);
        res[tmp_name] = tmp_ino;
        break;
      }
      string tmp_inostr = src.substr(left, right - left);
      inum tmp_ino = n2i(tmp_inostr);
      res[tmp_name] = tmp_ino;
      left = right + 1;
    }
    return res;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    lc->acquire(parent);

    string pbuf;
    if(ec->get(parent, pbuf) != OK)
    {
      found = false;
      lc->release(parent);
      return NOENT;
    }

    map<string, inum> res = split(pbuf);

    map<string, inum>::iterator it 
      = res.find(name);
    if(it == res.end())
    {
      found = false;
      lc->release(parent);
      return NOENT;
    }

    ino_out = it->second;
    found = true;
    lc->release(parent);
    return OK;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    lc->acquire(dir);

    string pbuf;
    list.clear();
    if(ec->get(dir, pbuf) != OK)
    {
      lc->release(dir);
      return NOENT;
    }

    map<string, inum> res = split(pbuf);

    for(map<string, inum>::iterator it = res.begin();
        it != res.end(); ++it)
    {
      dirent tmp = {it->first, it->second};
      list.push_back(tmp);
    }

    lc->release(dir);
    return OK;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    lc->acquire(ino);

    string fbuf;
    if(ec->get(ino, fbuf) != OK)
    {
      lc->release(ino);
      return IOERR;
    }

    if((size_t)off >= fbuf.size())
    {
      data = "";
      lc->release(ino);
      return OK;
    }

    data = fbuf.substr(off, size);
    lc->release(ino);
    return OK;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    lc->acquire(ino);

    string fbuf;
    if(ec->get(ino, fbuf) != OK)
    {
      lc->release(ino);
      return IOERR;
    }

    if(off + size > fbuf.size())
      fbuf.resize(off + size, 0);
    
    for(uint32_t i = 0; i < size; i++)
    {
      fbuf[off + i] = data[i];
    }
    
    if(ec->put(ino, fbuf) != OK)
    {
      lc->release(ino);
      return IOERR;
    }
    bytes_written = size;
    lc->release(ino);
    return OK;
}

int yfs_client::unlink(inum parent, const char *name)
{
    lc->acquire(parent);

    string pbuf;
    string sname = name;
    if(sname == "." || sname == "..")
    {
      lc->release(parent);
      return IOERR;
    }
    if(ec->get(parent, pbuf) != OK)
    {
      lc->release(parent);
      return NOENT;
    }
    
    map<string, inum> res = split(pbuf);
    map<string, inum>::iterator it
      = res.find(name);
    if(it == res.end())
    {
      lc->release(parent);
      return NOENT;
    }

    dirent d = {it->first, it->second};
    res.erase(it);
    pbuf = "";
    for(it = res.begin(); it != res.end(); ++it)
    {
      pbuf += "/" + it->first + "/" + filename(it->second);
    }
    if(ec->put(parent, pbuf) != OK)
    {
      lc->release(parent);
      return IOERR;
    }

    if(ec->remove(d.inum) != OK)
    {
      lc->release(parent);
      return IOERR;
    }

    lc->release(parent);
    return OK;
}

int yfs_client::symlink(inum parent, const char *name, const char *link, inum &ino_out)
{
  lc->acquire(parent);

  string pbuf;  
  if(ec->get(parent, pbuf) != OK)
  {
    lc->release(parent);
    return NOENT;
  }

  int r = _create(parent, name, 0, ino_out, extent_protocol::T_SLINK);
  if(r != OK)
  {
    lc->release(parent);
    return r;
  }

  r = ec->put(ino_out, link);

  lc->release(parent);
  return r;
}

int yfs_client::readlink(inum ino, string &link)
{
  lc->acquire(ino);
  int r = ec->get(ino, link);
  lc->release(ino);
  return r;
}


