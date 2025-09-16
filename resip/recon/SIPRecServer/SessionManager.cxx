#include "../UserAgent.hxx"
#include "AppSubsystem.hxx"
#include "SessionManager.hxx"
#include "Server.hxx"

#include <resip/stack/ExtensionParameter.hxx>
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
   mConversationProfileHandle(0),
   mMusicFilenameChanged(false)
{
}

SessionManager::~SessionManager()
{
}

void
SessionManager::startup(ConfigParser::SIPRecSettings& settings)
{
   // Initialize settings
   initializeSettings(settings.mRecordingLocation);

   // Setup ConversationProfile - If session setting specific outbound proxy is not specified, then use global 
   // outbound proxy setting.
   initializeConversationProfile(settings.mUri, settings.mPassword, settings.mRegistrationTime, 
      !settings.mOutboundProxy.uri().host().empty() ? settings.mOutboundProxy : mServer.mConfig.mOutboundProxy);

   // Create an initial conversation and start music
   ConversationHandle convHandle = mServer.createConversation(ConversationManager::AutoHoldBroadcastOnly);
   //mServer.createMediaResourceParticipant(convHandle, settings.mMOHFilenameUrl);  // Play Music
   mConversations[convHandle];
   mMusicFilenameChanged = false;

   InfoLog(SIPRECLOG_PREFIX << "startup: RecordingLocationUrl=" << settings.mRecordingLocation);
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
   NameAddr capabilities;
   capabilities.param(p_automaton);
   capabilities.param(p_byeless);
   capabilities.param(p_rendering) = "\"no\"";
   conversationProfile->setUserAgentCapabilities(capabilities);
   conversationProfile->natTraversalMode() = ConversationProfile::NoNatTraversal;
   conversationProfile->secureMediaMode() = ConversationProfile::NoSecureMedia;
   mServer.buildSessionCapabilities(conversationProfile->sessionCaps());
   mConversationProfileHandle = mServer.mMyUserAgent->addConversationProfile(conversationProfile);
   mSessionUri = uri;
}

void 
SessionManager::initializeSettings(const resip::Uri& recordingLocationUrl)
{
   Lock lock(mMutex);
   mRecordingLocationUrl = recordingLocationUrl;
   // If there is a single conversation with no participants, then there are no 
   // current parties on hold - re-create the conversation with new music
   if(mConversations.size() == 1 && mConversations.begin()->second.size() == 0)
   {
      mServer.destroyConversation(mConversations.begin()->first);
      mConversations.clear();

      // re-create an initial conversation and start music
      ConversationHandle convHandle = mServer.createConversation(ConversationManager::AutoHoldBroadcastOnly);
      //mServer.createMediaResourceParticipant(convHandle, mMusicFilename);  // Play Music
      mConversations[convHandle];
   }
   else
   {
      mMusicFilenameChanged = true;
   }
}

void
SessionManager::shutdown(bool shuttingDownServer)
{
   Lock lock(mMutex);
   // Destroy all conversations
   ConversationMap::iterator it = mConversations.begin();
   for (; it != mConversations.end(); it++)
   {
      // Clean up participant memory
      ParticipantMap::iterator partIt = it->second.begin();
      for (; partIt != it->second.end(); partIt++)
      {
         delete partIt->second;
      }
      it->second.clear();

      mServer.destroyConversation(it->first);
   }
   mConversations.clear();

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
SessionManager::addParticipant(ParticipantHandle participantHandle, const Uri& heldUri, const Uri& holdingUri)
{
   Lock lock(mMutex);
   ConversationHandle conversationToUse = 0;
   // Check if we have an existing conversation with room to add this party
   ConversationMap::iterator it = mConversations.begin();
   for(; it != mConversations.end(); it++)
   {
      if(it->second.size() < DEFAULT_BRIDGE_MAX_IN_OUTPUTS-3)
      {
         // Found an existing conversation with room - add the participant here
         conversationToUse = it->first;
         break;
      }
   }

   // No conversation found that we can use - create a new one
   if(!conversationToUse)
   {
      conversationToUse = mServer.createConversation(ConversationManager::AutoHoldBroadcastOnly);
      InfoLog(SIPRECLOG_PREFIX << "addParticipant: created new conversation for music on hold, id=" << conversationToUse);

      // Play Music
      //mServer.createMediaResourceParticipant(conversationToUse, mMusicFilename);
   }
   else
   {
       InfoLog(SIPRECLOG_PREFIX << "addParticipant: using existing conversation for music on hold, id=" << conversationToUse);
   }

   resip_assert(conversationToUse);

   mServer.addParticipant(conversationToUse, participantHandle);
   mServer.modifyParticipantContribution(conversationToUse, participantHandle, 100, 0 /* Mute participant */);
   mServer.answerParticipant(participantHandle);
   mConversations[conversationToUse].insert(std::make_pair(participantHandle, new ParticipantSessionInfo(participantHandle, heldUri, holdingUri)));
}

bool
SessionManager::removeParticipant(ParticipantHandle participantHandle)
{
   Lock lock(mMutex);
   // Find Conversation that participant is in
   ConversationMap::iterator it = mConversations.begin();
   for(; it != mConversations.end(); it++)
   {
      ParticipantMap::iterator partIt = it->second.find(participantHandle);
      if(partIt != it->second.end())
      {
         InfoLog(SIPRECLOG_PREFIX << "removeParticipant: found in conversation id=" << it->first << ", size=" << it->second.size());

         // Found! Remove from conversation
         delete partIt->second;
         it->second.erase(partIt);

         // Check if conversation is now empty, and it's not the last conversation
         if(it->second.size() == 0)
         {    
            if(mConversations.size() > 1)
            {
               // Destroy conversation (and containing media participant)
               mServer.destroyConversation(it->first);

               // Remove Conversation from Map
               mConversations.erase(it);

               InfoLog(SIPRECLOG_PREFIX << "removeParticipant: last participant in conversation, destroying conversation, num conversations now=" << mConversations.size());
            }
            else if(mConversations.size() == 1 && mMusicFilenameChanged)  // If the initial conversation is empty, and the music filename setting changed, then restart it
            {
               mServer.destroyConversation(mConversations.begin()->first);
               mConversations.clear();

               // re-create an initial conversation and start music
               ConversationHandle convHandle = mServer.createConversation(ConversationManager::AutoHoldBroadcastOnly);
               //mServer.createMediaResourceParticipant(convHandle, mMusicFilename);  // Play Music
               mConversations[convHandle];
               mMusicFilenameChanged = false;
            }
         }
         return true;
      }
   }
   return false;
}

void 
SessionManager::getActiveCallsInfo(CallInfoList& callInfos)
{
   Lock lock(mMutex);
   // Find Conversation that participant is in
   ConversationMap::iterator it = mConversations.begin();
   for(; it != mConversations.end(); it++)
   {
      ParticipantMap::iterator partIt = it->second.begin();
      for(; partIt != it->second.end(); partIt++)
      {
         callInfos.push_back(ActiveCallInfo(partIt->second->mHeldUri, partIt->second->mHoldingUri, "MOH", partIt->first, it->first));
      }
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

