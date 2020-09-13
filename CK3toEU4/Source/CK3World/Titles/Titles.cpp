#include "Titles.h"
#include "../Characters/Characters.h"
#include "../Characters/Character.h"
#include "../CoatsOfArms/CoatsOfArms.h"
#include "LandedTitles.h"
#include "Log.h"
#include "ParserHelpers.h"
#include "Title.h"

CK3::Titles::Titles(std::istream& theStream)
{
	registerKeys();
	parseStream(theStream);
	clearRegisteredKeywords();
}

void CK3::Titles::registerKeys()
{
	registerKeyword("landed_titles", [this](const std::string& unused, std::istream& theStream) {
		// A bit of recursion is good for the soul.
		const auto& tempTitles = Titles(theStream);
		titles = tempTitles.getTitles();
		titleCounter = tempTitles.getCounter();
	});
	registerRegex(R"(\d+)", [this](const std::string& ID, std::istream& theStream) {
		// Incoming titles may not be actual titles but half-deleted junk.
		const auto& titleBlob = commonItems::singleItem(ID, theStream);
		if (titleBlob.find('{') != std::string::npos)
		{
			std::stringstream tempStream(titleBlob);
			auto newTitle = std::make_shared<Title>(tempStream, std::stoi(ID));
			if (!newTitle->getName().empty())
				titles.insert(std::pair(newTitle->getName(), newTitle));
			if (newTitle->getName().find("b_") == 0)
				++titleCounter[0];
			else if (newTitle->getName().find("c_") == 0)
				++titleCounter[1];
			else if (newTitle->getName().find("d_") == 0)
				++titleCounter[2];
			else if (newTitle->getName().find("k_") == 0)
				++titleCounter[3];
			else if (newTitle->getName().find("e_") == 0)
				++titleCounter[4];
		}
	});
	registerRegex(commonItems::catchallRegex, commonItems::ignoreItem);
}

void CK3::Titles::linkCoats(const CoatsOfArms& coats)
{
	auto counter = 0;
	const auto& coatData = coats.getCoats();
	for (const auto& title: titles)
	{
		if (!title.second->getCoA())
			continue;
		const auto& coatDataItr = coatData.find(title.second->getCoA()->first);
		if (coatDataItr != coatData.end())
		{
			title.second->loadCoat(*coatDataItr);
			++counter;
		}
		else
		{
			throw std::runtime_error("Title " + title.first + " has CoA " + std::to_string(title.second->getCoA()->first) + " which has no definition!");
		}
	}
	Log(LogLevel::Info) << "<> " << counter << " titles updated.";
}

void CK3::Titles::linkTitles()
{
	auto DFLcounter = 0;
	auto DJLcounter = 0;
	auto DFVcounter = 0;
	auto DJVcounter = 0;
	// We'll be needing a cache.
	std::map<int, std::shared_ptr<Title>> IDCache;
	for (const auto& title: titles)
		IDCache.insert(std::pair(title.second->getID(), title.second));
	
	// We'll also be needing a defacto vassal registry for later.
	std::map<int, std::map<int, std::shared_ptr<Title>>> dfvRegistry; // map<liegeTitleID, map<vassalTitleID, vassalTitle>>

	// Additional objective:
	// When a county and a duchy are held by same holder, under a king, both titles will have the kingdom set as defacto liege. We need to
	// untangle this mess and drop all vassals held by a person that also holds the vassal's dejure title as a vassal under same liege. :/

	for (const auto& title: titles)
	{
		// Self-loading - this is used for coalescing and congregating counties later.
		if (title.first.find("c_") == 0)
		{
			std::map<std::string, std::shared_ptr<Title>> self;
			self.insert(title);
			title.second->loadOwnedDFCounties(self); // every county defacto owns itself, by definition.
			title.second->loadOwnedDJCounties(self); // dejure as well.
		}

		// DFLiege
		if (title.second->getDFLiege())
		{
			const auto& cacheItr = IDCache.find(title.second->getDFLiege()->first);
			if (cacheItr != IDCache.end())
			{
				title.second->loadDFLiege(*cacheItr);
				dfvRegistry[title.second->getDFLiege()->first].insert(std::pair(title.second->getID(), title.second));
				++DFLcounter;
			}
			else
			{
				throw std::runtime_error(
					 "Title " + title.first + " has defacto liege " + std::to_string(title.second->getDFLiege()->first) + " which has no definition!");
			}
		}
		// DJLiege
		if (title.second->getDJLiege())
		{
			const auto& cacheItr = IDCache.find(title.second->getDJLiege()->first);
			if (cacheItr != IDCache.end())
			{
				title.second->loadDJLiege(*cacheItr);
				++DJLcounter;
			}
			else
			{
				throw std::runtime_error(
					 "Title " + title.first + " has dejure liege " + std::to_string(title.second->getDJLiege()->first) + " which has no definition!");
			}
		}
		// DJVassals
		if (!title.second->getDJVassals().empty())
		{
			std::map<int, std::shared_ptr<Title>> replacementMap;
			for (const auto& dJVassal: title.second->getDJVassals())
			{
				const auto& cacheItr = IDCache.find(dJVassal.first);
				if (cacheItr != IDCache.end())
				{
					replacementMap.insert(*cacheItr);
					++DJVcounter;
				}
				else
				{
					throw std::runtime_error("Title " + title.first + " has dejure vassal " + std::to_string(dJVassal.first) + " which has no definition!");
				}
			}
			title.second->loadDJVassals(replacementMap);
		}
	}
	
	// Now with fully generated dfvCache we can load it into all lieges, after fixing the mess inside.
	std::map<int, std::map<int, std::shared_ptr<Title>>> updateMap;
	for (const auto& liege: dfvRegistry)
	{
		const auto& cacheItr = IDCache.find(liege.first);
		if (cacheItr != IDCache.end())
		{
			// liege.second holds all "vassals". Let's find actual ones.
			std::map<int, std::shared_ptr<Title>> actualMap;
			for (const auto& vassal: liege.second)
			{
				auto djLiegeID = vassal.second->getDJLiege()->first;
				const auto& actualDJLiege = liege.second.find(djLiegeID);
				if (actualDJLiege != liege.second.end())
				{
					// this title's dejure liege is also under us. Redirect it.
					vassal.second->loadDFLiege(std::pair(actualDJLiege->first, actualDJLiege->second));
					// record this move so we can update new dfLiege
					updateMap[actualDJLiege->first].insert(vassal);
				}
				else
				{
					// this is a legitimate vassal.
					actualMap.insert(vassal);
				}
			}
			cacheItr->second->loadDFVassals(actualMap);
			DFVcounter += static_cast<int>(actualMap.size());
		}
		else
		{
			throw std::runtime_error("DJVCache corruption: " + std::to_string(liege.first) + " cannot be found!");
		}
	}
	// Finally update the dfvassal lists of all titles that gained new vassals.
	for (const auto& update: updateMap)
	{
		IDCache[update.first]->addDFVassals(update.second);
	}
	
	Log(LogLevel::Info) << "<> " << DFLcounter << " defacto lieges, " << DJLcounter << " dejure lieges, " << DFVcounter << " defacto vassals, " << DJVcounter
							  << " dejure vassals updated.";
}

