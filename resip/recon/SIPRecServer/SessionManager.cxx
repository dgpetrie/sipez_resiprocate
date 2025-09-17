#include "../UserAgent.hxx"
#include "AppSubsystem.hxx"
#include "SessionManager.hxx"
#include "Server.hxx"

#include <resip/stack/ExtensionParameter.hxx>
#include <resip/stack/Contents.hxx>
#include <rutil/Log.hxx>
#include <rutil/Logger.hxx>
#include <rutil/WinLeakCheck.hxx>

using namespace recon;
using namespace resip;
using namespace std;

#define RESIPROCATE_SUBSYSTEM AppSubsystem::SIPRECSERVER

#define SIPRECLOG_PREFIX << "SessionManager[" << mSessionUri << "] "

static const resip::ExtensionParameter p_automaton("automaton");
static const resip::ExtensionParameter p_byeless("+sip.byeless");
static const resip::ExtensionParameter p_rendering("+sip.rendering");

namespace siprecserver 
{

SessionManager::SessionManager(Server& server) :
   mServer(server),
   mConversationProfileHandle(0)
{
}

SessionManager::~SessionManager()
{
}

void
SessionManager::startup(ConfigParser::SIPRecSettings& settings)
{
   // Initialize settings
   initializeSettings(settings.mRecordPath);

   // Setup ConversationProfile - If session setting specific outbound proxy is not specified, then use global 
   // outbound proxy setting.
   initializeConversationProfile(settings.mUri, settings.mPassword, settings.mRegistrationTime, 
      !settings.mOutboundProxy.uri().host().empty() ? settings.mOutboundProxy : mServer.mConfig.mOutboundProxy);

   InfoLog(SIPRECLOG_PREFIX << "startup: RecordPath=" << settings.mRecordPath);
}

void 
SessionManager::initializeConversationProfile(const NameAddr& uri, const Data& password, unsigned long registrationTime, const resip::NameAddr& outboundProxy)
{
   if(mConversationProfileHandle)
   {
       mServer.mMyUserAgent->destroyConversationProfile(mConversationProfileHandle);
       mConversationProfileHandle = 0;
   }

   // Setup ConversationProfile
   auto conversationProfile = std::make_shared<ConversationProfile>(mServer.mUserAgentMasterProfile);
   conversationProfile->setDefaultRegistrationTime(registrationTime);  
   conversationProfile->setDefaultRegistrationRetryTime(120);  // 2 mins
   conversationProfile->setDefaultFrom(uri);
   conversationProfile->setDigestCredential(uri.uri().host(), uri.uri().user(), password);  
   if(!outboundProxy.uri().host().empty())
   {
      conversationProfile->setOutboundProxy(outboundProxy.uri());
   }
   conversationProfile->challengeOODReferRequests() = false;
   conversationProfile->setExtraHeadersInReferNotifySipFragEnabled(true);  // Enable dialog identifying headers in SipFrag bodies of Refer Notifies - required for a music on hold server
   // TODO SLG - need anything here for SIPRec? If not, delete these lines
   //NameAddr capabilities;
   //capabilities.param(p_automaton);
   //capabilities.param(p_byeless);
   //capabilities.param(p_rendering) = "\"no\"";
   //conversationProfile->setUserAgentCapabilities(capabilities);
   conversationProfile->natTraversalMode() = ConversationProfile::NoNatTraversal;
   conversationProfile->secureMediaMode() = ConversationProfile::NoSecureMedia;
   mServer.buildSessionCapabilities(conversationProfile->sessionCaps());
   mConversationProfileHandle = mServer.mMyUserAgent->addConversationProfile(conversationProfile);
   mSessionUri = uri;
}

void 
SessionManager::initializeSettings(const resip::Data& recordPath)
{
   Lock lock(mMutex);
   mRecordPath = recordPath;
}

void
SessionManager::shutdown(bool shuttingDownServer)
{
   Lock lock(mMutex);
   // Destroy all sessions
   SessionMap::iterator it = mSessions.begin();
   for (; it != mSessions.end(); it++)
   {
      // Destory the conversation and it will destroy all participants
      mServer.destroyConversation(it->second->mConversationHandle);
      delete it->second;
   }
   mSessions.clear();

   // If shutting down server, then we shouldn't remove the conversation profiles here
   // shutting down the ConversationManager will take care of this.  We need to be sure
   // we don't remove all conversation profiles when we are still processing SipMessages,
   // since recon requires at least one to be present for inbound processing.
   if(mConversationProfileHandle && !shuttingDownServer)
   {
       mServer.mMyUserAgent->destroyConversationProfile(mConversationProfileHandle);
       mConversationProfileHandle = 0;
   }
   InfoLog(SIPRECLOG_PREFIX << "shutdown");
}

bool 
SessionManager::isMyProfile(recon::ConversationProfile& profile)
{
   Lock lock(mMutex);
   return profile.getHandle() == mConversationProfileHandle;
}

void 
SessionManager::addNewSession(recon::ParticipantHandle participantHandle, const resip::SipMessage& sipMessage)
{
   Lock lock(mMutex);

   resip::Data callId = sipMessage.header(h_CallId).value();
   resip::Data recordFilename;
   {
      resip::DataStream ds(recordFilename);
      ds << mRecordPath << "/recording-" << callId << ".wav";
   }

   // Create a new Conversation for this call/session
   recon::ConversationHandle conversationHandle = mServer.createConversation(ConversationManager::AutoHoldEnabled);
   InfoLog(SIPRECLOG_PREFIX << "addNewSession: created new conversation for SIPREc session, handle=" << conversationHandle << ", recordFilename=" << recordFilename);

   resip_assert(conversationHandle);

   // Add this new inbound participant/call to the newly created conversation
   mServer.addParticipant(conversationHandle, participantHandle);

   // Add a recorder to conversation
   /** 
     Note:  This is a subset of the recon docs for createMediaResourceParticipant - the information 
            relevant to recording only.

     record:<filepath> - Single channel recorder.  If filename only, then writes to
                         application directory. Use | instead of : for drive specifier.
                         ;append parameter specifies to append to an existing recording
     record:circularbuffer - Single channel recorded audio is written to provided media
                         specific CircularBuffer
     record-mc:<filepath> - Multichannel recorder. If filename only, then writes to application
                         directory.  Use | instead of : for drive specifier.
                         ;append parameter specifies to append to an existing recording
                         ;numchannels parameter specifies either 1 or 2 channels of recording
     record-mc:circularbuffer - Multichannel recorded audio is written to provided media
                         specific CircularBuffer.
                         ;numchannels parameter specifies either 1 or 2 channels of recording
     buffer:<type> - For sipXtapi the only allowed type is RAW_PCM_16, the sampling rate
                         is expected to be 8khz.

     other optional parameters are: [;duration=<milliseconds>][;repeat][silencetime=<milliseconds>][;format=<recording_format>][;startoffset=<milliseconds>]
        - 'duration' specifies max recording length in Ms
        - 'silencetime' parameter specifies ms of silence to end recording
        - 'format' Possible values: WAV_PCM16, WAV_MULAW, WAV_ALAW, WAV_GSM, OGG_OPUS

     Sample mediaUrls:
        record:recording.wav             - records all participants audio mixed together in a WAV file of type WAV_PCM16, must be manually destroyed
        record:recording.ogg;format=OGG_OPUS - records all participants audio mixed together in an Opus encoded OGG file, must be manually destroyed
        record:recording.wav;duration=30000;silencetime=5000 - records all participants audio mixed togehter in a WAV file, for up to 5 mins, stop
                                                               automatically when voice is missing for 5 seconds
        record:circularbuffer;format=WAV_PCM16 - records all participants audio mixed together as PCM16 in the provided ciruclar buffer (no WAV header
                                                 is generated). Must be manually destroyed.
        record-mc:circularbuffer;format=WAV_PCM16;numchannels=2 - records all parties in the left channel unless they have channel 2 recording enabled
                                                 via the modifyParticipantRecordChannel API, then they are mixed into the right channel.  Use PCM16
                                                 format and output to the provided ciruclar buffer (no WAV header is generated).  Must be manually destroyed.
        buffer:RAW_PCM_16;repeat         - plays the audio from the provided playAudioBuffer parameter, repeating when complete until participant is destroyed

     @param convHandle - Handle of the conversation to create the MediaParticipant in
     @param mediaUrl - Controls what type of media participant to add
     @param playAudioBuffer - not used for recording
     @param recordingCircularBuffer - a pointer to media stack specific circular buffer implementation to pass to the recorder.  Audio can be read from
                                      the circular buffer as it is being recorded.  For sipXtapi, this implementation is of type utl\CircularBufferPtr.h

     @return A handle to the newly created media participant
   */
   resip::Data mediaUri;
   {
      resip::DataStream ds(mediaUri);
      ds << "record-mc:" << recordFilename << ";format=WAV_PCM16;numchannels=2";
   }
   mServer.createMediaResourceParticipant(conversationHandle, resip::Uri(mediaUri));

   // Answer the call (SIP 200/INVITE)
   mServer.answerParticipant(participantHandle);

   // Create new SessionInfo and add to map
   SessionInfo* sessionInfo = new SessionInfo(
      participantHandle, 
      conversationHandle,
      sipMessage.header(h_To).uri(),
      sipMessage.header(h_From).uri(),
      sipMessage.header(h_CallId).value(),
      sipMessage.getContents());
   mSessions.insert(std::make_pair(participantHandle, sessionInfo));
}

bool
SessionManager::removeSession(ParticipantHandle participantHandle)
{
   Lock lock(mMutex);
   // Find the session
   SessionMap::iterator it = mSessions.find(participantHandle);
   if (it != mSessions.end())
   {
      mServer.destroyConversation(it->second->mConversationHandle);
      mSessions.erase(it);
      return true;
   }
   return false;
}

void 
SessionManager::getActiveCallsInfo(CallInfoList& callInfos)
{
   Lock lock(mMutex);
   SessionMap::iterator it = mSessions.begin();
   for(; it != mSessions.end(); it++)
   {
      // TODO TEMP
      callInfos.push_back(ActiveCallInfo(it->second->mToUri, it->second->mFromUri, it->second->mCallId, it->second->mParticipantHandle, it->second->mConversationHandle));
   }
}

}

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

