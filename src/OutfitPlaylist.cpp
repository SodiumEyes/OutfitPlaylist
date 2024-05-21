#include "OutfitPlaylist.h"
#include "SKSEUtil/FormIDUtil.h"
#include "SKSEUtil/MathUtil.h"
#include "SKSEUtil/ActorUtil.h"
#include "SKSEUtil/StringUtil.h"
#include <filesystem>
#include <random>

#include <json/json.h>

using namespace RE;
using namespace SKSE;

namespace OutfitPlaylist
{
	std::vector<Outfit> sOutfits;
	typedef std::map<std::string, OutfitGroup> OutfitGroupMap;
	OutfitGroupMap sOutfitGroups;

	const std::string OPLQuest = "oplQuestScript";
	const std::string CustomOutfitGroupName = "CustomOutfits";

	typedef std::map<FormID, Outfit> ActorOutfitMap;
	ActorOutfitMap sActorEquippedOutfits;

	std::set<RE::FormID> sIgnoredFormIDs;

	IndexVec sShuffledIndices;
	unsigned int sShuffledSeed = 0u;

	const unsigned int SAVE_VERSION = 1u;

	inline const auto OutfitPlaylistRecord = _byteswap_ulong('OPLE');

	//Util
	template <class FormT>
	bool LookupForm(RE::FormID form_id, const std::string& mod_name, FormT*& out)
	{
		TESForm* res = TESDataHandler::GetSingleton()->LookupForm(form_id, mod_name);
		if (res) {
			out = res->As<FormT>();
			std::string name = res->GetName();
			if (name.empty())
				name = res->GetFormEditorID();
			//log::info("Retrieved form: {} {} {}", mod_name, SKSEUtil::hexToString(form_id), name);
			return true;
		} else {
			//log::error("Form Retrieval failed: {} {}", mod_name, SKSEUtil::hexToString(form_id));
			return false;
		}
	}

	//Plugin
	void LoadPluginData()
	{
		sActorEquippedOutfits.clear();
		sShuffledIndices.clear();
		sShuffledSeed = 0u;

		//Load config
		Json::Reader reader;
		std::ifstream config_file("Data/SKSE/Plugins/OutfitPlaylistConfig.json");
		Json::Value config_json;
		reader.parse(config_file, config_json);

		sIgnoredFormIDs.clear();
		Json::Value ignored_forms_json = config_json["ignoredForms"];
		for (Json::Value::iterator it = ignored_forms_json.begin(); it != ignored_forms_json.end(); ++it) {
			RE::FormID local_form_id;
			std::string mod_name;
			if (SKSEUtil::deserializeFormID(*it, local_form_id, mod_name)) {
				RE::TESForm* form = TESDataHandler::GetSingleton()->LookupForm(local_form_id, mod_name);
				if (form)
					sIgnoredFormIDs.insert(form->formID);
			}
		}

		log::info("Loaded {} ignored form ids", sIgnoredFormIDs.size());

		//Load Outfits
		sOutfits.clear();
		sOutfitGroups.clear();
		sOutfits.reserve(1024);

		std::string outfit_json_dir = "Data/SKSE/Plugins/OutfitPlaylist";
		for (const auto& entry : std::filesystem::directory_iterator(outfit_json_dir)) {
			std::string path = entry.path().string();
			if (!path.ends_with(".json"))
				continue;

			log::info("Reading outfit group file {}", path);
			std::ifstream group_file(path);

			Json::Value group_json;
			reader.parse(group_file, group_json);

			Json::Value outfits_json = group_json["outfits"];
			if (!outfits_json.isObject()) {
				log::error("Invalid group file JSON {}", path);
				continue;
			}

			std::string group_name = entry.path().filename().replace_extension("").string();
			OutfitGroup& group = sOutfitGroups.insert(std::pair<std::string, OutfitGroup>(group_name, OutfitGroup())).first->second;
			group.name = group_name;
			group.outfitIndices.reserve(outfits_json.size());

			for (Json::Value::iterator it = outfits_json.begin(); it != outfits_json.end(); ++it) {
				if (!it->isArray()) {
					log::error("Invalid outfit JSON {}", it.key().asString());
					continue;
				}

				unsigned int outfit_index = sOutfits.size();
				sOutfits.push_back(Outfit());
				Outfit& outfit = sOutfits[outfit_index];
				outfit.name = it.key().asString();
				outfit.groupName = group.name;
				outfit.forms.reserve(it->size());

				for (unsigned int i = 0u; i < it->size(); i++) {
					FormID form_id;
					std::string mod_name;
					if (SKSEUtil::deserializeFormID((*it)[i], form_id, mod_name)) {
						TESForm* form = NULL;
						if (!LookupForm(form_id, mod_name, form) || !form) {
							log::error("Outfit form not found: {}:{}", outfit.name, (*it)[i].asString());
							continue;
						}

						if (form->formType != FormType::Armor && form->formType != FormType::Weapon && form->formType != FormType::Ammo && form->formType != FormType::Light)
						{
							log::error("Outfit form is the wrong type: {}:{}", outfit.name, (*it)[i].asString());
							continue;
						}

						outfit.forms.push_back(form);
					}
					else {
						log::error("Invalid outfit formID: {}:{}", outfit.name, (*it)[i].asString());
					}
				}

				group.outfitIndices.push_back(outfit_index);
			}
		}

		log::info("Loaded {} outfits in {} groups", sOutfits.size(), sOutfitGroups.size());
	}

