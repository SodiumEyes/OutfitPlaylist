#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace OutfitPlaylist
{
	typedef std::vector<RE::TESForm*> FormVec;
	typedef std::vector<unsigned int> IndexVec;

	struct Outfit
	{
		std::string name;
		std::string groupName;
		FormVec forms;
	};

	struct OutfitGroup
	{
		std::string name;
		IndexVec outfitIndices;
	};

	//Plugin
	void LoadPluginData();
	void OnGameLoaded(SKSE::SerializationInterface* serde);
	void OnGameSaved(SKSE::SerializationInterface* serde);

	//Outfits
	Outfit* setOutfit(RE::Actor* actor, unsigned int index);
	void clearOutfit(RE::Actor* actor, FormVec* forms_out = NULL);

	bool outfitFormsAreTheSame(Outfit& outfit1, Outfit& outfit2);

	//Papyrus
	bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);
}
