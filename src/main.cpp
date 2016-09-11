//
//  main.cpp
//  DuplicateFinder
//
//  Created by Jack on 10/09/16.
//  Copyright © 2016 Jack. All rights reserved.
//

#include <stdint.h>
#include <openssl/md5.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>

#include <iostream>
#include <array>
#include <string>
#include <sstream>
#include <vector>
#include <numeric>
#include <iomanip>

using namespace std;

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s64 = uint64_t;

struct md5
{
  array<u8, MD5_DIGEST_LENGTH> data;
  
  md5() : data{{0}} { }
  
  bool operator==(const md5& other) const { return data == other.data; }
};

ostream& operator<< (ostream& stream, md5 val)
{
  ios::fmtflags f( cout.flags() );
  
  for (u32 i = 0; i < MD5_DIGEST_LENGTH; ++i)
    stream << hex << setfill('0') << setw(2) << (int)val.data[i];
  
  stream.flags(f);
  return stream;
}

struct entry;

struct utils
{
  static off_t getFileSize(const string& fileName);
  static off_t getFileSize(int fd);
  static bool computeMD5(const entry& entry);
  
  static std::vector<entry> enumerateDirectory(const string& folder, function<bool(const string&)> filter, bool recursive, bool verbose = false);
  
  static std::string humanReadableSize(off_t s);
};

struct entry
{
  std::string fileName;
  mutable off_t size;
  mutable md5 md5;
  
private:
  mutable bool hasSize;
  mutable bool hasMd5;
  
public:
  entry(std::string fileName) : fileName(fileName), size(0), hasSize(false), hasMd5(false) { }
  
  bool isSizeCached() const { return hasSize; }
  bool isMD5Cached() const { return hasMd5; }
  
  struct md5& md5buffer() const { return md5; }
  
  const string& getName() const { return fileName; }
  
  off_t getSize() const
  {
    if (!hasSize)
    {
      size = utils::getFileSize(fileName);
      hasSize = true;
    }
    
    return size;
  }
  
  const struct md5& getMD5() const
  {
    if (!hasMd5)
    {
      utils::computeMD5(*this);
      hasMd5 = true;
    }
    
    return md5;
  }
};

off_t utils::getFileSize(const std::string& fileName)
{
  struct stat buf;
  int rc = stat(fileName.c_str(), &buf);
  return rc == 0 ? buf.st_size : -1;
}

off_t utils::getFileSize(int fd)
{
  struct stat buf;
  int rc = fstat(fd, &buf);
  return rc == 0 ? buf.st_size : -1;
}

bool utils::computeMD5(const entry& entry)
{
  int fd = open(entry.fileName.c_str(), O_RDONLY);
  
  /* failed */
  if (fd < -1)
    return false;
    
  off_t size = entry.getSize();
  
  u8* buffer = static_cast<u8*>(mmap(0, size, PROT_READ, MAP_SHARED, fd, 0));
  
  struct md5 md5;
  
  MD5(buffer, size, entry.md5buffer().data.data());
  munmap(buffer, size);
  
  close(fd);
  
  return true;
}

vector<entry> utils::enumerateDirectory(const string& folder, function<bool(const string&)> filter, bool recursive, bool verbose)
{
  vector<entry> files;
  
  if (verbose)
    cout << "scanning folder " << folder << endl;
  
  DIR *d;
  struct dirent *dir;
  d = opendir(folder.c_str());
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
      string name = dir->d_name;
      
      if (name == "." || name == ".." || name == ".DS_Store")
        continue;
      else if (dir->d_type == DT_DIR && recursive)
      {
        auto rfiles = enumerateDirectory(folder + "/" + name, filter, true, verbose);
        files.reserve(files.size() + rfiles.size());
        files.insert(files.end(), rfiles.begin(), rfiles.end());
      }
      else if (dir->d_type == DT_REG)
        files.push_back(entry(folder + "/" + name));
    }
    
    closedir(d);
  }
  
  return files;
}

std::string utils::humanReadableSize(off_t bytes)
{
  constexpr u32 unit = 1024;
  static char buffer[128];
  
  if (bytes < unit) return to_string(bytes) + " B";
  
  int exp = log(bytes) / log(unit);
  char pre = "KMGTPE"[exp-1];
  sprintf(buffer, "%.1f %cb", bytes / pow(unit, exp), pre);
  
  return buffer;
}

struct match
{
  const entry& master;
  const entry& slave;
};

struct matcher
{
  static vector<match> findPotentialMatches(const vector<entry>& master, const vector<entry>& slave)
  {
    vector<match> matches;
    
    for (const auto& me : master)
    {
      for (const auto& se : slave)
      {
        if (me.getSize() == se.getSize())
          matches.push_back({me, se});
      }
    }
    
    return matches;
  }
  
  static bool verifyMatch(const match& match)
  {
    return match.master.getMD5() == match.slave.getMD5();
  }
};


int main(int argc, const char * argv[]) {
  string masterPath = "/Volumes/Vicky/Photos-SSD";
  string slavePath = "/Volumes/Vicky/-----Photos";
  
  auto mfiles = utils::enumerateDirectory(masterPath, [](const string& name) { return true; }, true, true);
  auto sfiles = utils::enumerateDirectory(slavePath, [](const string& name) { return true; }, true, true);

  struct info
  {
    off_t size;
    size_t count;
  };
  
  info inf = accumulate(mfiles.begin(), mfiles.end(), (info){0,0}, [] (info i, const entry& e) {
    ++i.count;
    i.size += e.getSize();
    return i;
  });
  
  inf = accumulate(sfiles.begin(), sfiles.end(), inf, [] (info i, const entry& e) {
    ++i.count;
    i.size += e.getSize();
    return i;
  });
  
  cout << "found " << inf.count << " files, total size: " << utils::humanReadableSize(inf.size) << endl;
  
  vector<match> potentialMatches = matcher::findPotentialMatches(mfiles, sfiles);
  
  cout << "found " << potentialMatches.size() << " potential matches " << endl;
  
  vector<match> verifiedMatches;
  
  copy_if(potentialMatches.begin(), potentialMatches.end(), back_inserter(verifiedMatches), [](const match& m) { return matcher::verifyMatch(m); });

  cout << "found " << potentialMatches.size() << " verified matches " << endl;

  
  /*for (const auto& f : files)
  {
    off_t size = f.getSize();
    md5 md5 = f.getMD5();
    
    cout << f.getName() << " size: " << size << " md5: " << md5 << endl;
  }*/

}
