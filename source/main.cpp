#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    
    #define NOGDI
    #define NOUSER
    
    #include <windows.h>
#endif

#include <filesystem>
#include <iostream>
#include <string.h>
#include <vector>
#include <string>
#include <float.h>
#include <math.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
#include <algorithm>
#include <thread>

#include "../include/raylib.h"
#include "../include/raymath.h"
#include "../include/rlgl.h"

extern "C" {
    #include "../include/tinyfiledialogs.h"
}

#include "../include/json.hpp"
#include "../include/bricknet/colors.h"
#include "../include/bricknet/part.h"
#include "../include/httplib.h"
#include "../include/bricknet/camera.h"
#include "../include/bricknet/presets/group_player.h"
#define DISCORDPP_IMPLEMENTATION
#include "../include/bricknet/discord.h"

enum WorkshopTool { TOOL_SELECT, TOOL_PLACE, TOOL_SCALE, TOOL_MOVE, TOOL_PAINT };

struct PartGroup {
    int id;
    std::string name;
    bool expanded;
};

struct SpawnedPart {
    Vector3 position;
    Vector3 size;
    Color color;
    BoundingBox bounds;
    std::string name;
    int groupId;
};

struct WorkspaceState {
    std::vector<SpawnedPart> parts;
    std::vector<PartGroup> groups;
    int nextGroupId;
};

struct ManipulatorHandle {
    Vector3 position;
    Vector3 axisDirection; 
    int sign;          
    Color color;
    int index;
};

std::vector<WorkspaceState> undoStack;
std::vector<WorkspaceState> redoStack;
std::string currentFileName = "";

bool skipInputFrame = false;

std::string GetFileNameFromPath(const std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string::npos) return path;
    return path.substr(lastSlash + 1);
}

void PushState(const std::vector<SpawnedPart>& parts, const std::vector<PartGroup>& groups, int nextId) {
    undoStack.push_back({parts, groups, nextId});
    redoStack.clear();
}

void SaveWorkspace(const std::vector<SpawnedPart>& parts, const std::vector<PartGroup>& groups, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "[\n";
    bool first = true;
    
    for (const auto& g : groups) {
        if (!first) file << ",\n";
        file << "  {\n    \"type\": \"group\",\n    \"id\": " << g.id << ",\n    \"name\": \"" << g.name << "\",\n    \"expanded\": " << (g.expanded ? 1 : 0) << "\n  }";
        first = false;
    }
    
    for (const auto& p : parts) {
        if (!first) file << ",\n";
        file << "  {\n    \"type\": \"part\",\n    \"groupId\": " << p.groupId << ",\n    \"name\": \"" << p.name << "\",\n    \"position\": [" << p.position.x << ", " << p.position.y << ", " << p.position.z << "],\n    \"size\": [" << p.size.x << ", " << p.size.y << ", " << p.size.z << "],\n    \"color\": [" << (int)p.color.r << ", " << (int)p.color.g << ", " << (int)p.color.b << ", " << (int)p.color.a << "]\n  }";
        first = false;
    }
    
    file << "\n]\n";
    file.close();
    currentFileName = GetFileNameFromPath(filename);
}

std::string TrimJSON(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\",:[]{}");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\",:[]{}");
    if (last == std::string::npos || last < first) return ""; 
    return str.substr(first, (last - first + 1));
}

void LoadWorkspace(std::vector<SpawnedPart>& parts, std::vector<PartGroup>& groups, int& nextId, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    parts.clear();
    groups.clear();
    nextId = 1;
    
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(file, line)) {
        if (line.find("},{") != std::string::npos || (line.find("type") != std::string::npos && line.find("\n") == std::string::npos && lines.size() == 0)) {
            std::stringstream ss(line);
            std::string segment;
            while (std::getline(ss, segment, '}')) {
                lines.push_back(segment + "}");
            }
        } else {
            lines.push_back(line);
        }
    }
    file.close();

    bool inObject = false;
    std::string curType = "part";
    SpawnedPart curPart;
    PartGroup curGroup;

    for (std::string& currentLine : lines) {
        if (currentLine.find("{") != std::string::npos) {
            inObject = true;
            curType = "part";
            curPart = { {0,0,0}, {1,1,1}, {255,255,255,255}, {0}, "", 0 };
            curGroup = { 0, "", true };
            if (currentLine.find(":") == std::string::npos) continue;
        }
        if (currentLine.find("}") != std::string::npos && inObject) {
            size_t colonPos = currentLine.find(":");
            if (colonPos != std::string::npos) {
                size_t startIdx = 0;
                while ((colonPos = currentLine.find(":", startIdx)) != std::string::npos) {
                    size_t lineStart = currentLine.find_last_of(",{\n", colonPos);
                    if (lineStart == std::string::npos) lineStart = startIdx;
                    
                    std::string key = TrimJSON(currentLine.substr(lineStart + 1, colonPos - lineStart - 1));
                    size_t lineEnd = currentLine.find_first_of(",}\n", colonPos);
                    std::string val = currentLine.substr(colonPos + 1, lineEnd - colonPos - 1);
                    
                    if (key == "type") curType = TrimJSON(val);
                    else if (key == "id") curGroup.id = std::stoi(TrimJSON(val));
                    else if (key == "groupId") curPart.groupId = std::stoi(TrimJSON(val));
                    else if (key == "name") { curPart.name = TrimJSON(val); curGroup.name = TrimJSON(val); }
                    else if (key == "expanded") curGroup.expanded = (std::stoi(TrimJSON(val)) != 0);
                    
                    startIdx = lineEnd;
                }
            }

            if (curType == "group") {
                groups.push_back(curGroup);
                if (curGroup.id >= nextId) nextId = curGroup.id + 1;
            } else {
                curPart.bounds = bn_part::GetPartBounds(curPart.position, curPart.size);
                parts.push_back(curPart);
            }
            inObject = false;
            continue;
        }

        if (!inObject) continue;

        size_t colonPos = currentLine.find(":");
        if (colonPos == std::string::npos) continue;

        std::string key = TrimJSON(currentLine.substr(0, colonPos));
        std::string val = currentLine.substr(colonPos + 1);

        if (key == "type") curType = TrimJSON(val);
        else if (key == "id") curGroup.id = std::stoi(TrimJSON(val));
        else if (key == "groupId") curPart.groupId = std::stoi(TrimJSON(val));
        else if (key == "name") { curPart.name = TrimJSON(val); curGroup.name = TrimJSON(val); }
        else if (key == "expanded") curGroup.expanded = (std::stoi(TrimJSON(val)) != 0);
        else if (key == "position" || key == "size" || key == "color") {
            size_t startBracket = val.find("[");
            size_t endBracket = val.find("]");
            if (startBracket != std::string::npos && endBracket != std::string::npos) {
                std::string arrayContent = val.substr(startBracket + 1, endBracket - startBracket - 1);
                std::stringstream ss(arrayContent);
                std::string item;
                std::vector<float> values;
                while (std::getline(ss, item, ',')) {
                    values.push_back(std::stof(item));
                }

                if (key == "position" && values.size() >= 3) curPart.position = { values[0], values[1], values[2] };
                else if (key == "size" && values.size() >= 3) curPart.size = { values[0], values[1], values[2] };
                else if (key == "color" && values.size() >= 4) curPart.color = { (unsigned char)values[0], (unsigned char)values[1], (unsigned char)values[2], (unsigned char)values[3] };
            }
        }
    }
    
    currentFileName = GetFileNameFromPath(filename);
}

void TriggerNativeSave(std::vector<SpawnedPart>& worldParts, std::vector<PartGroup>& worldGroups) {
#ifdef __linux__
    FILE* pipe = popen("zenity --file-selection --save --confirm-overwrite --title=\"Save Set\" --file-filter=\"*.set\"", "r");
    if (pipe) {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            std::string path(buffer);
            if (!path.empty() && path.back() == '\n') path.pop_back();
            if (!path.empty()) {
                if (path.size() < 4 || path.substr(path.size() - 4) != ".set") path += ".set";
                SaveWorkspace(worldParts, worldGroups, path);
            }
        }
        pclose(pipe);
    }
#else
    const char* filterPatterns[] = { "*.set" };
    const char* path = tinyfd_saveFileDialog("Save Set", "map.set", 1, filterPatterns, "Set Files (*.set)");
    if (path) {
        std::string pathStr(path);
        if (pathStr.size() < 4 || pathStr.substr(pathStr.size() - 4) != ".set") pathStr += ".set";
        SaveWorkspace(worldParts, worldGroups, pathStr);
    }
#endif
    skipInputFrame = true;
}