	void OnGameLoaded(SKSE::SerializationInterface* serde)
	{
		LoadPluginData();

		//Load saved equipped outfits
		std::uint32_t type;
		std::uint32_t size;
		std::uint32_t version;
		while (serde->GetNextRecordInfo(type, version, size))
		{
			if (type == OutfitPlaylistRecord) {
				unsigned int num_saved_actors;
				serde->ReadRecordData(&num_saved_actors, sizeof(num_saved_actors));
				log::info("Reading saved actor outfits for {} actors", num_saved_actors);

				for (unsigned int actor_index = 0u; actor_index < num_saved_actors; actor_index++) {
					Actor* actor = NULL;
					FormID actor_form_id;
					serde->ReadRecordData(&actor_form_id, sizeof(actor_form_id));

					RE::FormID resolved_actor_form_id;
					if (!serde->ResolveFormID(actor_form_id, resolved_actor_form_id))
						log::error("Actor ID {:X} could not be found after loading the save.", actor_form_id);
					else {
						actor = TESForm::LookupByID<Actor>(resolved_actor_form_id);
						if (actor)
							log::info("Actor {}", actor->GetName());
						else
							log::error("Actor not found {:X}", resolved_actor_form_id);
					}

					Outfit outfit;

					unsigned int outfit_name_size;
					serde->ReadRecordData(&outfit_name_size, sizeof(outfit_name_size));
					char* outfit_name_c_str = new char[outfit_name_size + 1];
					serde->ReadRecordData(outfit_name_c_str, outfit_name_size);
					outfit_name_c_str[outfit_name_size] = '\0';

					unsigned int outfit_group_size;
					serde->ReadRecordData(&outfit_group_size, sizeof(outfit_group_size));
					char* outfit_group_c_str = new char[outfit_group_size + 1];
					serde->ReadRecordData(outfit_group_c_str, outfit_group_size);
					outfit_group_c_str[outfit_group_size] = '\0';
					
					outfit.name = outfit_name_c_str;
					outfit.groupName = outfit_group_c_str;
					delete[] outfit_name_c_str;
					delete[] outfit_group_c_str;
					log::info("Outfit {}.{}", outfit.groupName, outfit.name);

					unsigned int num_forms;
					serde->ReadRecordData(&num_forms, sizeof(num_forms));
					for (unsigned int i = 0u; i < num_forms; i++) {
						FormID form_id;
						serde->ReadRecordData(&form_id, sizeof(form_id));
						RE::FormID resolved_form_id;
						if (!serde->ResolveFormID(form_id, resolved_form_id))
							log::error("Outfit form {:X} could not be found after loading the save.", resolved_form_id);
						else {
							TESForm* form = TESForm::LookupByID<TESForm>(resolved_form_id);
							if (form) {
								log::info("Outfit form {}", form->GetName());
								outfit.forms.push_back(form);
							} else
								log::error("Outfit form not found {:X}", resolved_form_id);
						}
					}

					if (actor && !outfit.forms.empty())
						sActorEquippedOutfits[actor->formID] = outfit;
				}
			}
		}
	}

