#if !defined(ConfigParser_hxx)
#define ConfigParser_hxx

#include <map>
#include <rutil/Data.hxx>
#include <rutil/Socket.hxx>
#include <resip/stack/NameAddr.hxx>

namespace siprecserver
{

class ConfigParser 
{
public:
   ConfigParser(int argc, char** argv); // Use this constructor to parse command line arguments and read configuration file from disk
   ConfigParser();  // If you use this constructor you must manually set all configuration values
   virtual ~ConfigParser();
   
   // TODO SLG - Does SIPRec server need to register?  If not - we don't need these SIPRecSettings
   class SIPRecSettings
   {
   public:
      SIPRecSettings() : mRegistrationTime(3600) {}
      resip::NameAddr mUri;
      resip::Data mPassword;
      unsigned long mRegistrationTime;
      resip::NameAddr mOutboundProxy;
      resip::Uri mRecordingLocation;
   };
   typedef std::map<unsigned long , SIPRecSettings> SIPRecSettingsMap;
   SIPRecSettingsMap mSIPRecSettingsMap;

   // SIP Settings
   resip::Data mAddress;
   resip::Data mDnsServers;
   unsigned short mUdpPort;
   unsigned short mTcpPort;
   unsigned short mTlsPort;
   resip::Data mTlsDomain;
   resip::Data mCertificatePath;
   resip::NameAddr mOutboundProxy;
   bool mKeepAlives;

   // Media Settings
   unsigned short mMediaPortRangeStart;
   unsigned short mMediaPortRangeSize;
   resip::Data mSipXLogFilename;

   // General Settings
   unsigned short mHttpPort;
   resip::Data mLogLevel;
   resip::Data mLogFilename;
   unsigned int mLogFileMaxBytes;

   // Store pointer to after socket creation fn - can be used for QOS
   resip::AfterSocketCreationFuncPtr mSocketFunc;

private:
   void parseCommandLine(int argc, char** argv);
   void parseConfigFile(const resip::Data& filename);
   bool processOption(const resip::Data& name, const resip::Data& value);
   bool assignNameAddr(const resip::Data& settingName, const resip::Data& settingValue, resip::NameAddr& nameAddr);
   bool assignRecordUrl(const resip::Data& settingName, const resip::Data& settingValue, resip::Uri& url);
};
 
}

#endif

/* ====================================================================

 Copyright (c) 2010-2025, SIP Spectrum, Inc.

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are 
 met:

 1. Redistributions of source code must retain the above copyright 
    notice, this list of conditions and the following disclaimer. 

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution. 

 3. Neither the name of SIP Spectrum nor the names of its contributors 
    may be used to endorse or promote products derived from this 
    software without specific prior written permission. 

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ==================================================================== */