void CK3::Titles::linkCharacters(const Characters& characters)
{
	auto holderCounter = 0;
	auto claimantCounter = 0;
	auto heirCounter = 0;
	auto electorCounter = 0;

	const auto& characterData = characters.getCharacters();
	for (const auto& title: titles)
	{
		// Holder
		if (title.second->getHolder())
		{
			const auto& characterDataItr = characterData.find(title.second->getHolder()->first);
			if (characterDataItr != characterData.end())
			{
				title.second->loadHolder(*characterDataItr);
				++holderCounter;
			}
			else
			{
				throw std::runtime_error("Title " + title.first + " has holder " + std::to_string(title.second->getHolder()->first) + " who has no definition!");
			}
		}
		// claimants
		std::map<int, std::shared_ptr<Character>> replacementMap;
		for (const auto& claimant: title.second->getClaimants())
		{
			const auto& characterDataItr = characterData.find(claimant.first);
			if (characterDataItr != characterData.end())
			{
				replacementMap.insert(*characterDataItr);
				++claimantCounter;
			}
			else
			{
				throw std::runtime_error("Title " + title.first + " has claimant " + std::to_string(claimant.first) + " who has no definition!");
			}
		}
		title.second->loadClaimants(replacementMap);

		// heirs
		std::vector<std::pair<int, std::shared_ptr<Character>>> replacementVector;
		for (const auto& heir: title.second->getHeirs())
		{
			const auto& characterDataItr = characterData.find(heir.first);
			if (characterDataItr != characterData.end())
			{
				replacementVector.emplace_back(*characterDataItr);
				++heirCounter;
			}
			else
			{
				// Dead person, save hadn't updated state yet.
			}
		}
		title.second->loadHeirs(replacementVector);

		// electors
		replacementMap.clear();
		for (const auto& elector: title.second->getElectors())
		{
			const auto& characterDataItr = characterData.find(elector.first);
			if (characterDataItr != characterData.end())
			{
				replacementMap.insert(*characterDataItr);
				++electorCounter;
			}
			else
			{
				throw std::runtime_error("Title " + title.first + " has elector " + std::to_string(elector.first) + " who has no definition!");
			}
		}
		title.second->loadElectors(replacementMap);
	}

	Log(LogLevel::Info) << "<> " << holderCounter << " holders, " << claimantCounter << " claimants, " << heirCounter << " heirs, " << electorCounter << " electors updated.";
}

void CK3::Titles::linkLandedTitles(const LandedTitles& landedTitles)
{
	// We're injecting clay towards titles instead the other way around.
	auto counter = 0;
	for (const auto& clay: landedTitles.getFoundTitles())
	{
		const auto& titleItr = titles.find(clay.first);
		if (titleItr != titles.end())
		{
			titleItr->second->loadClay(clay.second);
			++counter;
		}
		else
		{
			throw std::runtime_error("Clay for " + clay.first + " has no title definition!");
		}
	}
	Log(LogLevel::Info) << "<> " << counter << " titles updated.";
}