#include "ContentManager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include "App.h"

namespace fs = std::filesystem;

ContentManager::ContentManager(IApp* appInterface) : appInterface(appInterface) {}

void ContentManager::LoadContents(const std::string& folderPath)
{
    try
    {
        if (!fs::exists(folderPath) || !fs::is_directory(folderPath))
        {
            std::cerr << "Directory does not exist: " << folderPath << std::endl;
            return;
        }

        
        LoadSequence(folderPath);

        LoadVideoContentFromFolder(folderPath);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading contents from folder: " << e.what() << std::endl;
    }
}

void ContentManager::LoadVideoContentFromFolder(const std::string& folderPath)
{
	videoContents.clear();

    try
    {
        //Validates that the provided path exists and is a directory
        if (!fs::exists(folderPath) || !fs::is_directory(folderPath))
        {
            std::cerr << "Directory does not exist: " << folderPath << std::endl;
            return;
        }

        //Iterates through every file inside the specified directory
        for (const auto& entry : fs::directory_iterator(folderPath))
        {
            //Checks if the entry is a file and has an .mp4 extension
            if (entry.is_regular_file() && entry.path().extension() == ".mp4")
            {
                VideoContent content;
                //Stores the full system path of the video file
                content.filename = entry.path().string();

                //Assigns a default fade-in duration
                content.fadeInDuration = 1.0f;
                //Assigns a default fade-out duration
                content.fadeOutDuration = 1.0f;
                //Sets the video to play once
                content.looped = false;

                //Creates a potential path for a matching CSV by swapping the extension
                fs::path csvPath = entry.path();
                csvPath.replace_extension(".csv");

                //Checks if a .csv file with the same name as the video exists
                if (fs::exists(csvPath))
                {
                    std::cout << "ContentManager: Found matching CSV for " << entry.path().filename() << std::endl;
                    //Calls the helper to parse CSV coordinates into the content object
                    LoadCSVPositions(content, csvPath.string());
                }

                //Adds the fully prepared video metadata to the list
                videoContents.push_back(content);
            }
        }

        //Searches for the first video containing "bg" in its name to serve as the background
        auto it = std::find_if(videoContents.begin(), videoContents.end(), [](const VideoContent& v)
            {
                return v.filename.find("bg") != std::string::npos;
            });

        // If a background video is found, moves it to the very front of the list
        if (it != videoContents.end())
        {
            std::rotate(videoContents.begin(), it, it + 1);
        }

        // Ensures there are at least two videos before applying specific slot behaviors
        if (videoContents.size() >= 2)
        {
            //Sets the background (index 0) to loop indefinitely.
            videoContents.at(0).looped = true;
            //Disables fade-in for the background for immediate playback
            videoContents.at(0).fadeInDuration = 0.0f;
            //Disables fade-out for the background
            videoContents.at(0).fadeOutDuration = 0.0f;

            //JUST FOR TESTING
            //Sets the first foreground candidate (index 1) to loop indefinitely
            //videoContents.at(1).looped = true;
            ////Disables fade-in for this foreground slot
            //videoContents.at(1).fadeInDuration = 2.0f;
            ////Disables fade-out for this foreground slot
            //videoContents.at(1).fadeOutDuration = 2.0f;
        }

        std::cout << "ContentManager: Loaded " << videoContents.size() << " videos." << std::endl;
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    }
}

const std::vector<VideoContent>& ContentManager::GetVideoContents() const
{
    return videoContents;
}

void ContentManager::LoadCSVPositions(VideoContent& content, const std::string& csvPath)
{
    // Attempts to open the CSV file at the provided file path.
    std::ifstream file(csvPath);

    if (!file.is_open())
        return;

    content.positions.clear();

    //Reads the file line by line until the end of the document is reached
    std::string line;
    while (std::getline(file, line))
    {
        //Wraps the current line in a stringstream to facilitate comma-based parsing
        std::stringstream ss(line);
        std::string value;

        //Breaks each line into individual strings using the comma (',') as a delimiter
        while (std::getline(ss, value, ','))
        {
            try
            {
                if (!value.empty())
                {
                    //Converts the string to a floating-point number
                    float pos = std::stof(value);
                    content.positions.push_back(pos);
                }
            }
            catch (...) {
                // Skip invalid numeric entries
            }
        }
    }
}

void ContentManager::LoadSequence(const std::string& folderPath)
{
    std::string sequencePath = "";

    // First pass: scan the folder to see if a sequence text file exists
    for (const auto& entry : fs::directory_iterator(folderPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
        {
            std::string filename = entry.path().filename().string();
            if (filename.find("sequence") != std::string::npos)
            {
                sequencePath = entry.path().string();
                break; // Found sequence definition file
            }
        }
    }

    if (sequencePath.empty())
        return;

    std::ifstream file(sequencePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open sequence file: " << sequencePath << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Trim or check for empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        std::stringstream ss(line);
        std::string videoName;

        // 1. Extract the first token up to the first comma (the video name)
        if (!std::getline(ss, videoName, ','))
            continue;

        // Helper lambda to trim whitespace from both ends of parsed strings
        auto trim = [](std::string& str) {
            str.erase(0, str.find_first_not_of(" \t\r\n"));
            str.erase(str.find_last_not_of(" \t\r\n") + 1);
            };

        trim(videoName);
        if (videoName.empty())
            continue;

        SequenceItem seqItem;
        seqItem.filename = videoName;

        // 2. Extract remaining properties separated by commas
        std::string propertyToken;
        while (std::getline(ss, propertyToken, ','))
        {
            trim(propertyToken);
            if (propertyToken.empty())
                continue;

            size_t assignmentIdx = propertyToken.find('=');
            if (assignmentIdx != std::string::npos)
            {
                std::string key = propertyToken.substr(0, assignmentIdx);
                std::string val = propertyToken.substr(assignmentIdx + 1);

                trim(key);
                trim(val);

                if (key == "fadeIn") {
                    seqItem.fadeInDuration = std::stof(val);
                }
                else if (key == "fadeOut") {
                    seqItem.fadeOutDuration = std::stof(val);
                }
                else if (key == "loop") {
                    seqItem.looped = (val == "true" || val == "1");
                }
            }
        }
        sequence.push_back(seqItem);

        appInterface->GetAppState().sequence = sequence;
        appInterface->GetAppState().currentSequenceIdx = -1; // Start before the first item
    }
}