	void OnGameSaved(SKSE::SerializationInterface* serde)
	{
		log::info("Saving actor outfits");

		if (!serde->OpenRecord(OutfitPlaylistRecord, SAVE_VERSION)) {
			log::error("Unable to open record to write cosave data.");
			return;
		}

		unsigned int num_saved_actors = sActorEquippedOutfits.size();
		serde->WriteRecordData(&num_saved_actors, sizeof(num_saved_actors));
		for (ActorOutfitMap::iterator it = sActorEquippedOutfits.begin(); it != sActorEquippedOutfits.end(); ++it) {
			FormID a_fid = it->first;
			Outfit& outfit = it->second;
			serde->WriteRecordData(&a_fid, sizeof(a_fid));
			unsigned int outfit_name_size = std::strlen(outfit.name.c_str());
			serde->WriteRecordData(&outfit_name_size, sizeof(outfit_name_size));
			serde->WriteRecordData(outfit.name.c_str(), outfit_name_size);

			unsigned int outfit_group_size = std::strlen(outfit.groupName.c_str());
			serde->WriteRecordData(&outfit_group_size, sizeof(outfit_group_size));
			serde->WriteRecordData(outfit.groupName.c_str(), outfit_group_size);
			
			unsigned int num_forms = outfit.forms.size();
			serde->WriteRecordData(&num_forms, sizeof(num_forms));
			for (unsigned int i = 0u; i < outfit.forms.size(); i++)
				serde->WriteRecordData(&outfit.forms[i]->formID, sizeof(outfit.forms[i]->formID));
		}

		log::info("Saved actor outfits");
	}

	//Outfits

	Outfit* setOutfit(Actor* actor, unsigned int index)
	{
		if (!actor)
			return NULL;

		if (index < sOutfits.size()) {
			Outfit& outfit = sOutfits[index];
			
			log::info("Setting actor {} outfit to {}", actor->GetName(), outfit.name);

			sActorEquippedOutfits[actor->formID] = outfit;
			return &outfit;
		}

		return NULL;
	}

	void clearOutfit(Actor* actor, FormVec* forms_out)
	{
		if (!actor)
			return;

		log::info("Removing existing outfit");
		ActorOutfitMap::iterator it = sActorEquippedOutfits.find(actor->formID);
		if (it != sActorEquippedOutfits.end()) {
			if (forms_out)
				*forms_out = it->second.forms;

			sActorEquippedOutfits.erase(it);
		}
	}

	bool shuffleOutfits(unsigned int seed) {
		if (sShuffledSeed == seed && !sShuffledIndices.empty())
			return false;

		log::info("Shuffling outfits with seed {}", seed);

		std::mt19937 engine;
		engine.seed(seed);
		sShuffledIndices.clear();
		sShuffledIndices.reserve(sOutfits.size());

		IndexVec remaining_indices;
		remaining_indices.resize(sOutfits.size());
		for (unsigned int i = 0u; i < sOutfits.size(); i++)
			remaining_indices[i] = i;

		while (!remaining_indices.empty()) {
			unsigned int i = engine() % remaining_indices.size();
			sShuffledIndices.push_back(remaining_indices[i]);
			log::info("{}", remaining_indices[i]);

			if (remaining_indices.size() > 1u)
				remaining_indices[i] = remaining_indices[remaining_indices.size() - 1];
			remaining_indices.pop_back();
		}

		sShuffledSeed = seed;

		return true;
	}

	void serializeOutfitGroup(OutfitGroup& group, Json::Value& json_value) {
		json_value["outfits"] = Json::Value(Json::objectValue);
		Json::Value& outfit_dict = json_value["outfits"];

		for (unsigned int i = 0u; i < group.outfitIndices.size(); i++)
		{
			Outfit& outfit = sOutfits[group.outfitIndices[i]];
			std::string name = outfit.name;

			//Use a discriminator to make sure the outfit name is unique
			int discriminator = 0;
			while (outfit_dict.isMember(name)) {
				discriminator++;
				std::ostringstream string_stream;
				string_stream << outfit.name << '.' << std::setfill('0') << std::setw(3) << discriminator;
				name = string_stream.str();
			}

			outfit_dict[name] = Json::Value(Json::arrayValue);

			Json::Value& forms_list = outfit_dict[name];
			for (unsigned k = 0u; k < outfit.forms.size(); k++) {
				Json::Value form_id_json;
				SKSEUtil::serializeFormID(outfit.forms[k]->formID, form_id_json);
				forms_list.append(form_id_json);
			}
		}
	}

