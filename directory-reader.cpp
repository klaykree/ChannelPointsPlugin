#include "directory-reader.hpp"
#include <Windows.h>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <random>

bool GetRandomFile(const char* Directory, const char* Extensions, struct dstr* RandomFile)
{
	if(Directory && Directory[0] != '\0')
	{
		std::unordered_set<std::string> ExtensionList;

		std::string ExtensionsStr = std::string(Extensions);
		size_t CurrIndex = 0;
		size_t NextIndex = 0;
		while(NextIndex = ExtensionsStr.find(";", CurrIndex))
		{
			ExtensionList.emplace(ExtensionsStr.substr(CurrIndex, NextIndex - CurrIndex));

			CurrIndex = NextIndex + 1;

			if(NextIndex == std::string::npos)
			{
				break;
			}
		}

		std::vector<std::wstring> Files;

		for(const auto& Entry : std::filesystem::directory_iterator(Directory))
		{
			if(ExtensionList.contains(Entry.path().extension().string()))
			{
				Files.push_back(Entry.path().generic_wstring());
			}
		}

		if(Files.empty())
		{
			return false;
		}

		std::random_device rd;
		std::uniform_int_distribution<size_t> dist(0, Files.size() - 1);
		size_t RandIndex = dist(rd);

		dstr_from_wcs(RandomFile, Files[RandIndex].c_str());

		return true;
	}

	return false;
}