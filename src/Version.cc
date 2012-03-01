#include "Version.hh"
#include <iostream>
#include <unistd.h>
#include <pwd.h>

#define xstr(x) str(x)
#define str(x) #x

using namespace std;

Version::Version()
{
   svnVersion_  = xstr(SVNVERSION);
   compileTime_ = __TIME__;
   compileDate_ = __DATE__;
   srcPath_     = xstr(PATH);
   cxxFlags_    = xstr(CXXFLAGS);
   cFlags_      = xstr(CFLAGS);
   ldFlags_     = xstr(LDFLAGS);
   buildTarget_ = xstr(TARGET);
   buildArch_   = xstr(ARCH);

   char tmp[256];
   gethostname(tmp, 256);
   host_        = tmp;
   //user_        = getpwuid(getuid())->pw_name; //Not supported on all platforms  
}

const Version& Version::getInstance()
{
   static Version* instance = 0;
   if (! instance)
      instance = new Version();
   return *instance;
}

ostream& Version::versionPrint(ostream& out) const
{
   out << "Version info:\n"
       << "-------------\n"
       << "  hostname      " << host_ << "\n"
       << "  user          " << user_ << "\n"
       << "  svn revision: " << svnVersion_ << "\n"
       << "  compile time: " << compileDate_ << " " << compileTime_ << "\n"
       << "  source path:  " << srcPath_ << "\n"
       << "  build target: " << buildTarget_ << "\n"
       << "  build arch:   " << buildArch_ << "\n"
       << "  CXXFLAGS:     " << cxxFlags_ << "\n"
       << "  CFLAGS:       " << cFlags_ << "\n"
       << "  LDFLAGS:      " << ldFlags_ << "\n"
       << "_________________________________________________________________\n"
       << endl;
   return out;
}