void TriggerNativeLoad(std::vector<SpawnedPart>& worldParts, std::vector<PartGroup>& worldGroups, int& nextGroupId, std::vector<int>& selectedParts, int& selectedGroup, bool& isDraggingHandle, std::vector<ManipulatorHandle>& toolHandles, int& renamingItemId) {
#ifdef __linux__
    FILE* pipe = popen("zenity --file-selection --title=\"Open Set\" --file-filter=\"*.set\"", "r");
    if (pipe) {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            std::string path(buffer);
            if (!path.empty() && path.back() == '\n') path.pop_back();
            if (!path.empty()) {
                LoadWorkspace(worldParts, worldGroups, nextGroupId, path);
                selectedParts.clear();
                selectedGroup = 0;
                isDraggingHandle = false;
                toolHandles.clear();
                undoStack.clear();
                redoStack.clear();
                renamingItemId = -1;
            }
        }
        pclose(pipe);
    }
#else
    const char* filterPatterns[] = { "*.set" };
    const char* path = tinyfd_openFileDialog("Open Set", "", 1, filterPatterns, "Set Files (*.set)", 0);
    if (path) {
        LoadWorkspace(worldParts, worldGroups, nextGroupId, path);
        selectedParts.clear();
        selectedGroup = 0;
        isDraggingHandle = false;
        toolHandles.clear();
        undoStack.clear();
        redoStack.clear();
        renamingItemId = -1;
    }
#endif

    skipInputFrame = true;
}

enum ExplorerItemType { EXP_PART, EXP_GROUP };
struct ExplorerItem {
    ExplorerItemType type;
    int indexOrId;
    int indent;
};

bool launchViaToken = false;
std::string mapDataPayload = "";

Texture hatTexture; 
Texture faceTexture;
Texture shirtTexture;
bn_mesh::LoadedMeshComponent playerHat;
std::string hatTexPath;
std::string hatMeshPath;
std::string faceTexPath;
std::string shirtTexPath;
Color larmCol;
Color rarmCol;
Color headCol;
Color torsoCol;
Color llegCol;
Color rlegCol;

std::string siteUrl = "http://brick-net.cc/";

bool DownloadAsset(const std::string& fullUrl, const std::string& assetPath, const std::string& savePath) {
    std::string url = fullUrl;
    if (url.back() == '/') url.pop_back();
    
    size_t schemeEnd = url.find("://");
    bool isHttps = url.substr(0, schemeEnd) == "https";
    
    std::string hostPort = url.substr(schemeEnd + 3);
    size_t portDelim = hostPort.find(':');
    std::string host = hostPort.substr(0, portDelim);
    
    int port = 80;
    if (isHttps) port = 443;
    if (portDelim != std::string::npos) {
        port = std::stoi(hostPort.substr(portDelim + 1));
    }
    
    std::string path = assetPath;
    if (path[0] != '/') {
        path = "/" + path;
    }
    
    std::cout << "parsed! host: '" << host << "' port: " << port << std::endl;
    std::cout << "requesting: " << path << std::endl;
    
    httplib::Client cli(host, port);
    
    if (auto res = cli.Get(path.c_str())) {
        if (res->status == 200) {
            std::ofstream ofs(savePath, std::ios::binary);
            ofs << res->body;
            ofs.close();
            return true;
        } else {
            std::cout << "download failed with http status: " << res->status << std::endl;
        }
    } else {
        std::cout << "failed to connect to server for download." << std::endl;
    }
    return false;
}

