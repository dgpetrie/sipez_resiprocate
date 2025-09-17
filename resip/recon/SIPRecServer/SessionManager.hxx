#if !defined(SessionManager_hxx)
#define SessionManager_hxx

#include <map>
#include <set>
#include "ActiveCallInfo.hxx"
#include "../UserAgent.hxx"
#include "../HandleTypes.hxx"
#include "ConfigParser.hxx"

namespace resip
{
   class Contents;
}

namespace siprecserver
{
class Server;

class SessionInfo
{
public:
   SessionInfo(
      recon::ParticipantHandle participantHandle, 
      recon::ConversationHandle conversationHandle, 
      const resip::Uri& toUri,
      const resip::Uri& fromUri,
      const resip::Data& callId,
      resip::Contents* metaData) :
      mParticipantHandle(participantHandle), 
      mConversationHandle(conversationHandle),
      mToUri(toUri),
      mFromUri(fromUri),
      mCallId(callId)
   {
      updateMetaData(metaData);
   }

   void updateMetaData(resip::Contents* metaData)
   {
      mMetaData = std::unique_ptr<resip::Contents>(static_cast<resip::Contents*>(metaData->clone()));
   }

   recon::ParticipantHandle mParticipantHandle;
   recon::ConversationHandle mConversationHandle;
   resip::Uri mToUri;
   resip::Uri mFromUri;
   resip::Data mCallId;
   std::unique_ptr<resip::Contents> mMetaData;
};

class SessionManager
{
public:
   SessionManager(Server& server);
   virtual ~SessionManager(); 

   void startup(ConfigParser::SIPRecSettings& settings);
   void initializeConversationProfile(const resip::NameAddr& uri, const resip::Data& password, unsigned long registrationTime, const resip::NameAddr& outboundProxy);
   void initializeSettings(const resip::Data& recordingPath);

   void shutdown(bool shuttingDownServer);

   bool isMyProfile(recon::ConversationProfile& profile);
   void addNewSession(recon::ParticipantHandle participantHandle, const resip::SipMessage& sipMessage);
   bool removeSession(recon::ParticipantHandle participantHandle);
   void getActiveCallsInfo(CallInfoList& callInfos);

private:
   resip::Mutex mMutex;
   Server& mServer;
   volatile recon::ConversationProfileHandle mConversationProfileHandle;
   resip::NameAddr mSessionUri;  // The main AOR we are registing as 
   resip::Data mRecordPath;

   typedef std::map<recon::ParticipantHandle, SessionInfo*> SessionMap;
   SessionMap mSessions;
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

