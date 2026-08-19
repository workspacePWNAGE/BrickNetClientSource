#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <cfloat>

#include "../include/raylib.h"
#include "../include/raymath.h"
#include "../include/rlgl.h"
#include "../include/httplib.h"
#include "../include/bricknet/camera.h"
#include "../include/bricknet/presets/group_player.h"
#include "../include/json.hpp"
#include "../include/bricknet/part.h"

using json = nlohmann::json;

enum class RenderType {
    Avatar,
    HatPreview
};

struct RenderJob {
    RenderType type;
    std::string username;
    std::string hatId;

    std::vector<unsigned char> image_bytes;
    bool error = false;
    bool done = false;

    std::mutex mtx;
    std::condition_variable cv;
};

std::queue<std::shared_ptr<RenderJob>> jobQueue;
std::mutex queueMutex;

Color JsonToColor(const json& j)
{
    if (!j.is_array() || j.size() < 4)
        return WHITE;

    return {
        (unsigned char)j[0].get<int>(),
        (unsigned char)j[1].get<int>(),
        (unsigned char)j[2].get<int>(),
        (unsigned char)j[3].get<int>()
    };
}

bool DownloadAsset(const std::string& baseUrl,
                   const std::string& assetPath,
                   const std::string& savePath)
{
    std::string url = baseUrl;
    if (url.back() == '/') url.pop_back();

    size_t schemeEnd = url.find("://");
    std::string hostPort = url.substr(schemeEnd + 3);

    size_t portDelim = hostPort.find(':');
    std::string host = hostPort.substr(0, portDelim);
    int port = (portDelim != std::string::npos)
        ? std::stoi(hostPort.substr(portDelim + 1))
        : 80;

    httplib::Client cli(host, port);

    std::string reqPath = (assetPath[0] != '/') ? "/" + assetPath : assetPath;

    if (auto res = cli.Get(reqPath.c_str())) {
        if (res->status == 200) {
            std::ofstream ofs(savePath, std::ios::binary);
            ofs << res->body;
            return true;
        }
    }
    return false;
}

void ProcessJob(std::shared_ptr<RenderJob>& job,
                const std::string& host,
                int port,
                const std::string& BASE_URL,
                Shader lighting,
                Texture studs,
                Texture inlet)
{
    if (job->type == RenderType::Avatar)
    {
        httplib::Client cli(host, port);
        auto apiRes = cli.Get(("/include/getAvatar?username=" + job->username).c_str());

        if (!apiRes || apiRes->status != 200) {
            std::lock_guard<std::mutex> lock(job->mtx);
            job->error = true;
            job->done = true;
            job->cv.notify_one();
            return;
        }

	json userData = json::parse(apiRes->body);

	Color headCol  = JsonToColor(userData["colors"]["head"]);
	Color torsoCol = JsonToColor(userData["colors"]["torso"]);
	Color larmCol  = JsonToColor(userData["colors"]["left_arm"]);
	Color rarmCol  = JsonToColor(userData["colors"]["right_arm"]);
	Color llegCol  = JsonToColor(userData["colors"]["left_leg"]);
	Color rlegCol  = JsonToColor(userData["colors"]["right_leg"]);

        DownloadAsset(BASE_URL, userData["hat"]["mesh"], "assets/temp/hat.obj");
        DownloadAsset(BASE_URL, userData["hat"]["texture"], "assets/temp/hat.png");
        DownloadAsset(BASE_URL, userData["face"]["texture"], "assets/temp/face.png");
        DownloadAsset(BASE_URL, userData["shirt"]["texture"], "assets/temp/shirt.png");

        Texture hatTex = LoadTexture("assets/temp/hat.png");
        Texture faceTex = LoadTexture("assets/temp/face.png");
        Texture shirtTex = LoadTexture("assets/temp/shirt.png");

        bn_mesh::LoadedMeshComponent playerHat =
            bn_mesh::LoadModelAsset(
                "assets/temp/hat.obj",
                {0, 3.65f, 0},
                {0.65f, 0.65f, 0.65f},
                WHITE,
                hatTex
            );

        bn_group::BrickGroup playerGroup =
            group_player::createPlayerGroup(
                studs, inlet, playerHat,
                larmCol, rarmCol, torsoCol, headCol, llegCol, rlegCol
            );

        BoundingBox box = playerGroup.GetBoundingBox();

        Vector3 center = {
            (box.min.x + box.max.x) * 0.5f,
            box.min.y + (box.max.y - box.min.y) * 0.55f,
            (box.min.z + box.max.z) * 0.5f
        };

        float distance = 6.0f;
        Vector3 dir = Vector3Normalize({15, 10, -8});

        bn_camera::CustomCamera cam = bn_camera::Create({0, 2, 0});

        cam.raylibCamera.target = center;
        cam.raylibCamera.position = {
            center.x + dir.x * distance,
            center.y + dir.y * distance,
            center.z + dir.z * distance
        };
        cam.raylibCamera.fovy = 60;

        RenderTexture2D target = LoadRenderTexture(400, 400);

        BeginTextureMode(target);
            ClearBackground(BLANK);
            BeginMode3D(cam.raylibCamera);
                BeginShaderMode(lighting);
                    playerGroup.Draw(cam, lighting, faceTex, shirtTex);
                EndShaderMode();
            EndMode3D();
        EndTextureMode();

        Image img = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&img);

        int fileSize = 0;
        unsigned char* fileData = ExportImageToMemory(img, ".png", &fileSize);

        UnloadImage(img);
        UnloadRenderTexture(target);

        UnloadTexture(hatTex);
        UnloadTexture(faceTex);
        UnloadTexture(shirtTex);
        bn_mesh::UnloadModelAsset(playerHat);

        {
            std::lock_guard<std::mutex> lock(job->mtx);
            if (fileData) {
                job->image_bytes.assign(fileData, fileData + fileSize);
                MemFree(fileData);
            } else job->error = true;

            job->done = true;
        }

        job->cv.notify_one();
        return;
    }

    if (job->type == RenderType::HatPreview)
    {
        httplib::Client cli(host, port);
        auto apiRes = cli.Get(("/include/getHat?hat=" + job->hatId).c_str());

        if (!apiRes || apiRes->status != 200) {
            std::lock_guard<std::mutex> lock(job->mtx);
            job->error = true;
            job->done = true;
            job->cv.notify_one();
            return;
        }

        json hatData = json::parse(apiRes->body);

        DownloadAsset(BASE_URL, hatData["mesh"], "assets/temp/hat.obj");
        DownloadAsset(BASE_URL, hatData["texture"], "assets/temp/hat.png");

        Texture hatTex = LoadTexture("assets/temp/hat.png");
	Texture faceTex = LoadTexture("assets/textures/face.png");
	Texture shirtTex = LoadTexture("assets/textures/blank.png");

        bn_mesh::LoadedMeshComponent hat =
            bn_mesh::LoadModelAsset(
                "assets/temp/hat.obj",
                {0, 3.65f, 0},
                {0.65f, 0.65f, 0.65f},
                WHITE,
                hatTex
            );

        bn_mesh::LoadedMeshComponent dummyHat = hat;

        bn_group::BrickGroup mannequin =
            group_player::createPlayerGroup(
                studs, inlet, dummyHat,
                WHITE, WHITE, WHITE, WHITE, WHITE, WHITE
            );

        BoundingBox box = mannequin.GetBoundingBox();

        Vector3 center = {
            (box.min.x + box.max.x) * 0.5f,
            box.max.y - 0.2f,
            (box.min.z + box.max.z) * 0.5f
        };

        Vector3 dir = Vector3Normalize({1, 0.6f, -1});
        float distance = 6.0f;

        bn_camera::CustomCamera cam = bn_camera::Create({0, 0, 0});

        cam.raylibCamera.target = center;
        cam.raylibCamera.position = {
            center.x + dir.x * distance,
            center.y + dir.y * distance,
            center.z + dir.z * distance
        };

        cam.raylibCamera.fovy = 40;

        RenderTexture2D target = LoadRenderTexture(400, 400);

        BeginTextureMode(target);
            ClearBackground(BLANK);

            BeginMode3D(cam.raylibCamera);
                BeginShaderMode(lighting);

                    mannequin.Draw(cam, lighting,
                        faceTex, shirtTex);

                EndShaderMode();
            EndMode3D();
        EndTextureMode();

        Image img = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&img);

        int fileSize = 0;
        unsigned char* fileData = ExportImageToMemory(img, ".png", &fileSize);

        UnloadImage(img);
        UnloadRenderTexture(target);

        UnloadTexture(hatTex);
        bn_mesh::UnloadModelAsset(hat);

        {
            std::lock_guard<std::mutex> lock(job->mtx);
            if (fileData) {
                job->image_bytes.assign(fileData, fileData + fileSize);
                MemFree(fileData);
            } else job->error = true;

            job->done = true;
        }

        job->cv.notify_one();
    }
}

