#include "imagegen/LocalImageModelPreference.h"
#include "imagegen/LocalImageModelPreset.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "LocalImageModelPreset test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }


    void touch(
        const std::filesystem::path& path)
    {
        std::filesystem::create_directories(
            path.parent_path());

        std::ofstream file{
            path,
            std::ios::binary
        };

        file << "test";
    }


    void removeIfPresent(
        const std::filesystem::path& path)
    {
        std::error_code ignored;
        std::filesystem::remove(
            path,
            ignored);
    }
}


int main()
{
    namespace fs = std::filesystem;
    using namespace rose::imagegen;

    const fs::path root =
        fs::temp_directory_path()
        / "RoseLocalImageModelPresetTest";

    const fs::path preferencePath =
        root
        / "config"
        / "image-model.txt";

    const fs::path realVisPath =
        root
        / "realvisxl-v5"
        / "model.safetensors";

    const fs::path juggernautPath =
        root
        / "juggernaut-xl"
        / "model.safetensors";

    const fs::path ponyPath =
        root
        / "pony-v6"
        / "model.safetensors";

    const fs::path sd15Path =
        root
        / "v1-5-pruned-emaonly.safetensors";

    std::error_code ignored;
    fs::remove_all(
        root,
        ignored);

    fs::create_directories(
        root);


    // -------------------------------------------------------------------------
    // Preference parsing / persistence
    // -------------------------------------------------------------------------

    require(
        parseLocalImageModelPreference("auto")
            == LocalImageModelPreference::Auto,
        "auto preference should parse");

    require(
        parseLocalImageModelPreference("flux")
            == LocalImageModelPreference::Flux2Klein4B,
        "flux alias should parse");

    require(
        parseLocalImageModelPreference("RealVis")
            == LocalImageModelPreference::RealVisXLV5,
        "RealVis alias should parse case-insensitively");

    require(
        parseLocalImageModelPreference("juggernaut")
            == LocalImageModelPreference::JuggernautXL,
        "Juggernaut alias should parse");

    require(
        parseLocalImageModelPreference("PonyXL")
            == LocalImageModelPreference::PonyV6,
        "PonyXL alias should parse case-insensitively");

    require(
        parseLocalImageModelPreference("SD1.5")
            == LocalImageModelPreference::StableDiffusion15Fallback,
        "SD1.5 alias should parse case-insensitively");

    require(
        !parseLocalImageModelPreference("not-a-model").has_value(),
        "unknown model preference should not parse");

    require(
        loadLocalImageModelPreference(
            preferencePath)
            == LocalImageModelPreference::Auto,
        "missing preference file should default to auto");

    saveLocalImageModelPreference(
        preferencePath,
        LocalImageModelPreference::JuggernautXL);

    require(
        loadLocalImageModelPreference(
            preferencePath)
            == LocalImageModelPreference::JuggernautXL,
        "saved preference should round-trip");


    // -------------------------------------------------------------------------
    // Auto-selection priority
    // -------------------------------------------------------------------------
    // With no files installed Rose retains historical graceful behavior and
    // returns an SD1.5-shaped preset whose generator later reports availability.

    {
        const LocalImageModelPreset fallback =
            selectLocalImageModelPreset(
                root);

        require(
            fallback.id == "sd15-fallback",
            "no installed image model should preserve SD1.5 graceful fallback");
    }


    // Pony participates in auto-selection when it is the only installed model.
    touch(
        ponyPath);

    {
        const LocalImageModelPreset pony =
            selectLocalImageModelPreset(
                root);

        require(
            pony.id == "pony-v6",
            "Pony should be auto-selected when it is the only installed model");

        require(
            pony.generatorConfig.modelId == pony.id
                && pony.generatorConfig.modelDisplayName == pony.displayName,
            "Pony logical model identity should reach the generator config");

        require(
            pony.generatorConfig.modelPath == ponyPath,
            "Pony should configure its Rose-owned single checkpoint path");

        require(
            pony.profiles.standard.steps == 30
                && pony.profiles.standard.cfgScale == 6.5f,
            "Pony should use its dedicated standard profile");
    }


    // Juggernaut outranks Pony in Auto.
    touch(
        juggernautPath);

    {
        const LocalImageModelPreset juggernaut =
            selectLocalImageModelPreset(
                root);

        require(
            juggernaut.id == "juggernaut-xl",
            "Juggernaut should outrank Pony in auto mode");
    }


    // RealVis outranks Juggernaut/Pony in Auto.
    touch(
        realVisPath);

    {
        const LocalImageModelPreset realVis =
            selectLocalImageModelPreset(
                root);

        require(
            realVis.id == "realvisxl-v5",
            "RealVisXL should outrank other single-checkpoint models in auto mode");

        require(
            realVis.generatorConfig.modelPath == realVisPath,
            "RealVisXL should configure --model with its Rose-owned checkpoint");

        require(
            realVis.generatorConfig.diffusionModelPath.empty(),
            "RealVisXL should not configure FLUX component arguments");
    }


    // SD1.5 is the last actual installed auto fallback.
    touch(
        sd15Path);


    // -------------------------------------------------------------------------
    // FLUX component completeness / priority
    // -------------------------------------------------------------------------

    touch(
        root
        / "flux2-klein-4b"
        / "diffusion.gguf");

    touch(
        root
        / "flux2-klein-4b"
        / "text_encoder.gguf");

    // Incomplete component folder must NOT partially select FLUX.
    {
        const LocalImageModelPreset incomplete =
            selectLocalImageModelPreset(
                root);

        require(
            incomplete.id == "realvisxl-v5",
            "incomplete FLUX should fall through to the next complete auto model");
    }


    // Explicit incomplete FLUX selection must fail visibly.
    {
        bool threw{ false };

        try
        {
            (void)selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::Flux2Klein4B);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        require(
            threw,
            "explicit incomplete FLUX selection should fail");
    }


    touch(
        root
        / "flux2-klein-4b"
        / "ae.safetensors");

    {
        const LocalImageModelPreset flux =
            selectLocalImageModelPreset(
                root);

        require(
            flux.id == "flux2-klein-4b",
            "complete FLUX component set should retain top auto priority");

        require(
            flux.generatorConfig.modelId == flux.id
                && flux.generatorConfig.modelDisplayName == flux.displayName,
            "FLUX logical model identity should reach the generator config");

        require(
            flux.generatorConfig.modelPath.empty(),
            "FLUX should not configure legacy --model");

        require(
            !flux.generatorConfig.diffusionModelPath.empty(),
            "FLUX should configure --diffusion-model");

        require(
            !flux.generatorConfig.vaePath.empty(),
            "FLUX should configure --vae");

        require(
            !flux.generatorConfig.llmPath.empty(),
            "FLUX should configure --llm");

        require(
            flux.generatorConfig.diffusionFlashAttention,
            "FLUX should enable diffusion flash attention");

        require(
            flux.profiles.standard.steps == 4
                && flux.profiles.standard.cfgScale == 1.0f,
            "FLUX standard profile should retain distilled defaults");
    }


    // -------------------------------------------------------------------------
    // Explicit selection overrides auto priority
    // -------------------------------------------------------------------------

    {
        const LocalImageModelPreset realVis =
            selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::RealVisXLV5);

        require(
            realVis.id == "realvisxl-v5",
            "explicit RealVisXL should override FLUX auto priority");
    }

    {
        const LocalImageModelPreset juggernaut =
            selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::JuggernautXL);

        require(
            juggernaut.id == "juggernaut-xl",
            "explicit Juggernaut should override FLUX auto priority");
    }

    {
        const LocalImageModelPreset pony =
            selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::PonyV6);

        require(
            pony.id == "pony-v6",
            "explicit Pony should override FLUX auto priority");
    }

    {
        const LocalImageModelPreset sd15 =
            selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::StableDiffusion15Fallback);

        require(
            sd15.id == "sd15-fallback",
            "explicit SD1.5 should override FLUX auto priority");
    }


    // Explicit missing single-file checkpoint selection must fail rather than
    // silently substituting a different installed model.
    removeIfPresent(
        realVisPath);

    {
        bool threw{ false };

        try
        {
            (void)selectLocalImageModelPreset(
                root,
                LocalImageModelPreference::RealVisXLV5);
        }
        catch (const std::exception&)
        {
            threw = true;
        }

        require(
            threw,
            "explicit missing RealVisXL selection should fail");
    }


    // Invalid persisted content falls back to Auto but is observable.
    {
        std::ofstream file{
            preferencePath,
            std::ios::binary | std::ios::trunc
        };
        file << "definitely-not-a-model\n";
        file.close();

        std::string warning;
        const LocalImageModelPreference loaded =
            loadLocalImageModelPreference(
                preferencePath,
                &warning);

        require(
            loaded == LocalImageModelPreference::Auto,
            "invalid persisted preference should fall back to auto");

        require(
            !warning.empty(),
            "invalid persisted preference should produce a warning");
    }


    fs::remove_all(
        root,
        ignored);

    {
        const fs::path clipRoot =
            fs::temp_directory_path()
            / "RoseLocalImageModelPresetClipSkipTest";

        fs::remove_all(
            clipRoot,
            ignored);

        touch(
            clipRoot
            / "pony-v6"
            / "model.safetensors");

        const LocalImageModelPreset preset =
            selectLocalImageModelPreset(
                clipRoot,
                LocalImageModelPreference::PonyV6);

        const auto& extra =
            preset.generatorConfig.extraArguments;

        require(
            extra.size() >= 2
                && extra[0] == "--clip-skip"
                && extra[1] == "2",
            "Pony V6 preset should pass --clip-skip 2 to sd-cli");

        fs::remove_all(
            clipRoot,
            ignored);
    }


    std::cout
        << "Rose LocalImageModelPreset tests: PASS\n";

    return 0;
}