int main(int argc, char* argv[]) {
    bool isWorkshop = true;
    std::string autoLoadPath = "";
    std::filesystem::create_directories("assets/temp");

    if (argc > 1) {
        std::string launchArg(argv[1]);
        size_t tokenPos = launchArg.find("brick-net://launch/token=");
        
        if (tokenPos != std::string::npos) {
            std::string token = launchArg.substr(tokenPos + 25);
            
            httplib::Client cli(siteUrl);
            cli.set_follow_location(true);
            cli.set_connection_timeout(5, 0);
            std::string path = "/include/setApi?action=validate&token=" + token;
            std::cout << "requesting: http://brick-net.cc" << path << "\n";
            
            if (auto res = cli.Get(path)) {
                if (res->status == 200) {
                    auto j = nlohmann::json::parse(res->body);
    
                    if (j.contains("map_data")) {
                        mapDataPayload = j["map_data"].get<std::string>();
                    }

                    if (j.contains("avatar") && j["avatar"].is_object()) {
                        auto& av = j["avatar"];
                        
                        if (av.contains("hat") && av["hat"].is_object()) {
                            if (av["hat"].contains("texture") && av["hat"]["texture"].is_string()) {
                                hatTexPath = av["hat"]["texture"].get<std::string>();
                            }
                            if (av["hat"].contains("mesh") && av["hat"]["mesh"].is_string()) {
                                hatMeshPath = av["hat"]["mesh"].get<std::string>();
                            }
                        }
                        
                        if (av.contains("face") && av["face"].is_object()) {
                            if (av["face"].contains("texture") && av["face"]["texture"].is_string()) {
                                faceTexPath = av["face"]["texture"].get<std::string>();
                            }
                        }

                        if (av.contains("shirt") && av["shirt"].is_object()) {
                            if (av["shirt"].contains("texture") && av["shirt"]["texture"].is_string()) {
                                shirtTexPath = av["shirt"]["texture"].get<std::string>();
                            }
                        }
                        
                        if (av.contains("colors") && av["colors"].is_object()) {
                            auto& cols = av["colors"];
                            auto setCol = [&](std::string key, Color& target) {
                                if (cols.contains(key) && cols[key].is_array() && cols[key].size() >= 4) {
                                    target = { static_cast<unsigned char>((int)cols[key][0]), static_cast<unsigned char>((int)cols[key][1]), static_cast<unsigned char>((int)cols[key][2]), static_cast<unsigned char>((int)cols[key][3]) };
                                }
                            };
                            
                            setCol("head", headCol);
                            setCol("torso", torsoCol);
                            setCol("left_arm", larmCol);
                            setCol("right_arm", rarmCol);
                            setCol("left_leg", llegCol);
                            setCol("right_leg", rlegCol);
                        }
                    }
                    isWorkshop = false;
                    launchViaToken = true;
                }
            } else {
                std::cout << "couldnt connect to the site\n";
            }
        }
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "brick-net://workshop") == 0) {
            isWorkshop = true;
        } else if (strncmp(argv[i], "brick-net://launch/set=", 23) == 0) {
            isWorkshop = false;
            autoLoadPath = std::string(argv[i] + 23);
        }
    }

    int screenHeight = 700;
    int screenWidth = 900;
    
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Brick Net");
    InitAudioDevice();

    Image windowIcon = LoadImage("assets/textures/icon.png");
    SetWindowIcon(windowIcon);

    std::string localHatTexPath = "assets/temp/hat_tex.png";
    std::string localHatMeshPath = "assets/temp/hat_mesh.obj";
    std::string localShirtTexPath = "assets/temp/shirt.png";
    std::string localFaceTexPath = "assets/temp/face.png";

    if (DownloadAsset(siteUrl, hatTexPath, localHatTexPath) && DownloadAsset(siteUrl, hatMeshPath, localHatMeshPath)) {
        hatTexture = LoadTexture(localHatTexPath.c_str());
        playerHat = bn_mesh::LoadModelAsset(localHatMeshPath.c_str(), Vector3{0, 3.65f, 0}, Vector3{0.65f, 0.65f, 0.65f}, WHITE, hatTexture);
        
        std::filesystem::remove(localHatTexPath);
        std::filesystem::remove(localHatMeshPath);
    } else {
        std::cout << "failed to download hat assets!" << std::endl;
    }
    if (DownloadAsset(siteUrl, faceTexPath, localFaceTexPath)) {
        faceTexture = LoadTexture(localFaceTexPath.c_str());
        
        std::filesystem::remove(localFaceTexPath);
    } else {
        std::cout << "failed to download face asset!" << std::endl;
    }
    if (DownloadAsset(siteUrl, shirtTexPath, localShirtTexPath)) {
        shirtTexture = LoadTexture(localShirtTexPath.c_str());
        
        std::filesystem::remove(localShirtTexPath);
    } else {
        std::cout << "failed to download shirt asset!" << std::endl;
    }
    
    Texture studs = LoadTexture("assets/textures/stud.png");
    Texture inlet = LoadTexture("assets/textures/inlet.png");
    Texture blank = LoadTexture("assets/textures/blank.png");
    Texture face = faceTexture;
    Texture2D selectIcon = LoadTexture("assets/textures/select.png"); 
    Texture2D brickIcon = LoadTexture("assets/textures/block.png");
    Texture2D scaleIcon = LoadTexture("assets/textures/scale.png");
    Texture2D moveIcon = LoadTexture("assets/textures/move.png");
    Texture2D paintIcon = LoadTexture("assets/textures/paint.png");

    Model coneModel = LoadModel("assets/meshes/cone.obj");
    Sound walkSound = LoadSound("assets/sounds/walk.wav");
    Sound jumpSound = LoadSound("assets/sounds/jump.wav");
    Shader lighting = LoadShader("assets/shaders/lighting.vs", "assets/shaders/lighting.fs");
    Font font = LoadFontEx("assets/textures/verdana.ttf", 18, 0, 250);
    
    uint64_t appId = 1517957381538185216;
    DiscordManager discord(appId);

    if (!discord.Initialize()) {
        printf("failed to the run discord sdk!!");
        return 1;
    }

    discordpp::Client* client = discord.GetClient();
    client->SetApplicationId(appId);
    discordpp::Activity activity{};
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetDetails("Brick Net");
    discord.SetActivity(activity);

    SetTargetFPS(60);

    Color availableColors[] = {
        bn_colors::Red, bn_colors::Orange, bn_colors::Yellow, bn_colors::Green,
        bn_colors::DarkGreen, bn_colors::Lime, bn_colors::Blue, bn_colors::Purple,
        bn_colors::Pink, bn_colors::White, bn_colors::Grey, bn_colors::DarkGrey,
        bn_colors::Black, bn_colors::Brown, bn_colors::Tan, bn_colors::LightBlue, 
        bn_colors::Mint, bn_colors::DarkPurple, bn_colors::DarkRed
    };
    const char* colorNames[] = {
        "Red", "Orange", "Yellow", "Green", "Dark Green", "Lime", 
        "Blue", "Purple", "Pink", "White", "Grey", "Dark Grey", 
        "Black", "Brown","Tan", "Light Blue", "Mint", "Dark Purple", 
        "Dark Red"
    };
    int totalColors = 18;
    int activeColorIndex = 0; 
    bool isColorDropdownOpen = false;

    std::vector<SpawnedPart> worldParts;
    std::vector<PartGroup> worldGroups;
    int nextGroupId = 1;

    float sidebarScrollOffset = 0.0f;

    if (!autoLoadPath.empty()) {
        LoadWorkspace(worldParts, worldGroups, nextGroupId, autoLoadPath);
    } else {
        worldParts.push_back({ Vector3{0.0f, 0.0f, 0.0f}, Vector3{100.0f, 1.0f, 100.0f}, bn_colors::Green, bn_part::GetPartBounds(Vector3{0.0f, 0.0f, 0.0f}, Vector3{100.0f, 1.0f, 100.0f}), "Baseplate", 0 });
        worldParts.push_back({ Vector3{0.0f, 1.0f, 0.0f}, Vector3{2.0f, 1.0f, 4.0f}, bn_colors::Pink, bn_part::GetPartBounds(Vector3{0.0f, 1.0f, 0.0f}, Vector3{2.0f, 1.0f, 4.0f}), "", 0 });
    }

    Vector3 movement = {0.0f, 0.0f, 0.0f};
    Vector3 playerSize = Vector3{2.0f, 5.0f, 0.5f};
    float playerX = 0.0f;
    float playerZ = 4.0f;
    float playerY = 150.0f; 
    
    bn_group::BrickGroup playerGroup = group_player::createPlayerGroup(studs, inlet, playerHat, larmCol, rarmCol, torsoCol, headCol, llegCol, rlegCol);
    float moveSpeed = isWorkshop ? 0.4f : 0.35f;
    float playerVelocityY = 0.0f;
    const float gravity = -0.025f;
    const float jumpForce = 0.45f;
    bool isGrounded = false;
    float stepTargetY = 0.0f;
    bool stepping = false;
    const float stepSpeed = 0.15f;

    Vector3 buildSize = { 2.0f, 1.0f, 4.0f };
    Vector3 ghostPos = { 0.0f, 0.0f, 0.0f };
    bool showGhost = false;
    WorkshopTool currentTool = TOOL_SELECT;

    std::vector<int> selectedParts;
    int selectedGroup = 0;
    
    float handleRadius = 0.4f;
    float handleDistanceOffset = 0.6f; 
    std::vector<ManipulatorHandle> toolHandles;

    bool isDraggingHandle = false;
    ManipulatorHandle activeDragHandle = {0};
    std::vector<Vector3> initialPartPositions;
    Vector3 initialPartSize = {0};
    Vector2 initialMouseScreenPos = {0};
    Vector2 axisScreenDirection = {0};
    float pixelsPerWorldUnit = 25.0f; 

    float sidebarWidth = 250.0f;
    bool isDraggingSplitter = false;
    float topbarHeight = 80.0f;
    
    Rectangle selectToolSlotRec = { 10.0f, 6.5f, 65.0f, 65.0f };
    Rectangle buildToolSlotRec  = { 85.0f, 6.5f, 65.0f, 65.0f };
    Rectangle scaleToolSlotRec  = { 160.0f, 6.5f, 65.0f, 65.0f };
    Rectangle moveToolSlotRec   = { 235.0f, 6.5f, 65.0f, 65.0f };
    Rectangle paintToolSlotRec  = { 310.0f, 6.5f, 65.0f, 65.0f };
    
    Rectangle colorDropdownRec = { 395.0f, 16.5f, 150.0f, 45.0f };
    Rectangle saveBtnRec       = { 555.0f, 16.5f, 80.0f, 45.0f };
    Rectangle openBtnRec       = { 640.0f, 16.5f, 80.0f, 45.0f };

    ExplorerItemType renamingItemType = EXP_PART;
    int renamingItemId = -1;
    char renameBuffer[32] = "\0";
    float lastItemClickTime = 0.0f;
    const float doubleClickThreshold = 0.3f;

    bool isMouseOverSaveButton = false;
    bool isMouseOverOpenButton = false;

    float lastRpcUpdateTime = 0.0f;
    SetExitKey(KEY_NULL);

    Vector3 initialPos = isWorkshop ? (Vector3){0.0f, 5.0f, 10.0f} : (Vector3){playerX, playerY, playerZ};
    bn_camera::CustomCamera customCam = bn_camera::Create(initialPos);

    if (launchViaToken && !mapDataPayload.empty()) {
        
        std::string tempPath = "temp_runtime_map.set";
        
        std::ofstream tempFile(tempPath, std::ios::out | std::ios::binary);
        if (tempFile.is_open()) {
            tempFile << mapDataPayload;
            
            tempFile.flush(); 
            tempFile.close();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            LoadWorkspace(worldParts, worldGroups, nextGroupId, tempPath);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            if (std::filesystem::exists(tempPath)) {
                std::filesystem::remove(tempPath);
                std::cout << "cleaned up!\n";
            }
        }
        
        mapDataPayload.clear();
    }
    while (!WindowShouldClose()) {
        float currentTime = GetTime();
        discord.RunCallbacks();

         if (skipInputFrame) {
            skipInputFrame = false;
            BeginDrawing();
            EndDrawing();
            continue;
        }

        screenWidth = GetScreenWidth();
        screenHeight = GetScreenHeight();
        float viewWidth = isWorkshop ? ((float)screenWidth - sidebarWidth) : (float)screenWidth;
        
        Rectangle topbarBarRec = { 0.0f, 0.0f, (float)screenWidth, topbarHeight };
        Rectangle sidebarRec = { viewWidth, topbarHeight, sidebarWidth, (float)screenHeight - topbarHeight };
        Rectangle splitterRec = { viewWidth - 2.0f, topbarHeight, 4.0f, (float)screenHeight - topbarHeight };

        bool isMoving = IsKeyDown(KEY_W) || IsKeyDown(KEY_S) || IsKeyDown(KEY_A) || IsKeyDown(KEY_D);
        float deltaTime = GetFrameTime();
        Vector2 mousePoint2D = GetMousePosition();

        if (!isWorkshop) playerGroup.Tick(deltaTime, isMoving && isGrounded, isGrounded);

        movement = (Vector3){0.0f, 0.0f, 0.0f};
        Vector3 camForward = {0}, camRight = {0};
        bn_camera::GetMovementDirections(customCam, camForward, camRight, isWorkshop);

        if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, Vector3Scale(camForward, moveSpeed));
        if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, Vector3Scale(camForward, moveSpeed));
        if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, Vector3Scale(camRight, moveSpeed));
        if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, Vector3Scale(camRight, moveSpeed));

        if (isWorkshop) {
            if (IsKeyDown(KEY_E)) movement.y += moveSpeed;
            if (IsKeyDown(KEY_Q)) movement.y -= moveSpeed;
        }

        bn_camera::Update(customCam, playerGroup.GetPosition(), movement, isWorkshop);

        std::vector<ExplorerItem> explorerItems;
        if (isWorkshop) {
            for (size_t i = 0; i < worldGroups.size(); i++) {
                explorerItems.push_back({EXP_GROUP, worldGroups[i].id, 0});
                if (worldGroups[i].expanded) {
                    for (size_t j = 0; j < worldParts.size(); j++) {
                        if (worldParts[j].groupId == worldGroups[i].id) {
                            explorerItems.push_back({EXP_PART, (int)j, 1});
                        }
                    }
                }
            }
            for (size_t j = 0; j < worldParts.size(); j++) {
                if (worldParts[j].groupId == 0) explorerItems.push_back({EXP_PART, (int)j, 0});
            }

            bool isMouseOverTopbar = CheckCollisionPointRec(mousePoint2D, topbarBarRec);
            bool isMouseOverSelectSlot = CheckCollisionPointRec(mousePoint2D, selectToolSlotRec);
            bool isMouseOverBuildSlot = CheckCollisionPointRec(mousePoint2D, buildToolSlotRec);
            bool isMouseOverScaleSlot = CheckCollisionPointRec(mousePoint2D, scaleToolSlotRec);
            bool isMouseOverMoveSlot = CheckCollisionPointRec(mousePoint2D, moveToolSlotRec);
            bool isMouseOverPaintSlot = CheckCollisionPointRec(mousePoint2D, paintToolSlotRec);
            bool isMouseOverDropdownButton = CheckCollisionPointRec(mousePoint2D, colorDropdownRec);
            isMouseOverSaveButton = CheckCollisionPointRec(mousePoint2D, saveBtnRec);
            isMouseOverOpenButton = CheckCollisionPointRec(mousePoint2D, openBtnRec);
            bool isMouseOverSidebar = CheckCollisionPointRec(mousePoint2D, sidebarRec);
            bool isMouseOverSplitter = CheckCollisionPointRec(mousePoint2D, splitterRec);

            float itemHeight = 30.0f;
            float totalContentHeight = (float)explorerItems.size() * itemHeight;
            float visibleAreaHeight = (float)screenHeight - topbarHeight - 20.0f;

            if (isMouseOverSidebar) {
                float wheel = GetMouseWheelMove();
                sidebarScrollOffset -= wheel * 30.0f;
                if (sidebarScrollOffset < 0) sidebarScrollOffset = 0;
                if (totalContentHeight > visibleAreaHeight) {
                    if (sidebarScrollOffset > totalContentHeight - visibleAreaHeight) sidebarScrollOffset = totalContentHeight - visibleAreaHeight;
                } else sidebarScrollOffset = 0;
            }

            bool isMouseOverDropdownExtended = false;
            if (isColorDropdownOpen) {
                Rectangle extendedRec = { colorDropdownRec.x, colorDropdownRec.y + colorDropdownRec.height, colorDropdownRec.width, (float)(totalColors * 30) };
                isMouseOverDropdownExtended = CheckCollisionPointRec(mousePoint2D, extendedRec);
            }

            if (isMouseOverSplitter || isDraggingSplitter) {
                SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) isDraggingSplitter = true;
            } else SetMouseCursor(MOUSE_CURSOR_DEFAULT);

            if (isDraggingSplitter) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    sidebarWidth = (float)screenWidth - mousePoint2D.x;
                    if (sidebarWidth < 150.0f) sidebarWidth = 150.0f;
                    if (sidebarWidth > (float)screenWidth * 0.5f) sidebarWidth = (float)screenWidth * 0.5f;
                } else isDraggingSplitter = false;
            }

            if (renamingItemId != -1) {
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= 32) && (key <= 125) && (strlen(renameBuffer) < 31)) {
                        int len = strlen(renameBuffer);
                        renameBuffer[len] = (char)key;
                        renameBuffer[len + 1] = '\0';
                    }
                    key = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    int len = strlen(renameBuffer);
                    if (len > 0) renameBuffer[len - 1] = '\0';
                }
                if (IsKeyPressed(KEY_ENTER)) {
                    PushState(worldParts, worldGroups, nextGroupId);
                    if (renamingItemType == EXP_PART) worldParts[renamingItemId].name = renameBuffer;
                    else {
                        for (auto& g : worldGroups) {
                            if (g.id == renamingItemId) { g.name = renameBuffer; break; }
                        }
                    }
                    renamingItemId = -1;
                }
            }

            if (!isMouseOverSidebar && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && renamingItemId != -1) {
                PushState(worldParts, worldGroups, nextGroupId);
                if (renamingItemType == EXP_PART) worldParts[renamingItemId].name = renameBuffer;
                else {
                    for (auto& g : worldGroups) {
                        if (g.id == renamingItemId) { g.name = renameBuffer; break; }
                    }
                }
                renamingItemId = -1;
            }

            if (renamingItemId == -1) {
                if (IsKeyDown(KEY_LEFT_CONTROL)) {
                    if (IsKeyPressed(KEY_Z)) {
                        if (!undoStack.empty()) {
                            redoStack.push_back({worldParts, worldGroups, nextGroupId});
                            worldParts = undoStack.back().parts;
                            worldGroups = undoStack.back().groups;
                            nextGroupId = undoStack.back().nextGroupId;
                            undoStack.pop_back();
                            selectedParts.clear(); selectedGroup = 0;
                            isDraggingHandle = false; toolHandles.clear();
                        }
                    }
                    else if (IsKeyPressed(KEY_Y)) {
                        if (!redoStack.empty()) {
                            undoStack.push_back({worldParts, worldGroups, nextGroupId});
                            worldParts = redoStack.back().parts;
                            worldGroups = redoStack.back().groups;
                            nextGroupId = redoStack.back().nextGroupId;
                            redoStack.pop_back();
                            selectedParts.clear(); selectedGroup = 0;
                            isDraggingHandle = false; toolHandles.clear();
                        }
                    }
                    else if (IsKeyPressed(KEY_S)) TriggerNativeSave(worldParts, worldGroups);
                    else if (IsKeyPressed(KEY_O)) TriggerNativeLoad(worldParts, worldGroups, nextGroupId, selectedParts, selectedGroup, isDraggingHandle, toolHandles, renamingItemId);
                    else if (IsKeyPressed(KEY_G)) {
                        if (selectedParts.size() > 0 && selectedGroup == 0) {
                            PushState(worldParts, worldGroups, nextGroupId);
                            PartGroup newGroup = { nextGroupId++, "Model", true };
                            worldGroups.push_back(newGroup);
                            for (int idx : selectedParts) worldParts[idx].groupId = newGroup.id;
                            selectedGroup = newGroup.id;
                        }
                    }
                }

                if (IsKeyPressed(KEY_BACKSPACE)) {
                    renamingItemId = -1; 
                    if (selectedGroup != 0) {
                        PushState(worldParts, worldGroups, nextGroupId);
                        worldGroups.erase(std::remove_if(worldGroups.begin(), worldGroups.end(), [&](const PartGroup& g){ return g.id == selectedGroup; }), worldGroups.end());
                        worldParts.erase(std::remove_if(worldParts.begin(), worldParts.end(), [&](const SpawnedPart& p){ return p.groupId == selectedGroup; }), worldParts.end());
                        selectedGroup = 0;
                        selectedParts.clear();
                        isDraggingHandle = false; toolHandles.clear();
                    } else if (!selectedParts.empty()) {
                        PushState(worldParts, worldGroups, nextGroupId);
                        std::vector<int> sortedDelete = selectedParts;
                        std::sort(sortedDelete.rbegin(), sortedDelete.rend());
                        for (int idx : sortedDelete) worldParts.erase(worldParts.begin() + idx);
                        selectedParts.clear();
                        isDraggingHandle = false; toolHandles.clear();
                    }
                }

                if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_D)) {
                    if (selectedGroup != 0) {
                        PushState(worldParts, worldGroups, nextGroupId);
                        PartGroup dupGroup = { nextGroupId++, "Model", true };
                        for (auto& g : worldGroups) if (g.id == selectedGroup) { dupGroup.name = g.name; break; }
                        worldGroups.push_back(dupGroup);

                        std::vector<int> newSelection;
                        for (int idx : selectedParts) {
                            SpawnedPart s = worldParts[idx];
                            s.position.y += s.size.y; 
                            s.groupId = dupGroup.id;
                            s.bounds = bn_part::GetPartBounds(s.position, s.size);
                            worldParts.push_back(s);
                            newSelection.push_back((int)worldParts.size() - 1);
                        }
                        selectedGroup = dupGroup.id;
                        selectedParts = newSelection;
                    } else if (selectedParts.size() == 1) {
                        PushState(worldParts, worldGroups, nextGroupId);
                        SpawnedPart sourcePart = worldParts[selectedParts[0]];
                        Vector3 duplicatePos = sourcePart.position;
                        duplicatePos.y += sourcePart.size.y;
                        worldParts.push_back({ duplicatePos, sourcePart.size, sourcePart.color, bn_part::GetPartBounds(duplicatePos, sourcePart.size), sourcePart.name, 0 });
                        selectedParts.clear();
                        selectedParts.push_back((int)worldParts.size() - 1);
                    }
                }
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isColorDropdownOpen) {
                    for (int i = 0; i < totalColors; i++) {
                        Rectangle itemRec = { colorDropdownRec.x, colorDropdownRec.y + colorDropdownRec.height + (i * 30), colorDropdownRec.width, 30.0f };
                        if (CheckCollisionPointRec(mousePoint2D, itemRec)) {
                            activeColorIndex = i;
                            if (!selectedParts.empty()) {
                                PushState(worldParts, worldGroups, nextGroupId);
                                for (int idx : selectedParts) worldParts[idx].color = availableColors[activeColorIndex];
                            }
                            break;
                        }
                    }
                    isColorDropdownOpen = false;
                } 
                else if (isMouseOverDropdownButton) isColorDropdownOpen = true;
                else if (isMouseOverSaveButton) TriggerNativeSave(worldParts, worldGroups);
                else if (isMouseOverOpenButton) TriggerNativeLoad(worldParts, worldGroups, nextGroupId, selectedParts, selectedGroup, isDraggingHandle, toolHandles, renamingItemId);
                else if (isMouseOverSelectSlot) { currentTool = TOOL_SELECT; isDraggingHandle = false; }
                else if (isMouseOverBuildSlot) { currentTool = TOOL_PLACE; selectedParts.clear(); selectedGroup = 0; isDraggingHandle = false; }
                else if (isMouseOverScaleSlot) { currentTool = TOOL_SCALE; isDraggingHandle = false; }
                else if (isMouseOverMoveSlot) { currentTool = TOOL_MOVE; isDraggingHandle = false; }
                else if (isMouseOverPaintSlot) { currentTool = TOOL_PAINT; selectedParts.clear(); selectedGroup = 0; isDraggingHandle = false; }
            }

            if (currentTool == TOOL_PLACE && IsKeyPressed(KEY_R)) {
                float temp = buildSize.x; buildSize.x = buildSize.z; buildSize.z = temp;
            }

            showGhost = false;
            
            toolHandles.clear();
            if ((currentTool == TOOL_SELECT || currentTool == TOOL_SCALE || currentTool == TOOL_MOVE) && !selectedParts.empty()) {
                if (currentTool == TOOL_SCALE && selectedParts.size() > 1) {
                } else {
                    Vector3 minB = { FLT_MAX, FLT_MAX, FLT_MAX };
                    Vector3 maxB = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                    for (int idx : selectedParts) {
                        BoundingBox b = worldParts[idx].bounds;
                        minB.x = fmin(minB.x, b.min.x); minB.y = fmin(minB.y, b.min.y); minB.z = fmin(minB.z, b.min.z);
                        maxB.x = fmax(maxB.x, b.max.x); maxB.y = fmax(maxB.y, b.max.y); maxB.z = fmax(maxB.z, b.max.z);
                    }
                    Vector3 aggPos = { (minB.x + maxB.x)/2.0f, (minB.y + maxB.y)/2.0f, (minB.z + maxB.z)/2.0f };
                    Vector3 aggSize = { maxB.x - minB.x, maxB.y - minB.y, maxB.z - minB.z };

                    Vector3 hxs = {1, 0, 0}, hys = {0, 1, 0}, hzs = {0, 0, 1};
                    float offsetX = (aggSize.x * 0.5f) + handleDistanceOffset;
                    float offsetY = (aggSize.y * 0.5f) + handleDistanceOffset;
                    float offsetZ = (aggSize.z * 0.5f) + handleDistanceOffset;

                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hxs, offsetX)), hxs, 1, RED, 0 });
                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hxs, -offsetX)), hxs, -1, RED, 1 });
                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hys, offsetY)), hys, 1, GREEN, 2 });
                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hys, -offsetY)), hys, -1, GREEN, 3 });
                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hzs, offsetZ)), hzs, 1, BLUE, 4 });
                    toolHandles.push_back({ Vector3Add(aggPos, Vector3Scale(hzs, -offsetZ)), hzs, -1, BLUE, 5 });
                }
            }

            if (isMouseOverSidebar) {
                float itemStartY = sidebarRec.y + 10.0f - sidebarScrollOffset;
                for (size_t i = 0; i < explorerItems.size(); i++) {
                    Rectangle itemRec = { sidebarRec.x + 10.0f + (explorerItems[i].indent * 15.0f), itemStartY + (i * 30.0f), sidebarWidth - 25.0f - (explorerItems[i].indent * 15.0f), 25.0f };
                    
                    if (itemRec.y + itemRec.height > sidebarRec.y && itemRec.y < sidebarRec.y + screenHeight - topbarHeight) {
                        if (CheckCollisionPointRec(mousePoint2D, itemRec) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            float currentTime = GetTime();
                            bool doubleClicked = (currentTime - lastItemClickTime < doubleClickThreshold);
                            
                            if (explorerItems[i].type == EXP_GROUP) {
                                Rectangle arrowRec = { itemRec.x, itemRec.y, 20.0f, itemRec.height };
                                if (CheckCollisionPointRec(mousePoint2D, arrowRec)) {
                                    for (auto& g : worldGroups) if (g.id == explorerItems[i].indexOrId) { g.expanded = !g.expanded; break; }
                                } else {
                                    if (doubleClicked && selectedGroup == explorerItems[i].indexOrId) {
                                        renamingItemType = EXP_GROUP;
                                        renamingItemId = explorerItems[i].indexOrId;
                                        std::string n;
                                        for (auto& g : worldGroups) if (g.id == renamingItemId) { n = g.name; break; }
                                        strcpy(renameBuffer, n.c_str());
                                    } else {
                                        selectedGroup = explorerItems[i].indexOrId;
                                        selectedParts.clear();
                                        for (size_t j = 0; j < worldParts.size(); j++) if (worldParts[j].groupId == selectedGroup) selectedParts.push_back(j);
                                    }
                                }
                            } else {
                                if (doubleClicked && std::find(selectedParts.begin(), selectedParts.end(), explorerItems[i].indexOrId) != selectedParts.end()) {
                                    renamingItemType = EXP_PART;
                                    renamingItemId = explorerItems[i].indexOrId;
                                    std::string n = worldParts[renamingItemId].name.empty() ? ("Part " + std::to_string(renamingItemId)) : worldParts[renamingItemId].name;
                                    strcpy(renameBuffer, n.c_str());
                                } else {
                                    if (IsKeyDown(KEY_LEFT_SHIFT)) {
                                        selectedGroup = 0;
                                        int targetIdx = explorerItems[i].indexOrId;
                                        auto it = std::find(selectedParts.begin(), selectedParts.end(), targetIdx);
                                        if (it != selectedParts.end()) selectedParts.erase(it);
                                        else selectedParts.push_back(targetIdx);
                                    } else {
                                        int pGroupId = worldParts[explorerItems[i].indexOrId].groupId;
                                        if (pGroupId != 0) {
                                            selectedGroup = pGroupId;
                                            selectedParts.clear();
                                            for (size_t j = 0; j < worldParts.size(); j++) if (worldParts[j].groupId == selectedGroup) selectedParts.push_back(j);
                                        } else {
                                            selectedGroup = 0;
                                            selectedParts.clear();
                                            selectedParts.push_back(explorerItems[i].indexOrId);
                                            Color c = worldParts[selectedParts[0]].color;
                                            for (int x = 0; x < totalColors; x++) {
                                                if (availableColors[x].r == c.r && availableColors[x].g == c.g && availableColors[x].b == c.b) { activeColorIndex = x; break; }
                                            }
                                        }
                                    }
                                }
                            }
                            lastItemClickTime = currentTime;
                            break;
                        }
                    }
                }
            }

            if (!isMouseOverTopbar && !isMouseOverDropdownExtended && !isMouseOverSidebar && !isDraggingSplitter) {
                Ray ray = GetScreenToWorldRay(mousePoint2D, customCam.raylibCamera);

                if (currentTool == TOOL_PLACE) {
                    float closestHitDistance = FLT_MAX;
                    for (const auto& part : worldParts) {
                        RayCollision collision = GetRayCollisionBox(ray, part.bounds);
                        if (collision.hit && collision.distance < closestHitDistance) {
                            closestHitDistance = collision.distance;
                            Vector3 rawTarget;
                            rawTarget.x = collision.point.x + (collision.normal.x * (buildSize.x * 0.5f));
                            rawTarget.y = collision.point.y + (collision.normal.y * (buildSize.y * 0.5f));
                            rawTarget.z = collision.point.z + (collision.normal.z * (buildSize.z * 0.5f));
                            ghostPos.x = roundf(rawTarget.x);
                            ghostPos.y = roundf(rawTarget.y);
                            ghostPos.z = roundf(rawTarget.z);
                            showGhost = true;
                        }
                    }

                    if (showGhost && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        PushState(worldParts, worldGroups, nextGroupId);
                        worldParts.push_back({ ghostPos, buildSize, availableColors[activeColorIndex], bn_part::GetPartBounds(ghostPos, buildSize), "", 0 });
                    }
                } 
                else if (currentTool == TOOL_PAINT) {
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        float closestHitDistance = FLT_MAX;
                        int hitIndex = -1;
                        for (size_t i = 0; i < worldParts.size(); i++) {
                            RayCollision collision = GetRayCollisionBox(ray, worldParts[i].bounds);
                            if (collision.hit && collision.distance < closestHitDistance) {
                                closestHitDistance = collision.distance;
                                hitIndex = (int)i;
                            }
                        }
                        if (hitIndex >= 0) {
                            PushState(worldParts, worldGroups, nextGroupId);
                            if (worldParts[hitIndex].groupId != 0) {
                                for (auto& p : worldParts) if (p.groupId == worldParts[hitIndex].groupId) p.color = availableColors[activeColorIndex];
                            } else {
                                worldParts[hitIndex].color = availableColors[activeColorIndex];
                            }
                        }
                    }
                }
                else if (currentTool == TOOL_SELECT || currentTool == TOOL_SCALE || currentTool == TOOL_MOVE) {
                    if (isDraggingHandle) {
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                            Vector2 currentMousePos = GetMousePosition();
                            Vector2 mouseDelta = Vector2Subtract(currentMousePos, initialMouseScreenPos);
                            float dragDot = (mouseDelta.x * axisScreenDirection.x + mouseDelta.y * axisScreenDirection.y);
                            int deltaUnits = (int)roundf(dragDot / pixelsPerWorldUnit);

                            if (currentTool == TOOL_SCALE && selectedParts.size() == 1) {
                                auto& part = worldParts[selectedParts[0]];
                                Vector3 nextSize = initialPartSize;
                                Vector3 nextPos = initialPartPositions[0];

                                if (activeDragHandle.axisDirection.x > 0.5f) {
                                    nextSize.x += deltaUnits; if (nextSize.x < 1.0f) nextSize.x = 1.0f;
                                    nextPos.x += ((nextSize.x - initialPartSize.x) * 0.5f) * activeDragHandle.sign;
                                } else if (activeDragHandle.axisDirection.y > 0.5f) {
                                    nextSize.y += deltaUnits; if (nextSize.y < 1.0f) nextSize.y = 1.0f;
                                    nextPos.y += ((nextSize.y - initialPartSize.y) * 0.5f) * activeDragHandle.sign;
                                } else if (activeDragHandle.axisDirection.z > 0.5f) {
                                    nextSize.z += deltaUnits; if (nextSize.z < 1.0f) nextSize.z = 1.0f;
                                    nextPos.z += ((nextSize.z - initialPartSize.z) * 0.5f) * activeDragHandle.sign;
                                }
                                part.size = nextSize; part.position = nextPos;
                                part.bounds = bn_part::GetPartBounds(part.position, part.size);
                            } 
                            else if (currentTool == TOOL_MOVE || currentTool == TOOL_SELECT) {
                                Vector3 translation = {0};
                                if (activeDragHandle.axisDirection.x > 0.5f) translation.x += deltaUnits * activeDragHandle.sign;
                                else if (activeDragHandle.axisDirection.y > 0.5f) translation.y += deltaUnits * activeDragHandle.sign;
                                else if (activeDragHandle.axisDirection.z > 0.5f) translation.z += deltaUnits * activeDragHandle.sign;
                                
                                for (size_t i = 0; i < selectedParts.size(); i++) {
                                    int idx = selectedParts[i];
                                    worldParts[idx].position = Vector3Add(initialPartPositions[i], translation);
                                    worldParts[idx].bounds = bn_part::GetPartBounds(worldParts[idx].position, worldParts[idx].size);
                                }
                            }
                        } else isDraggingHandle = false;
                    } 
                    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        bool handleClicked = false;
                        for (const auto& handle : toolHandles) {
                            RayCollision collision = GetRayCollisionSphere(ray, handle.position, handleRadius);
                            if (collision.hit) {
                                PushState(worldParts, worldGroups, nextGroupId);
                                isDraggingHandle = true;
                                activeDragHandle = handle;
                                
                                initialPartPositions.clear();
                                for (int idx : selectedParts) initialPartPositions.push_back(worldParts[idx].position);
                                if (selectedParts.size() == 1) initialPartSize = worldParts[selectedParts[0]].size;

                                initialMouseScreenPos = GetMousePosition();
                                Vector2 baseScreenPos = GetWorldToScreen(handle.position, customCam.raylibCamera);
                                Vector3 offsetWorldPos = Vector3Add(handle.position, Vector3Scale(handle.axisDirection, 1.0f * handle.sign));
                                Vector2 targetScreenPos = GetWorldToScreen(offsetWorldPos, customCam.raylibCamera);
                                Vector2 directionVec = Vector2Subtract(targetScreenPos, baseScreenPos);
                                axisScreenDirection = Vector2Normalize(directionVec);
                                float screenSpan = Vector2Length(directionVec);
                                pixelsPerWorldUnit = (screenSpan > 2.0f) ? screenSpan : 25.0f;
                                handleClicked = true;
                                break;
                            }
                        }

                        if (!handleClicked) {
                            float closestHitDistance = FLT_MAX;
                            int intermediateSelection = -1;
                            for (size_t i = 0; i < worldParts.size(); i++) {
                                RayCollision collision = GetRayCollisionBox(ray, worldParts[i].bounds);
                                if (collision.hit && collision.distance < closestHitDistance) {
                                    closestHitDistance = collision.distance;
                                    intermediateSelection = (int)i;
                                }
                            }
                            
                            if (intermediateSelection >= 0) {
                                if (IsKeyDown(KEY_LEFT_SHIFT) && worldParts[intermediateSelection].groupId == 0) {
                                    selectedGroup = 0;
                                    auto it = std::find(selectedParts.begin(), selectedParts.end(), intermediateSelection);
                                    if (it != selectedParts.end()) selectedParts.erase(it);
                                    else selectedParts.push_back(intermediateSelection);
                                } else {
                                    int pGroupId = worldParts[intermediateSelection].groupId;
                                    if (pGroupId != 0) {
                                        selectedGroup = pGroupId;
                                        selectedParts.clear();
                                        for (size_t j = 0; j < worldParts.size(); j++) if (worldParts[j].groupId == selectedGroup) selectedParts.push_back(j);
                                    } else {
                                        selectedGroup = 0;
                                        selectedParts.clear();
                                        selectedParts.push_back(intermediateSelection);
                                        Color c = worldParts[intermediateSelection].color;
                                        for (int i = 0; i < totalColors; i++) {
                                            if (availableColors[i].r == c.r && availableColors[i].g == c.g && availableColors[i].b == c.b) { activeColorIndex = i; break; }
                                        }
                                    }
                                }
                            } else {
                                selectedGroup = 0;
                                selectedParts.clear();
                            }
                        }
                    }
                }
            }
        }

        if (!isWorkshop) {
            if (isMoving && isGrounded) {
                if (!IsSoundPlaying(walkSound)) PlaySound(walkSound);
            } else {
                if (IsSoundPlaying(walkSound)) StopSound(walkSound);
            }

            Vector3 currentGroupPos = (Vector3){playerX, playerY, playerZ};
            playerX = currentGroupPos.x; playerY = currentGroupPos.y; playerZ = currentGroupPos.z;

            float targetX = playerX + movement.x;
            BoundingBox xBox = bn_part::GetPartBounds(Vector3{targetX, playerY, playerZ}, playerSize);
            bool collideX = false;
            for (const auto& part : worldParts) {
                if (CheckCollisionBoxes(xBox, part.bounds)) { collideX = true; break; }
            }

            if (!collideX) playerX = targetX;
            else {
                const float maxStepHeight = 1.0f;
                BoundingBox playerBox = bn_part::GetPartBounds(Vector3{playerX, playerY, playerZ}, playerSize);
                float playerFeet = playerBox.min.y;
                BoundingBox hitBox = {0};
                for (const auto& part : worldParts) {
                    if (CheckCollisionBoxes(xBox, part.bounds)) { hitBox = part.bounds; break; }
                }
                float stepHeight = hitBox.max.y - playerFeet;
                if (stepHeight > 0.0f && stepHeight <= maxStepHeight) {
                    BoundingBox stepCheck = bn_part::GetPartBounds(Vector3{targetX, playerY + stepHeight + 0.05f, playerZ}, playerSize);
                    bool stepCollide = false;
                    for (const auto& part : worldParts) {
                        if (CheckCollisionBoxes(stepCheck, part.bounds)) { stepCollide = true; break; }
                    }
                    if (!stepCollide) {
                        playerX = targetX; stepTargetY = playerY + stepHeight + 0.05f; stepping = true; isGrounded = true; playerVelocityY = 0.0f;
                    }
                }
            }

            float targetZ = playerZ + movement.z;
            BoundingBox zBox = bn_part::GetPartBounds(Vector3{playerX, playerY, targetZ}, playerSize);
            bool collideZ = false;
            for (const auto& part : worldParts) {
                if (CheckCollisionBoxes(zBox, part.bounds)) { collideZ = true; break; }
            }

            if (!collideZ) playerZ = targetZ;
            else {
                const float maxStepHeight = 1.0f;
                BoundingBox playerBox = bn_part::GetPartBounds(Vector3{playerX, playerY, playerZ}, playerSize);
                float playerFeet = playerBox.min.y;
                BoundingBox hitBox = {0};
                for (const auto& part : worldParts) {
                    if (CheckCollisionBoxes(zBox, part.bounds)) { hitBox = part.bounds; break; }
                }
                float stepHeight = hitBox.max.y - playerFeet;
                if (stepHeight > 0.0f && stepHeight <= maxStepHeight) {
                    BoundingBox stepCheck = bn_part::GetPartBounds(Vector3{playerX, playerY + stepHeight + 0.05f, targetZ}, playerSize);
                    bool stepCollide = false;
                    for (const auto& part : worldParts) {
                        if (CheckCollisionBoxes(stepCheck, part.bounds)) { stepCollide = true; break; }
                    }
                    if (!stepCollide) {
                        playerZ = targetZ; stepTargetY = playerY + stepHeight + 0.05f; stepping = true; isGrounded = true; playerVelocityY = 0.0f;
                    }
                }
            }

            if (stepping) {
                float diff = stepTargetY - playerY;
                if (diff <= stepSpeed) { playerY = stepTargetY; stepping = false; } 
                else playerY += stepSpeed;
                playerVelocityY = 0.0f; isGrounded = true;
            }

            if (!stepping) playerVelocityY += gravity;
            
            if (isGrounded && IsKeyPressed(KEY_SPACE)) {
                SetSoundVolume(jumpSound, 0.5f); PlaySound(jumpSound);
                playerVelocityY = jumpForce; isGrounded = false;
            }
            float targetY = playerY + playerVelocityY;
            BoundingBox yBox = bn_part::GetPartBounds(Vector3{playerX, targetY, playerZ}, playerSize);
            
            bool collideY = false;
            for (const auto& part : worldParts) {
                if (CheckCollisionBoxes(yBox, part.bounds)) { collideY = true; break; }
            }

            if (!collideY) { playerY = targetY; isGrounded = false; } 
            else {
                if (playerVelocityY < 0.0f) isGrounded = true;
                playerVelocityY = 0.0f;
            }

            if (movement.x != 0.0f || movement.z != 0.0f) playerGroup.SetRotation(atan2f(-movement.z, movement.x));
            playerGroup.SetPosition((Vector3){playerX, playerY - (playerSize.y * 0.5f) + 1.0f, playerZ});
        }

        BeginDrawing();
        ClearBackground(SKYBLUE);

        if (isWorkshop) BeginScissorMode(0, (int)topbarHeight, (int)viewWidth, screenHeight - (int)topbarHeight);

        BeginMode3D(customCam.raylibCamera);
        BeginShaderMode(lighting);

        for (const auto& part : worldParts) {
            if (bn_camera::CheckBoxVisible(customCam, part.bounds)) bn_part::CreatePart(part.position, part.size, part.color, studs, inlet);
        }

        if (isWorkshop) {
            if (currentTool == TOOL_PLACE && showGhost) {
                Color ghostColor = availableColors[activeColorIndex]; ghostColor.a = 120; 
                rlDisableBackfaceCulling(); bn_part::CreatePart(ghostPos, buildSize, ghostColor, studs, inlet); rlEnableBackfaceCulling();
            }
            else if ((currentTool == TOOL_SELECT || currentTool == TOOL_SCALE || currentTool == TOOL_MOVE) && !selectedParts.empty()) {
                for (int idx : selectedParts) DrawBoundingBox(worldParts[idx].bounds, WHITE);
                rlDisableDepthTest();
                for (const auto& h : toolHandles) {
                    if (currentTool == TOOL_SCALE) DrawSphere(h.position, handleRadius, h.color);
                    else {
                        coneModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = h.color;
                        Vector3 targetDir = Vector3Scale(h.axisDirection, h.sign);
                        Vector3 rotAxis = Vector3CrossProduct((Vector3){0, 1, 0}, targetDir);
                        float rotAng = acosf(Vector3DotProduct((Vector3){0, 1, 0}, targetDir)) * RAD2DEG;
                        if (Vector3Length(rotAxis) == 0.0f && h.sign < 0) rotAxis = (Vector3){1, 0, 0}; 
                        DrawModelEx(coneModel, h.position, rotAxis, rotAng, (Vector3){1, 1, 1}, WHITE);
                    }
                }
                rlEnableDepthTest();
            }
        }

        if (!isWorkshop) playerGroup.Draw(customCam, lighting, face, shirtTexture);

        EndShaderMode();
        EndMode3D();
        if (isWorkshop) EndScissorMode();

        DrawFPS(10, (int)topbarHeight + 10);

        if (isWorkshop) {
            DrawRectangleRec(topbarBarRec, DARKPURPLE);

            Color selectBgColor = (currentTool == TOOL_SELECT) ? Fade(GREEN, 0.4f) : Fade(GRAY, 0.2f);
            Color selectBorder = (currentTool == TOOL_SELECT) ? GREEN : GRAY;
            if (CheckCollisionPointRec(GetMousePosition(), selectToolSlotRec)) selectBgColor = ColorAlphaBlend(selectBgColor, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(selectToolSlotRec, selectBgColor); DrawRectangleLinesEx(selectToolSlotRec, 2, selectBorder);
            DrawTexturePro(selectIcon, (Rectangle){0, 0, (float)selectIcon.width, (float)selectIcon.height}, {selectToolSlotRec.x+4, selectToolSlotRec.y+4, selectToolSlotRec.width-8, selectToolSlotRec.height-8}, {0,0}, 0.0f, WHITE); 

            Color placeBgColor = (currentTool == TOOL_PLACE) ? Fade(GREEN, 0.4f) : Fade(GRAY, 0.2f);
            Color placeBorder = (currentTool == TOOL_PLACE) ? GREEN : GRAY;
            if (CheckCollisionPointRec(GetMousePosition(), buildToolSlotRec)) placeBgColor = ColorAlphaBlend(placeBgColor, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(buildToolSlotRec, placeBgColor); DrawRectangleLinesEx(buildToolSlotRec, 2, placeBorder);
            DrawTexturePro(brickIcon, (Rectangle){0, 0, (float)brickIcon.width, (float)brickIcon.height}, {buildToolSlotRec.x+4, buildToolSlotRec.y+4, buildToolSlotRec.width-8, buildToolSlotRec.height-8}, {0,0}, 0.0f, WHITE);

            Color scaleBgColor = (currentTool == TOOL_SCALE) ? Fade(GREEN, 0.4f) : Fade(GRAY, 0.2f);
            Color scaleBorder = (currentTool == TOOL_SCALE) ? GREEN : GRAY;
            if (CheckCollisionPointRec(GetMousePosition(), scaleToolSlotRec)) scaleBgColor = ColorAlphaBlend(scaleBgColor, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(scaleToolSlotRec, scaleBgColor); DrawRectangleLinesEx(scaleToolSlotRec, 2, scaleBorder);
            DrawTexturePro(scaleIcon, (Rectangle){0, 0, (float)scaleIcon.width, (float)scaleIcon.height}, {scaleToolSlotRec.x+4, scaleToolSlotRec.y+4, scaleToolSlotRec.width-8, scaleToolSlotRec.height-8}, {0,0}, 0.0f, WHITE);

            Color moveBgColor = (currentTool == TOOL_MOVE) ? Fade(GREEN, 0.4f) : Fade(GRAY, 0.2f);
            Color moveBorder = (currentTool == TOOL_MOVE) ? GREEN : GRAY;
            if (CheckCollisionPointRec(GetMousePosition(), moveToolSlotRec)) moveBgColor = ColorAlphaBlend(moveBgColor, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(moveToolSlotRec, moveBgColor); DrawRectangleLinesEx(moveToolSlotRec, 2, moveBorder);
            DrawTexturePro(moveIcon, (Rectangle){0, 0, (float)moveIcon.width, (float)moveIcon.height}, {moveToolSlotRec.x+4, moveToolSlotRec.y+4, moveToolSlotRec.width-8, moveToolSlotRec.height-8}, {0,0}, 0.0f, WHITE);

            Color paintBgColor = (currentTool == TOOL_PAINT) ? Fade(GREEN, 0.4f) : Fade(GRAY, 0.2f);
            Color paintBorder = (currentTool == TOOL_PAINT) ? GREEN : GRAY;
            if (CheckCollisionPointRec(GetMousePosition(), paintToolSlotRec)) paintBgColor = ColorAlphaBlend(paintBgColor, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(paintToolSlotRec, paintBgColor); DrawRectangleLinesEx(paintToolSlotRec, 2, paintBorder);
            DrawTexturePro(paintIcon, (Rectangle){0, 0, (float)paintIcon.width, (float)paintIcon.height}, {paintToolSlotRec.x+4, paintToolSlotRec.y+4, paintToolSlotRec.width-8, paintToolSlotRec.height-8}, {0,0}, 0.0f, WHITE);

            Color dropdownBtnBg = isColorDropdownOpen ? Fade(BLACK, 0.5f) : Fade(GRAY, 0.2f);
            if (CheckCollisionPointRec(GetMousePosition(), colorDropdownRec)) dropdownBtnBg = ColorAlphaBlend(dropdownBtnBg, Fade(WHITE, 0.1f), WHITE);
            DrawRectangleRec(colorDropdownRec, dropdownBtnBg); DrawRectangleLinesEx(colorDropdownRec, 2, GRAY);
            DrawRectangle(colorDropdownRec.x + 8.0f, colorDropdownRec.y + 12.0f, 20.0f, 20.0f, availableColors[activeColorIndex]);
            DrawRectangleLines(colorDropdownRec.x + 8.0f, colorDropdownRec.y + 12.0f, 20.0f, 20.0f, WHITE);
            DrawTextEx(font, colorNames[activeColorIndex], (Vector2){ colorDropdownRec.x + 36.0f, colorDropdownRec.y + 14.0f }, 18, 1, WHITE);

            Color saveBtnBg = Fade(GRAY, 0.2f);
            if (isMouseOverSaveButton) saveBtnBg = ColorAlphaBlend(saveBtnBg, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(saveBtnRec, saveBtnBg); DrawRectangleLinesEx(saveBtnRec, 2, GRAY);
            DrawTextEx(font, "Save", (Vector2){ saveBtnRec.x + 20.0f, saveBtnRec.y + 14.0f }, 18, 1, WHITE);

            Color openBtnBg = Fade(GRAY, 0.2f);
            if (isMouseOverOpenButton) openBtnBg = ColorAlphaBlend(openBtnBg, Fade(WHITE, 0.15f), WHITE);
            DrawRectangleRec(openBtnRec, openBtnBg); DrawRectangleLinesEx(openBtnRec, 2, GRAY);
            DrawTextEx(font, "Open", (Vector2){ openBtnRec.x + 20.0f, openBtnRec.y + 14.0f }, 18, 1, WHITE);

            if (isColorDropdownOpen) {
                for (int i = 0; i < totalColors; i++) {
                    Rectangle itemRec = { colorDropdownRec.x, colorDropdownRec.y + colorDropdownRec.height + (i * 30), colorDropdownRec.width, 30.0f };
                    Color itemBg = Fade(BLACK, 0.85f);
                    if (CheckCollisionPointRec(GetMousePosition(), itemRec)) itemBg = Fade(DARKPURPLE, 0.85f);
                    DrawRectangleRec(itemRec, itemBg); DrawRectangleLinesEx(itemRec, 1, Fade(GRAY, 0.4f));
                    DrawRectangle(itemRec.x + 8.0f, itemRec.y + 5.0f, 20.0f, 20.0f, availableColors[i]);
                    DrawRectangleLines(itemRec.x + 8.0f, itemRec.y + 5.0f, 20.0f, 20.0f, WHITE);
                    DrawTextEx(font, colorNames[i], (Vector2){ itemRec.x + 36.0f, itemRec.y + 7.0f }, 18, 1, WHITE);
                }
            }

            DrawRectangleRec(sidebarRec, DARKPURPLE);
            DrawLineEx((Vector2){ sidebarRec.x, topbarHeight }, (Vector2){ sidebarRec.x, (float)screenHeight }, 2, GRAY);
            
            float itemHeight = 30.0f;
            float visibleAreaHeight = (float)screenHeight - topbarHeight - 20.0f;

            explorerItems.clear();
            for (size_t i = 0; i < worldGroups.size(); i++) {
                explorerItems.push_back({EXP_GROUP, worldGroups[i].id, 0});
                if (worldGroups[i].expanded) {
                    for (size_t j = 0; j < worldParts.size(); j++) {
                        if (worldParts[j].groupId == worldGroups[i].id) {
                            explorerItems.push_back({EXP_PART, (int)j, 1});
                        }
                    }
                }
            }
            for (size_t j = 0; j < worldParts.size(); j++) {
                if (worldParts[j].groupId == 0) explorerItems.push_back({EXP_PART, (int)j, 0});
            }

            BeginScissorMode((int)sidebarRec.x, (int)sidebarRec.y, (int)sidebarWidth, (int)screenHeight - topbarHeight);
            float itemStartY = sidebarRec.y + 10.0f - sidebarScrollOffset;
            
            for (size_t i = 0; i < explorerItems.size(); i++) {
                Rectangle itemRec = { sidebarRec.x + 10.0f + (explorerItems[i].indent * 15.0f), itemStartY + (i * itemHeight), sidebarWidth - 25.0f - (explorerItems[i].indent * 15.0f), 25.0f };
                
                if (itemRec.y + itemRec.height > sidebarRec.y && itemRec.y < sidebarRec.y + screenHeight - topbarHeight) {
                    bool isSelected = false;
                    if (explorerItems[i].type == EXP_GROUP) isSelected = (selectedGroup == explorerItems[i].indexOrId);
                    else isSelected = (std::find(selectedParts.begin(), selectedParts.end(), explorerItems[i].indexOrId) != selectedParts.end());

                    bool isHovered = CheckCollisionPointRec(mousePoint2D, itemRec);
                    Color itemBg = isSelected ? Fade(BLACK, 0.6f) : (isHovered ? Fade(BLACK, 0.2f) : BLANK);
                    DrawRectangleRec(itemRec, itemBg);
                    if (isSelected) DrawRectangleLinesEx(itemRec, 1, PURPLE);

                    if (explorerItems[i].type == EXP_GROUP) {
                        bool expanded = false;
                        for (auto& g : worldGroups) if (g.id == explorerItems[i].indexOrId) { expanded = g.expanded; break; }
                        DrawTextEx(font, expanded ? "v" : ">", (Vector2){ itemRec.x + 4.0f, itemRec.y + 4.0f }, 18, 1, isSelected ? WHITE : LIGHTGRAY);
                    }

                    float textOffsetX = (explorerItems[i].type == EXP_GROUP) ? 24.0f : 8.0f;

                    if (renamingItemId == explorerItems[i].indexOrId && renamingItemType == explorerItems[i].type) {
                        DrawRectangleRec(itemRec, Fade(BLACK, 0.8f));
                        DrawRectangleLinesEx(itemRec, 1, WHITE);
                        std::string viewStr = renameBuffer;
                        if (((int)(GetTime() * 3.0f)) % 2 == 0) viewStr += "|";
                        DrawTextEx(font, viewStr.c_str(), (Vector2){ itemRec.x + textOffsetX, itemRec.y + 4.0f }, 18, 1, WHITE);
                    } else {
                        std::string displayName;
                        if (explorerItems[i].type == EXP_GROUP) {
                            for (auto& g : worldGroups) if (g.id == explorerItems[i].indexOrId) { displayName = g.name; break; }
                        } else {
                            displayName = worldParts[explorerItems[i].indexOrId].name.empty() ? ("Part " + std::to_string(explorerItems[i].indexOrId)) : worldParts[explorerItems[i].indexOrId].name;
                        }
                        DrawTextEx(font, displayName.c_str(), (Vector2){ itemRec.x + textOffsetX, itemRec.y + 4.0f }, 18, 1, isSelected ? WHITE : LIGHTGRAY);
                    }
                }
            }
            EndScissorMode();

            float totalContentHeight = (float)explorerItems.size() * itemHeight;
            if (totalContentHeight > visibleAreaHeight) {
                float scrollbarHeight = (visibleAreaHeight / totalContentHeight) * visibleAreaHeight;
                float scrollbarY = sidebarRec.y + 10.0f + (sidebarScrollOffset / totalContentHeight) * visibleAreaHeight;
                DrawRectangle(sidebarRec.x + sidebarWidth - 10.0f, (int)scrollbarY, 6, (int)scrollbarHeight, Fade(WHITE, 0.5f));
            }
        }

        EndDrawing();
    }
    if (hatTexture.id > 0) {
        UnloadTexture(hatTexture);
    }
    UnloadModel(coneModel);
    UnloadTexture(selectIcon); 
    UnloadTexture(brickIcon);
    UnloadTexture(scaleIcon);
    UnloadTexture(moveIcon);
    UnloadTexture(paintIcon); 
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
