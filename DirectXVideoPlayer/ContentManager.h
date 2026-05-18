#pragma once
#include <vector>
#include "customtypes.h"

class IApp;

class ContentManager
{
private:
	std::vector<VideoContent> videoContents;
	IApp* appInterface;

public:
	ContentManager(IApp *appInterface);
	~ContentManager() = default;

	// Scans the folder for .mp4 files and matching .csv position files
	void LoadVideoContentFromFolder(const std::string& folderPath);

	// Returns the list of loaded video content
	const std::vector<VideoContent>& GetVideoContents() const;

private:
	// Internal helper to parse position data from CSV files
	void LoadCSVPositions(VideoContent& content, const std::string& csvPath);
};