	void saveGroupFile(OutfitGroup& group) {
		Json::Value group_json;
		serializeOutfitGroup(group, group_json);

		std::string group_file_path = "Data/SKSE/Plugins/OutfitPlaylist/";
		group_file_path.append(group.name);
		group_file_path.append(".json");

		std::ofstream group_file(group_file_path);

		Json::StyledStreamWriter writer;
		writer.write(group_file, group_json);
	}

	bool generateOutfitFromWorn(Actor* actor, Outfit& outfit_out, bool apparel_only) {
		if (!actor)
			return false;

		SKSEUtil::FormSet worn_forms;
		SKSEUtil::GetWornForms(actor, &worn_forms);

		for (SKSEUtil::FormSet::iterator it = worn_forms.begin(); it != worn_forms.end(); ++it) {
			TESForm* form = *it;

			//Ignore non-apparel if specified
			if (apparel_only && form->formType != FormType::Armor)
				continue;

			//Ignore non-playable or ignored items
			if ((form->GetFormFlags() & 4) > 0 || sIgnoredFormIDs.contains(form->formID))
				continue;

			TESObjectARMO* armor = form->As<TESObjectARMO>();

			if (outfit_out.name.empty() || (armor && (static_cast<unsigned int>(armor->GetSlotMask()) & 4) > 0))
				outfit_out.name = armor->GetName();

			outfit_out.forms.push_back(form);
		}

		if (outfit_out.name.empty()) {
			if (outfit_out.forms.empty())
				outfit_out.name = "Naked";
			else
				outfit_out.name = "CustomOutfit";
		}

		return true;
	}

	int getOutfitIndex(const std::string& group_name, const std::string& outfit_name) {
		OutfitGroupMap::iterator it = sOutfitGroups.find(group_name);
		if (it != sOutfitGroups.end()) {
			//Case-sensitive
			for (unsigned int i = 0u; i < it->second.outfitIndices.size(); i++) {
				if (sOutfits[it->second.outfitIndices[i]].name == outfit_name)
					return static_cast<int>(it->second.outfitIndices[i]);
			}

			//Non case-sensitive search because Skyrim messes with string casing
			for (unsigned int i = 0u; i < it->second.outfitIndices.size(); i++) {
				if (SKSEUtil::nonCaseSensitiveEquals(outfit_name, sOutfits[it->second.outfitIndices[i]].name))
					return static_cast<int>(it->second.outfitIndices[i]);
			}
		}
		return -1;
	}

	Outfit* getActorEquippedOutfit(Actor* actor) {
		if (!actor)
			return NULL;

		ActorOutfitMap::iterator it = sActorEquippedOutfits.find(actor->formID);
		if (it != sActorEquippedOutfits.end()) {
			return &it->second;
		}
		
		return NULL;
	}

	std::string makeNameUniqueForGroup(OutfitGroup& group, const std::string& name, const std::string& ignore_name = std::string())
	{
		std::set<std::string> group_names;
		for (unsigned int i = 0u; i < group.outfitIndices.size(); i++) {
			if (sOutfits[group.outfitIndices[i]].name != ignore_name) {
				group_names.insert(SKSEUtil::toLowercaseString(sOutfits[group.outfitIndices[i]].name));
			}
		}

		//Use a discriminator to make sure the outfit name is unique
		std::string unique_name = name;
		int discriminator = 0;
		while (group_names.contains(SKSEUtil::toLowercaseString(unique_name))) {
			discriminator++;
			std::ostringstream string_stream;
			string_stream << name << '.' << std::setfill('0') << std::setw(3) << discriminator;
			unique_name = string_stream.str();
		}

		return unique_name;
	}

