#ifndef yfs_client_h
#define yfs_client_h

#include <string>

#include "lock_protocol.h"
#include "lock_client_cache.h"

//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>
#include <map>
using std::string;
using std::map;
using std::vector;

class yfs_client {
  extent_client_cache *ec;
  lock_client_cache *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);

  int _create(inum, const char *, mode_t, inum &, extent_protocol::types);
  map<string, uint64_t> split(string);
  

 public:
  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  bool islink(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  int setattr(inum, size_t);
  int lookup(inum, const char *, bool &, inum &);
  int create(inum, const char *, mode_t, inum &);
  int readdir(inum, std::list<dirent> &);
  int write(inum, size_t, off_t, const char *, size_t &);
  int read(inum, size_t, off_t, std::string &);
  int unlink(inum,const char *);
  int mkdir(inum , const char *, mode_t , inum &);
  int symlink(inum, const char *, const char *, inum &);
  int readlink(inum, string &);
};

#endif 
