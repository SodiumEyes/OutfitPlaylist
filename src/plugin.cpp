#include "log.h"
#include "OutfitPlaylist.h"

void OnDataLoaded()
{
   
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
        
		break;
	case SKSE::MessagingInterface::kPostLoad:
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
        break;
	case SKSE::MessagingInterface::kNewGame:
		OutfitPlaylist::LoadPluginData();
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);
	SetupLog();

    auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	const SKSE::SerializationInterface* serde = SKSE::GetSerializationInterface();
	serde->SetUniqueID(_byteswap_ulong('OPL'));

	SKSE::GetPapyrusInterface()->Register(OutfitPlaylist::RegisterFunctions);
	serde->SetLoadCallback(OutfitPlaylist::OnGameLoaded);
	serde->SetSaveCallback(OutfitPlaylist::OnGameSaved);
	
    return true;
}