	bool outfitFormsAreTheSame(Outfit& outfit1, Outfit& outfit2) {
		if (outfit1.forms.size() != outfit2.forms.size())
			return false;

		for (unsigned int i = 0u; i < outfit1.forms.size(); i++) {
			bool found = false;
			for (unsigned int k = 0u; k < outfit2.forms.size(); k++) {
				if (outfit2.forms[k] == outfit1.forms[i]) {
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}

		return true;
	}

	//Papyrus

	int PapyrusGetNumOutfits(RE::StaticFunctionTag*) {
		return static_cast<int>(sOutfits.size());
	}

	std::vector<TESForm*> PapyrusGetOutfitForms(RE::StaticFunctionTag*, int index)
	{
		if (index < sOutfits.size())
			return sOutfits[index].forms;
		return std::vector<TESForm*>();
	}

	std::string PapyrusGetOutfitGroupName(RE::StaticFunctionTag*, int index)
	{
		if (index < sOutfits.size())
			return sOutfits[index].groupName;
		return std::string();
	}

	std::string PapyrusGetOutfitName(RE::StaticFunctionTag*, int index)
	{
		if (index < sOutfits.size())
			return sOutfits[index].name;
		return std::string();
	}

	int PapyrusGetOutfitIndex(RE::StaticFunctionTag*, std::string group_name, std::string outfit_name)
	{
		return getOutfitIndex(group_name, outfit_name);
	}

	std::vector<TESForm*> PapyrusSetOutfit(RE::StaticFunctionTag*, Actor* actor, int index)
	{
		Outfit* outfit = setOutfit(actor, index);
		if (outfit)
			return outfit->forms;
		return std::vector<TESForm*>();
	}

	std::vector<TESForm*> PapyrusClearOutfit(RE::StaticFunctionTag*, Actor* actor)
	{
		FormVec forms;
		clearOutfit(actor, &forms);
		return forms;
	}

	std::string PapyrusGetActorOutfitGroupName(RE::StaticFunctionTag*, Actor* actor)
	{
		Outfit* outfit = getActorEquippedOutfit(actor);
		if (outfit)
			return outfit->groupName;
		return std::string();
	}

	std::string PapyrusGetActorOutfitName(RE::StaticFunctionTag*, Actor* actor)
	{
		Outfit* outfit = getActorEquippedOutfit(actor);
		if (outfit)
			return outfit->name;
		return std::string();
	}

	int PapyrusGetShuffledOutfitIndex(RE::StaticFunctionTag*, int shuffle_index, int seed)
	{
		if (seed < 0)
			return shuffle_index % static_cast<int>(sOutfits.size());
		else {
			shuffleOutfits(seed);
			if (!sShuffledIndices.empty())
				return static_cast<int>(sShuffledIndices[shuffle_index % sShuffledIndices.size()]);
		}

		return 0;
	}

	int PapyrusGetOutfitShuffleIndex(RE::StaticFunctionTag*, int index, int seed)
	{
		if (seed < 0)
			return index;

		if (index < 0 || index > sOutfits.size())
			return 0;

		shuffleOutfits(seed);
		for (unsigned int i = 0u; i < sShuffledIndices.size(); i++) {
			if (sShuffledIndices[i] == static_cast<unsigned int>(index))
				return i;
		}

		return 0;
	}

	bool PapyrusRegisterCurrentOutfit(RE::StaticFunctionTag*, Actor* actor, std::string group_name, std::string outfit_name, bool apparel_only)
	{
		Outfit outfit;
		if (!generateOutfitFromWorn(actor, outfit, apparel_only))
			return false;

		if (!outfit_name.empty())
			outfit.name = outfit_name;

		//Get/add the custom outfit group
		OutfitGroupMap::iterator group_it = sOutfitGroups.find(group_name);
		if (group_it == sOutfitGroups.end()) {
			group_it = sOutfitGroups.insert(std::pair<std::string, OutfitGroup>(group_name, OutfitGroup())).first;
			group_it->second.name = group_name;
		}

		//Check if the outfit already exists in the group
		for (unsigned int i = 0u; i < group_it->second.outfitIndices.size(); i++) {
			Outfit& existing = sOutfits[group_it->second.outfitIndices[i]];
			if (outfitFormsAreTheSame(outfit, existing))
				return false;
		}

		//Add a new outfit and assign it to the group
		unsigned int outfit_index = sOutfits.size();
		outfit.name = makeNameUniqueForGroup(group_it->second, outfit.name);
		outfit.groupName = group_it->second.name;
		sOutfits.push_back(outfit);
		group_it->second.outfitIndices.push_back(outfit_index);

		saveGroupFile(group_it->second); //Save the group file
		setOutfit(actor, outfit_index); //Update the actor outfit

		return true;
	}
	
	bool PapyrusReplaceCurrentOutfit(RE::StaticFunctionTag*, Actor* actor, bool apparel_only) {
		Outfit outfit;
		if (!generateOutfitFromWorn(actor, outfit, apparel_only))
			return false;

		//Get current outfit for the actor
		Outfit* equipped_outfit = getActorEquippedOutfit(actor);
		if (!equipped_outfit)
			return false; //No current outfit

		OutfitGroupMap::iterator group_it = sOutfitGroups.find(equipped_outfit->groupName);
		if (group_it == sOutfitGroups.end())
			return false; //Group not found

		int outfit_index = getOutfitIndex(equipped_outfit->groupName, equipped_outfit->name);
		if (outfit_index < 0 || outfit_index >= sOutfits.size())
			return false; //Outfit not found

		sOutfits[outfit_index].forms = outfit.forms;  //Replace the formlist for the outfit
		saveGroupFile(group_it->second); //Save the group file
		setOutfit(actor, outfit_index); //Update the actor outfit

		return true;
	}

	bool PapyrusRenameCurrentOutfit(RE::StaticFunctionTag*, Actor* actor, std::string name)
	{
		//Get current outfit for the actor
		Outfit* equipped_outfit = getActorEquippedOutfit(actor);
		if (!equipped_outfit)
			return false; //No current outfit

		OutfitGroupMap::iterator group_it = sOutfitGroups.find(equipped_outfit->groupName);
		if (group_it == sOutfitGroups.end())
			return false; //Group not found

		int outfit_index = getOutfitIndex(equipped_outfit->groupName, equipped_outfit->name);
		if (outfit_index < 0 || outfit_index >= sOutfits.size())
			return false; //Outfit not found

		sOutfits[outfit_index].name = makeNameUniqueForGroup(group_it->second, name, equipped_outfit->name);  //Update the outfit name
		saveGroupFile(group_it->second); //Save the group file
		setOutfit(actor, outfit_index); //Update the actor outfit

		return true;
	}

	std::vector<std::string> PapyrusGetGroupNames(RE::StaticFunctionTag*)
	{
		std::vector<std::string> result;

		log::info("Get Group Names");

		for (OutfitGroupMap::iterator it = sOutfitGroups.begin(); it != sOutfitGroups.end(); ++it) {
			result.push_back(it->first);
		}

		return result;
	}

	std::vector<std::string> PapyrusGetGroupOutfitNames(RE::StaticFunctionTag*, std::string group_name)
	{
		std::vector<std::string> result;

		log::info("Get Group Outfit Names {}", group_name);

		OutfitGroupMap::iterator it = sOutfitGroups.find(group_name);
		if (it != sOutfitGroups.end()) {
			
			for (unsigned int i = 0u; i < it->second.outfitIndices.size(); i++) {
				result.push_back(sOutfits[it->second.outfitIndices[i]].name);
			}
		}
		return result;
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
	{
		log::info("Registered papyrus functions");

		vm->RegisterFunction("GetNumOutfits", OPLQuest, PapyrusGetNumOutfits);
		vm->RegisterFunction("GetOutfitForms", OPLQuest, PapyrusGetOutfitForms);
		vm->RegisterFunction("GetOutfitName", OPLQuest, PapyrusGetOutfitName);
		vm->RegisterFunction("GetOutfitGroupName", OPLQuest, PapyrusGetOutfitGroupName);
		vm->RegisterFunction("GetOutfitIndex", OPLQuest, PapyrusGetOutfitIndex);
		vm->RegisterFunction("GetActorOutfitGroupName", OPLQuest, PapyrusGetActorOutfitGroupName);
		vm->RegisterFunction("GetActorOutfitName", OPLQuest, PapyrusGetActorOutfitName);
		vm->RegisterFunction("ExtSetOutfit", OPLQuest, PapyrusSetOutfit);
		vm->RegisterFunction("ExtClearOutfit", OPLQuest, PapyrusClearOutfit);
		vm->RegisterFunction("GetShuffledOutfitIndex", OPLQuest, PapyrusGetShuffledOutfitIndex);
		vm->RegisterFunction("GetOutfitShuffleIndex", OPLQuest, PapyrusGetOutfitShuffleIndex);
		vm->RegisterFunction("RegisterCurrentOutfit", OPLQuest, PapyrusRegisterCurrentOutfit);
		vm->RegisterFunction("ReplaceCurrentOutfit", OPLQuest, PapyrusReplaceCurrentOutfit);
		vm->RegisterFunction("RenameCurrentOutfit", OPLQuest, PapyrusRenameCurrentOutfit);
		vm->RegisterFunction("GetGroupNames", OPLQuest, PapyrusGetGroupNames);
		vm->RegisterFunction("GetGroupOutfitNames", OPLQuest, PapyrusGetGroupOutfitNames);

		return true;
	}

}
