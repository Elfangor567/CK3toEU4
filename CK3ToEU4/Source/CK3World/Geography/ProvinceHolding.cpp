#include "ProvinceHolding.h"
#include "CommonRegexes.h"
#include "Log.h"
#include "ParserHelpers.h"

CK3::ProvinceHolding::ProvinceHolding(std::istream& theStream)
{
	registerKeys();
	parseStream(theStream);
	clearRegisteredKeywords();
}

void CK3::ProvinceHolding::registerKeys()
{
	registerKeyword("holding", [this](const std::string& unused, std::istream& theStream) {
		// A bit of recursion is good for sanity.
		const auto& holdingDetails = ProvinceHolding(theStream);
		holdingType = holdingDetails.getHoldingType();
		buildings = holdingDetails.getBuildings();
		specialBuilding = holdingDetails.getSpecialBuilding();
	});
	registerKeyword("type", [this](const std::string& unused, std::istream& theStream) {
		holdingType = commonItems::singleString(theStream).getString();
	});
	registerKeyword("special_building_type", [this](const std::string& unused, std::istream& theStream) {
		specialBuilding = commonItems::singleString(theStream).getString();
	});
	registerKeyword("buildings", [this](const std::string& unused, std::istream& theStream) {
		// We'll get blobs, streamify them, and then again recursively parse them.
		const auto buildingBlobs = commonItems::blobList(theStream).getBlobs();
		for (const auto& blob: buildingBlobs)
		{
			auto blobStream = std::stringstream(blob);
			const auto newBuilding = ProvinceHolding(blobStream).getHoldingType();
			if (!newBuilding.empty())
				buildings.insert(newBuilding);
		}
	});
	registerRegex(commonItems::catchallRegex, commonItems::ignoreItem);
}

int CK3::ProvinceHolding::countBuildings() const
{
	int totalBuildings = 0;
	for (const auto& building: buildings)
	{
		const auto pos = building.find_last_of("_");
		if (pos == std::string::npos)
			continue;
		const auto buildingString = building.substr(pos + 1, building.size());
		try
		{
			totalBuildings += std::stoi(buildingString);
		}
		catch (std::exception&)
		{
			Log(LogLevel::Warning) << "Province building stoi fail: " << buildingString << " from " << building;
			totalBuildings++;
		}
	}

	if (specialBuilding.empty())
		return totalBuildings;
	else
		return totalBuildings + 1;
}
