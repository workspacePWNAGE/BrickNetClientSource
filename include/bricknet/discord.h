#pragma once

#include <iostream>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include "../discordpp.h"

class DiscordManager {
public:
    explicit DiscordManager(uint64_t appId) : m_appId(appId) {}

    bool Initialize() {
        m_client = std::make_unique<discordpp::Client>();

        if (!*m_client) {
            std::cerr << "Failed to instantiate Discord Client." << std::endl;
            return false;
        }

        m_client->SetApplicationId(m_appId);

        m_client->SetStatusChangedCallback([this](discordpp::Client::Status status, discordpp::Client::Error error, int32_t errorCode) {
            std::cout << "Discord Status Changed: " << discordpp::Client::StatusToString(status) << std::endl;

            if (error != discordpp::Client::Error::None) {
                std::cerr << "Discord Error: " << discordpp::Client::ErrorToString(error)
                          << " (code " << errorCode << ")" << std::endl;
                return;
            }

            if (status == discordpp::Client::Status::Ready) {
                m_ready = true;
                if (m_pendingActivity) {
                    m_client->UpdateRichPresence(*m_pendingActivity, [](discordpp::ClientResult result) {
                        if (!result.Successful()) {
                            std::cerr << "UpdateRichPresence failed: " << result.Error() << std::endl;
                        }
                    });
                }
            }
        });

        std::string savedRefreshToken = LoadRefreshToken();
        if (!savedRefreshToken.empty()) {
            TryRefresh(savedRefreshToken);
        } else {
            Authenticate();
        }

        std::cout << "discord sdk running!!" << std::endl;
        return true;
    }

    void SetActivity(const discordpp::Activity& activity) {
        m_pendingActivity = activity;
        if (m_ready) {
            m_client->UpdateRichPresence(activity, [](discordpp::ClientResult result) {
                if (!result.Successful()) {
                    std::cerr << "UpdateRichPresence failed: " << result.Error() << std::endl;
                }
            });
        }
    }

    bool IsReady() const { return m_ready; }

    void RunCallbacks() {
        discordpp::RunCallbacks();
    }

    discordpp::Client* GetClient() const {
        return m_client.get();
    }

private:
    static constexpr const char* kTokenFile = "discord_refresh.token";

    std::string LoadRefreshToken() {
        std::ifstream file(kTokenFile);
        if (!file.is_open()) return "";
        std::string token;
        std::getline(file, token);
        return token;
    }

    void SaveRefreshToken(const std::string& token) {
        std::ofstream file(kTokenFile, std::ios::trunc);
        if (file.is_open()) file << token;
    }

    void ClearRefreshToken() {
        std::remove(kTokenFile);
    }

    void TryRefresh(const std::string& refreshToken) {
        m_client->RefreshToken(
            m_appId, refreshToken,
            [this](discordpp::ClientResult result,
                   std::string accessToken,
                   std::string newRefreshToken,
                   discordpp::AuthorizationTokenType tokenType,
                   int32_t,
                   std::string) {
                if (!result.Successful()) {
                    std::cerr << "Discord token refresh failed (" << result.Error()
                              << "), falling back to full login." << std::endl;
                    ClearRefreshToken();
                    Authenticate();
                    return;
                }

                SaveRefreshToken(newRefreshToken);

                m_client->UpdateToken(tokenType, accessToken, [this](discordpp::ClientResult result) {
                    if (!result.Successful()) {
                        std::cerr << "Discord UpdateToken failed: " << result.Error() << std::endl;
                        return;
                    }
                    m_client->Connect();
                });
            });
    }

    void Authenticate() {
        auto verifier = m_client->CreateAuthorizationCodeVerifier();

        discordpp::AuthorizationArgs args{};
        args.SetClientId(m_appId);
        args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
        args.SetCodeChallenge(verifier.Challenge());

        m_client->Authorize(args, [this, verifier](discordpp::ClientResult result, std::string code, std::string redirectUri) mutable {
            if (!result.Successful()) {
                std::cerr << "Discord authorization failed: " << result.Error() << std::endl;
                return;
            }

            m_client->GetToken(
                m_appId, code, verifier.Verifier(), redirectUri,
                [this](discordpp::ClientResult result,
                       std::string accessToken,
                       std::string refreshToken,
                       discordpp::AuthorizationTokenType tokenType,
                       int32_t,
                       std::string) {
                    if (!result.Successful()) {
                        std::cerr << "Discord token exchange failed: " << result.Error() << std::endl;
                        return;
                    }

                    SaveRefreshToken(refreshToken);

                    m_client->UpdateToken(tokenType, accessToken, [this](discordpp::ClientResult result) {
                        if (!result.Successful()) {
                            std::cerr << "Discord UpdateToken failed: " << result.Error() << std::endl;
                            return;
                        }
                        m_client->Connect();
                    });
                });
        });
    }

    uint64_t m_appId;
    std::unique_ptr<discordpp::Client> m_client;
    bool m_ready = false;
    std::optional<discordpp::Activity> m_pendingActivity;
};