int main()
{
    const std::string BASE_URL = "http://brick-net.cc";
    std::string host = "localhost";
    int port = 3561;

    std::filesystem::create_directories("assets/temp");

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(400, 400, "Brick Net Render Service");

    Shader lighting = LoadShader("assets/shaders/lighting.vs",
                                 "assets/shaders/lighting.fs");

    Texture studs = LoadTexture("assets/textures/stud.png");
    Texture inlet = LoadTexture("assets/textures/inlet.png");

    httplib::Server svr;

    svr.Get("/render", [&](const httplib::Request& req, httplib::Response& res) {
        auto job = std::make_shared<RenderJob>();
        job->type = RenderType::Avatar;
        job->username = req.get_param_value("username");

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            jobQueue.push(job);
        }

        std::unique_lock<std::mutex> lock(job->mtx);
        job->cv.wait(lock, [&] { return job->done; });

        res.set_content((char*)job->image_bytes.data(),
                        job->image_bytes.size(),
                        "image/png");
    });

    svr.Get("/hatrender", [&](const httplib::Request& req, httplib::Response& res) {
        auto job = std::make_shared<RenderJob>();
        job->type = RenderType::HatPreview;
        job->hatId = req.get_param_value("hat");

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            jobQueue.push(job);
        }

        std::unique_lock<std::mutex> lock(job->mtx);
        job->cv.wait(lock, [&] { return job->done; });

        res.set_content((char*)job->image_bytes.data(),
                        job->image_bytes.size(),
                        "image/png");
    });

    std::thread serverThread([&]() {
        svr.listen("0.0.0.0", 8080);
    });

    while (!WindowShouldClose())
    {
        std::shared_ptr<RenderJob> job;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!jobQueue.empty()) {
                job = jobQueue.front();
                jobQueue.pop();
            }
        }

        if (job) {
            ProcessJob(job, host, port, BASE_URL, lighting, studs, inlet);
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    svr.stop();
    serverThread.join();

    UnloadShader(lighting);
    UnloadTexture(studs);
    UnloadTexture(inlet);
    CloseWindow();

    return 0;
}
