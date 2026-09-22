#pragma once

#include "../features/vehicle/VehicleService.hpp"
#include "../game/vehicle/VehicleCatalogs.hpp"
#include "../render/Renderer.hpp"
#include "VehicleThumbnailDownloader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TutonesV2::UI
{
    struct VehicleThumbnailView final
    {
        std::uint64_t textureId{};
        std::uint32_t width{};
        std::uint32_t height{};
        bool exact{};
        std::filesystem::path source{};

        [[nodiscard]] bool Valid() const noexcept
        {
            return textureId != 0;
        }
    };

    class VehicleThumbnailCache final
    {
    public:
        static VehicleThumbnailCache& Get() noexcept
        {
            static VehicleThumbnailCache instance;
            return instance;
        }

        [[nodiscard]] VehicleThumbnailView ClassThumbnail(int classIndex) noexcept
        {
            EnsureGeneration();
            StartArtworkSync();

            if (classIndex < 0
                || classIndex >= static_cast<int>(Game::VehicleCatalogs::VehicleClassNames.size()))
            {
                return {};
            }

            const auto index = static_cast<std::size_t>(classIndex);
            EnsureClassTexture(index);

            if (m_ClassTextures[index].Valid())
            {
                return VehicleThumbnailView{
                    m_ClassTextures[index].TextureId,
                    m_ClassTextures[index].Width,
                    m_ClassTextures[index].Height,
                    false,
                    m_ClassPaths[index],
                };
            }

            if (const char* representative = RepresentativeModel(index))
                VehicleThumbnailDownloader::Get().Request(representative, classIndex);

            return {};
        }

        [[nodiscard]] VehicleThumbnailView VehicleThumbnail(
            std::string_view model,
            int classIndex) noexcept
        {
            EnsureGeneration();
            StartArtworkSync();

            const std::string normalized = NormalizeModel(model);
            if (normalized.empty())
                return ClassThumbnail(classIndex);

            ++m_UseCounter;

            if (auto found = m_ExactTextures.find(normalized);
                found != m_ExactTextures.end())
            {
                found->second.lastUse = m_UseCounter;
                return VehicleThumbnailView{
                    found->second.texture.TextureId,
                    found->second.texture.Width,
                    found->second.texture.Height,
                    true,
                    found->second.path,
                };
            }

            std::filesystem::path path;
            if (FindNamedImage(normalized, path))
            {
                EvictIfNeeded();

                ExactEntry entry{};
                entry.lastUse = m_UseCounter;
                entry.path = path;

                if (Render::Renderer::Get().LoadTextureFile(path, entry.texture))
                {
                    auto [it, inserted] = m_ExactTextures.emplace(normalized, std::move(entry));
                    if (inserted)
                    {
                        return VehicleThumbnailView{
                            it->second.texture.TextureId,
                            it->second.texture.Width,
                            it->second.texture.Height,
                            true,
                            it->second.path,
                        };
                    }
                }
            }

            VehicleThumbnailDownloader::Get().Request(normalized, classIndex);
            return ClassThumbnail(classIndex);
        }

        [[nodiscard]] VehicleThumbnailSyncSnapshot SyncSnapshot() const
        {
            return VehicleThumbnailDownloader::Get().Snapshot();
        }

        [[nodiscard]] std::filesystem::path ThumbnailFolder() const
        {
            return VehicleThumbnailDownloader::Get().ThumbnailFolder();
        }

        void Refresh() noexcept
        {
            ReleaseAllTextures();
            VehicleThumbnailDownloader::Get().Restart(m_LastClasses);
        }

        void ReleaseAllTextures() noexcept
        {
            auto& renderer = Render::Renderer::Get();

            for (auto& texture : m_ClassTextures)
                renderer.ReleaseTexture(texture);

            for (auto& [_, entry] : m_ExactTextures)
                renderer.ReleaseTexture(entry.texture);

            m_ExactTextures.clear();
            m_ClassAttemptGeneration.fill(std::numeric_limits<std::uint64_t>::max());
        }

    private:
        struct ExactEntry final
        {
            Render::Renderer::TextureHandle texture{};
            std::filesystem::path path{};
            std::uint64_t lastUse{};
        };

        VehicleThumbnailCache()
        {
            m_ClassAttemptGeneration.fill(std::numeric_limits<std::uint64_t>::max());
        }

        static constexpr std::size_t MaxExactResident = 48;

        [[nodiscard]] static const char* RepresentativeModel(std::size_t classIndex) noexcept
        {
            static constexpr std::array<const char*, 23> Models{{
                "panto",
                "tailgater",
                "baller",
                "sentinel",
                "dominator",
                "turismo2",
                "jester",
                "adder",
                "bati",
                "mesa3",
                "bulldozer",
                "towtruck",
                "speedo",
                "bmx",
                "speeder",
                "maverick",
                "luxor",
                "taxi",
                "police",
                "rhino",
                "phantom",
                "freight",
                "formula",
            }};
            return classIndex < Models.size() ? Models[classIndex] : nullptr;
        }

        static std::string NormalizeModel(std::string_view model)
        {
            std::string output;
            output.reserve(model.size());

            for (const unsigned char character : model)
            {
                if ((character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9')
                    || character == '_'
                    || character == '-')
                {
                    output.push_back(static_cast<char>(character));
                }
                else if (character >= 'A' && character <= 'Z')
                {
                    output.push_back(static_cast<char>(character - 'A' + 'a'));
                }
                else
                {
                    output.push_back('_');
                }
            }

            return output;
        }

        void EnsureGeneration() noexcept
        {
            const auto generation = Render::Renderer::Get().TextureGeneration();
            if (generation == 0 || generation == m_RenderGeneration)
                return;

            ReleaseAllTextures();
            m_RenderGeneration = generation;
        }

        void StartArtworkSync() noexcept
        {
            const auto catalog = Features::Vehicle::VehicleService::Get().CatalogSnapshot();
            if (catalog.total == 0
                || catalog.ready < catalog.total
                || catalog.classes.size() != catalog.total)
            {
                return;
            }

            if (m_LastClasses != catalog.classes)
                m_LastClasses = catalog.classes;

            VehicleThumbnailDownloader::Get().EnsureStarted(catalog.classes);
        }

        [[nodiscard]] bool FindNamedImage(
            std::string_view baseName,
            std::filesystem::path& output) const noexcept
        {
            const auto root = VehicleThumbnailDownloader::Get().ThumbnailFolder();
            static constexpr std::array<const char*, 5> Extensions{{
                ".png", ".jpg", ".jpeg", ".bmp", ".webp"
            }};

            for (const char* extension : Extensions)
            {
                const auto path = root / (std::string(baseName) + extension);
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error) && !error)
                {
                    output = path;
                    return true;
                }
            }

            return false;
        }

        void EnsureClassTexture(std::size_t index) noexcept
        {
            if (index >= m_ClassTextures.size() || m_ClassTextures[index].Valid())
                return;

            const auto syncGeneration = VehicleThumbnailDownloader::Get().Snapshot().generation;
            if (m_ClassAttemptGeneration[index] == syncGeneration)
                return;

            m_ClassAttemptGeneration[index] = syncGeneration;

            std::filesystem::path path;
            const std::string explicitClass = "class_" + std::to_string(index);
            if (FindNamedImage(explicitClass, path)
                && Render::Renderer::Get().LoadTextureFile(path, m_ClassTextures[index]))
            {
                m_ClassPaths[index] = path;
                return;
            }

            if (const char* representative = RepresentativeModel(index))
            {
                if (FindNamedImage(representative, path)
                    && Render::Renderer::Get().LoadTextureFile(path, m_ClassTextures[index]))
                {
                    m_ClassPaths[index] = path;
                    return;
                }

                VehicleThumbnailDownloader::Get().Request(
                    representative,
                    static_cast<int>(index));
            }
        }

        void EvictIfNeeded() noexcept
        {
            if (m_ExactTextures.size() < MaxExactResident)
                return;

            auto victim = m_ExactTextures.end();
            std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();

            for (auto it = m_ExactTextures.begin(); it != m_ExactTextures.end(); ++it)
            {
                if (it->second.lastUse < oldest)
                {
                    oldest = it->second.lastUse;
                    victim = it;
                }
            }

            if (victim == m_ExactTextures.end())
                return;

            Render::Renderer::Get().ReleaseTexture(victim->second.texture);
            m_ExactTextures.erase(victim);
        }

        std::uint64_t m_RenderGeneration{};
        std::uint64_t m_UseCounter{};
        std::vector<int> m_LastClasses{};

        std::array<
            Render::Renderer::TextureHandle,
            Game::VehicleCatalogs::VehicleClassNames.size()> m_ClassTextures{};
        std::array<
            std::filesystem::path,
            Game::VehicleCatalogs::VehicleClassNames.size()> m_ClassPaths{};
        std::array<
            std::uint64_t,
            Game::VehicleCatalogs::VehicleClassNames.size()> m_ClassAttemptGeneration{};

        std::unordered_map<std::string, ExactEntry> m_ExactTextures{};
    };
}
