#include "model/LlamaCppModelProvider.h"

#include "llama.h"

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <limits>
#include <iostream>
#include <cctype>

namespace rose::model
{

    namespace
    {

        // ParsedModelOutput
        // -----------------------------------------------------------------------------
        // Internal provider-side representation used while translating model-specific
        // output into Rose's provider-independent ModelResponse.
        //
        // This type never escapes LlamaCppModelProvider.cpp.
        struct ParsedModelOutput
        {
            std::string text;

            std::string reasoning;
        };

        [[nodiscard]]
        std::string trimCopy(
            const std::string_view text)
        {
            std::size_t begin{ 0 };

            while (
                begin < text.size()
                && std::isspace(
                    static_cast<unsigned char>(text[begin])))
            {
                ++begin;
            }


            std::size_t end{ text.size() };

            while (
                end > begin
                && std::isspace(
                    static_cast<unsigned char>(text[end - 1])))
            {
                --end;
            }


            return std::string{
                text.substr(
                    begin,
                    end - begin)
            };
        }


        [[nodiscard]]
        ParsedModelOutput parseModelOutput(
            const std::string_view rawOutput)
        {
            constexpr std::string_view openingTag{ "<think>" };
            constexpr std::string_view closingTag{ "</think>" };


            std::size_t firstContent{ 0 };

            while (
                firstContent < rawOutput.size()
                && std::isspace(
                    static_cast<unsigned char>(
                        rawOutput[firstContent])))
            {
                ++firstContent;
            }


            // If the model did not begin with a Qwen reasoning wrapper,
            // treat the entire result as visible assistant text.
            if (
                rawOutput.substr(
                    firstContent,
                    openingTag.size())
                != openingTag)
            {
                return ParsedModelOutput{
                    .text = trimCopy(rawOutput),
                    .reasoning = {}
                };
            }


            const std::size_t reasoningBegin =
                firstContent + openingTag.size();


            const std::size_t closingPosition =
                rawOutput.find(
                    closingTag,
                    reasoningBegin);


            // If generation ended before </think>, preserve what was generated as
            // reasoning rather than accidentally presenting it as Rose's answer.
            if (closingPosition == std::string_view::npos)
            {
                return ParsedModelOutput{
                    .text = {},
                    .reasoning =
                        trimCopy(
                            rawOutput.substr(
                                reasoningBegin))
                };
            }


            const std::string reasoning =
                trimCopy(
                    rawOutput.substr(
                        reasoningBegin,
                        closingPosition - reasoningBegin));


            const std::size_t answerBegin =
                closingPosition
                + closingTag.size();


            const std::string visibleText =
                trimCopy(
                    rawOutput.substr(
                        answerBegin));


            return ParsedModelOutput{
                .text = visibleText,
                .reasoning = reasoning
            };
        }

        // BackendRuntime
        // -----------------------------------------------------------------------------
        // Controls the process-level initialization required by llama.cpp.
        //
        // llama_backend_init() must happen before using llama.cpp.
        //
        // For the MVP Rose owns exactly one llama.cpp provider at a time, so tying the
        // backend lifetime to that provider keeps ownership easy to understand.
        //
        // FUTURE:
        //
        // If Rose later keeps multiple llama.cpp providers alive simultaneously,
        // backend initialization should move into a small process-level runtime object
        // shared by those providers.
        class BackendRuntime
        {
        public:
            BackendRuntime()
            {
                llama_backend_init();
            }

            ~BackendRuntime()
            {
                llama_backend_free();
            }

            BackendRuntime(const BackendRuntime&) = delete;
            BackendRuntime& operator=(const BackendRuntime&) = delete;
        };


        // -----------------------------------------------------------------------------
        // Custom deleters
        // -----------------------------------------------------------------------------
        //
        // llama.cpp is a C API and returns raw pointers.
        //
        // Instead of manually calling free functions throughout the provider, wrap
        // those pointers in std::unique_ptr.
        //
        // This gives us normal C++ RAII:
        //
        //     acquire resource
        //          |
        //          v
        //     unique_ptr owns it
        //          |
        //          v
        //     scope ends / exception occurs
        //          |
        //          v
        //     correct llama free function is called automatically
        //
        // This is especially important because inference has many failure paths.

        struct ModelDeleter
        {
            void operator()(llama_model* model) const noexcept
            {
                if (model != nullptr)
                {
                    llama_model_free(model);
                }
            }
        };


        struct ContextDeleter
        {
            void operator()(llama_context* context) const noexcept
            {
                if (context != nullptr)
                {
                    llama_free(context);
                }
            }
        };


        struct SamplerDeleter
        {
            void operator()(llama_sampler* sampler) const noexcept
            {
                if (sampler != nullptr)
                {
                    llama_sampler_free(sampler);
                }
            }
        };


        using ModelPtr =
            std::unique_ptr<llama_model, ModelDeleter>;

        using ContextPtr =
            std::unique_ptr<llama_context, ContextDeleter>;

        using SamplerPtr =
            std::unique_ptr<llama_sampler, SamplerDeleter>;


        // tokenToText()
        // -----------------------------------------------------------------------------
        // Converts one llama token ID back into its textual representation.
        //
        // llama.cpp may occasionally require more space than our small stack buffer.
        // When that happens we allocate exactly the required amount dynamically.
        [[nodiscard]]
        std::string tokenToText(
            const llama_vocab* vocab,
            const llama_token token)
        {
            std::array<char, 256> localBuffer{};

            int result = llama_token_to_piece(
                vocab,
                token,
                localBuffer.data(),
                static_cast<int32_t>(localBuffer.size()),
                0,
                true);

            if (result >= 0)
            {
                return std::string{
                    localBuffer.data(),
                    static_cast<std::size_t>(result)
                };
            }

            // A negative return value indicates that the supplied buffer was too
            // small. Its magnitude tells us the required size.
            const auto requiredSize =
                static_cast<std::size_t>(-result);

            std::vector<char> dynamicBuffer(requiredSize);

            result = llama_token_to_piece(
                vocab,
                token,
                dynamicBuffer.data(),
                static_cast<int32_t>(dynamicBuffer.size()),
                0,
                true);

            if (result < 0)
            {
                throw std::runtime_error{
                    "llama.cpp failed to convert a generated token to text."
                };
            }

            return std::string{
                dynamicBuffer.data(),
                static_cast<std::size_t>(result)
            };
        }

        // modelRoleToLlamaRole()
// -----------------------------------------------------------------------------
// Translate Rose's model-independent roles into role names understood by chat
// templates used through llama.cpp.
//
// IMPORTANT:
// Rose itself should never need to know these raw strings.
//
// This translation belongs at the provider boundary.
        [[nodiscard]]
        const char* modelRoleToLlamaRole(
            const ModelRole role)
        {
            switch (role)
            {
            case ModelRole::System:
                return "system";

            case ModelRole::User:
                return "user";

            case ModelRole::Assistant:
                return "assistant";

            case ModelRole::Tool:
                return "tool";
            }

            // Defensive fallback.
            //
            // Every currently defined ModelRole is handled above. Reaching this point
            // therefore means a future enum value was added without updating this
            // provider.
            throw std::logic_error{
                "Unsupported ModelRole in LlamaCppModelProvider."
            };
        }

        // buildChatPrompt()
// -----------------------------------------------------------------------------
// Convert Rose's structured ModelRequest into the chat format embedded in the
// GGUF model.
//
// Rose supplies generic messages:
//
//     System
//     User
//     Assistant
//     Tool
//
// llama.cpp then applies the model-specific template.
//
// This is the boundary that allows the same Rose conversation to eventually be
// sent to Qwen, Llama, Gemma, or another provider without Rose constructing
// model-specific control tokens itself.
        [[nodiscard]]
        std::string buildChatPrompt(
            const llama_model* model,
            const ModelRequest& request)
        {
            if (request.messages.empty())
            {
                throw std::invalid_argument{
                    "Cannot build a model prompt from an empty message list."
                };
            }

            const char* chatTemplate =
                llama_model_chat_template(
                    model,
                    nullptr);

            if (chatTemplate == nullptr)
            {
                throw std::runtime_error{
                    "The selected GGUF model does not contain a chat template."
                };
            }


            // llama_chat_message does NOT own the strings referenced by role/content.
            //
            // The content pointers refer directly to std::strings stored in
            // request.messages.
            //
            // That is safe because:
            //
            //     - request is borrowed for this entire function
            //     - request.messages is not modified
            //     - therefore the std::string storage does not move during template use
            std::vector<llama_chat_message> chatMessages;

            chatMessages.reserve(
                request.messages.size());


            for (const ModelMessage& message : request.messages)
            {
                chatMessages.push_back(
                    llama_chat_message{
                        modelRoleToLlamaRole(message.role),
                        message.content.c_str()
                    });
            }


            // First call:
            // Ask llama.cpp how large the formatted prompt needs to be.
            const int32_t requiredSize =
                llama_chat_apply_template(
                    chatTemplate,
                    chatMessages.data(),
                    chatMessages.size(),
                    true,
                    nullptr,
                    0);

            if (requiredSize < 0)
            {
                throw std::runtime_error{
                    "llama.cpp could not apply the model's chat template."
                };
            }


            std::vector<char> buffer(
                static_cast<std::size_t>(requiredSize) + 1);


            // Second call:
            // Actually format the prompt into our allocated buffer.
            const int32_t written =
                llama_chat_apply_template(
                    chatTemplate,
                    chatMessages.data(),
                    chatMessages.size(),
                    true,
                    buffer.data(),
                    static_cast<int32_t>(buffer.size()));

            if (written < 0)
            {
                throw std::runtime_error{
                    "llama.cpp failed while formatting the chat prompt."
                };
            }


            return std::string{
                buffer.data(),
                static_cast<std::size_t>(written)
            };
        }

    } // namespace


    // LlamaCppModelProvider::Impl
    // -----------------------------------------------------------------------------
    // All llama.cpp-specific state lives here.
    //
    // OWNERSHIP:
    //
    //     Impl
    //       |
    //       +-- BackendRuntime
    //       |
    //       +-- ModelPtr
    //
    // The model is loaded once and remains resident while the provider exists.
    //
    // Contexts and samplers are created separately for each generate() call for
    // now. That keeps requests independent and makes lifecycle behavior obvious.
    struct LlamaCppModelProvider::Impl
    {
        explicit Impl(LlamaCppConfig providerConfig)
            : config{ std::move(providerConfig) }
        {
            if (config.modelPath.empty())
            {
                throw std::invalid_argument{
                    "LlamaCppModelProvider requires a model path."
                };
            }

            if (!std::filesystem::exists(config.modelPath))
            {
                throw std::runtime_error{
                    "Model file does not exist: "
                    + config.modelPath.string()
                };
            }

            if (config.contextSize == 0)
            {
                throw std::invalid_argument{
                    "Model context size must be greater than zero."
                };
            }

            // Configure model loading.
            llama_model_params modelParams =
                llama_model_default_params();

            modelParams.n_gpu_layers = config.gpuLayers;

            // NOTE:
            // std::filesystem::path::string() is sufficient for our initial
            // Windows development path.
            //
            // We should explicitly address full Unicode Windows path handling
            // before Rose's file/model configuration becomes user-facing.
            const std::string modelPath =
                config.modelPath.string();

            model.reset(
                llama_model_load_from_file(
                    modelPath.c_str(),
                    modelParams));

            if (!model)
            {
                throw std::runtime_error{
                    "llama.cpp failed to load model: " + modelPath
                };
            }
        }


        [[nodiscard]]
        ModelResponse generate(
            const ModelRequest& request)
        {
            if (request.messages.empty())
            {
                throw std::invalid_argument{
                    "ModelRequest must contain at least one message."
                };
            }

            if (request.maxGeneratedTokens <= 0)
            {
                throw std::invalid_argument{
                    "ModelRequest maxGeneratedTokens must be greater than zero."
                };
            }

            if (request.sampling.topK <= 0)
            {
                throw std::invalid_argument{
                    "Sampling topK must be greater than zero."
                };
            }

            if (
                request.sampling.topP <= 0.0f ||
                request.sampling.topP > 1.0f)
            {
                throw std::invalid_argument{
                    "Sampling topP must be within the range (0, 1]."
                };
            }

            if (request.sampling.temperature <= 0.0f)
            {
                throw std::invalid_argument{
                    "Sampling temperature must be greater than zero."
                };
            }

            // ---------------------------------------------------------------------
            // 1. Format the generic user message using the model's own chat format.
            // ---------------------------------------------------------------------

            const std::string prompt =
                buildChatPrompt(
                    model.get(),
                    request);

            // -----------------------------------------------------------------------------
// Temporary model-request diagnostics
// -----------------------------------------------------------------------------
//
// This lets us verify that Rose's structured conversation actually reaches the
// model provider.
//
// Once Rose's logging system exists, these messages will become structured
// Verbose-level diagnostic events instead of direct console output.

            std::cerr
                << "\n[Rose debug] Model request contains "
                << request.messages.size()
                << " messages:\n";


            for (const model::ModelMessage& message : request.messages)
            {
                const char* role = "unknown";

                switch (message.role)
                {
                case model::ModelRole::System:
                    role = "system";
                    break;

                case model::ModelRole::User:
                    role = "user";
                    break;

                case model::ModelRole::Assistant:
                    role = "assistant";
                    break;

                case model::ModelRole::Tool:
                    role = "tool";
                    break;
                }


                std::cerr
                    << "\n[" << role << "]\n"
                    << message.content
                    << '\n';
            }

            // ---------------------------------------------------------------------
            // 2. Tokenize the prompt.
            // ---------------------------------------------------------------------

            const llama_vocab* vocab =
                llama_model_get_vocab(model.get());

            if (vocab == nullptr)
            {
                throw std::runtime_error{
                    "Could not retrieve the model vocabulary."
                };
            }

            // Supplying no destination buffer asks llama.cpp to report how many
            // tokens are required. The required capacity is returned as a negative
            // value.
            const int32_t tokenCountResult =
                llama_tokenize(
                    vocab,
                    prompt.data(),
                    static_cast<int32_t>(prompt.size()),
                    nullptr,
                    0,
                    true,
                    true);

            if (tokenCountResult >= 0)
            {
                throw std::runtime_error{
                    "Unexpected token-count result from llama.cpp."
                };
            }

            const auto promptTokenCount =
                static_cast<std::size_t>(-tokenCountResult);

            std::vector<llama_token> promptTokens(
                promptTokenCount);

            const int32_t actualTokenCount =
                llama_tokenize(
                    vocab,
                    prompt.data(),
                    static_cast<int32_t>(prompt.size()),
                    promptTokens.data(),
                    static_cast<int32_t>(promptTokens.size()),
                    true,
                    true);

            if (actualTokenCount < 0)
            {
                throw std::runtime_error{
                    "llama.cpp failed to tokenize the prompt."
                };
            }


            // ---------------------------------------------------------------------
            // 3. Verify that prompt + response fit into our configured context.
            // ---------------------------------------------------------------------

            const auto requiredContext =
                promptTokens.size()
                + static_cast<std::size_t>(
                    request.maxGeneratedTokens);

            if (requiredContext > config.contextSize)
            {
                throw std::runtime_error{
                    "Prompt and requested response exceed Rose's configured "
                    "model context size."
                };
            }


            // ---------------------------------------------------------------------
            // 4. Create an inference context for this request.
            // ---------------------------------------------------------------------
            //
            // The MODEL owns weights.
            //
            // The CONTEXT owns active inference state such as the KV cache.
            //
            // Keeping those concepts separate becomes important when we later add
            // multiple conversations, background agents, and context caching.

            llama_context_params contextParams =
                llama_context_default_params();

            contextParams.n_ctx = config.contextSize;

            // For our MVP we permit an entire prompt to enter in one logical batch.
            //
            // Later we'll use smaller batching for better memory/performance
            // control when processing large retrieved contexts.
            contextParams.n_batch = config.contextSize;

            contextParams.no_perf = true;

            ContextPtr context{
                llama_init_from_model(
                    model.get(),
                    contextParams)
            };

            if (!context)
            {
                throw std::runtime_error{
                    "llama.cpp failed to create an inference context."
                };
            }


            // ---------------------------------------------------------------------
            // 5. Create a sampler.
            // ---------------------------------------------------------------------
            //
            // Qwen3 performs better with probabilistic sampling than with greedy
            // decoding.
            //
            // These values are temporary provider defaults. They should eventually
            // move into ModelRequest / SamplingConfig so Rose can choose generation
            // behavior without hard-coding it into the llama.cpp provider.
            //
            // Sampling order:
            //
            //     logits
            //       |
            //       v
            //     top-k
            //       |
            //       v
            //     top-p
            //       |
            //       v
            //     temperature
            //       |
            //       v
            //     distribution sampler

            llama_sampler_chain_params samplerParams =
                llama_sampler_chain_default_params();

            samplerParams.no_perf = true;

            SamplerPtr sampler{
                llama_sampler_chain_init(samplerParams)
            };

            if (!sampler)
            {
                throw std::runtime_error{
                    "llama.cpp failed to create a sampler."
                };
            }

            llama_sampler_chain_add(
                sampler.get(),
                llama_sampler_init_top_k(
                    request.sampling.topK));

            llama_sampler_init_top_p(
                request.sampling.topP,
                1);

            llama_sampler_chain_add(
                sampler.get(),
                llama_sampler_init_temp(
                    request.sampling.temperature));

                const std::uint32_t samplerSeed =
                request.sampling.seed.value_or(
                    LLAMA_DEFAULT_SEED);

                llama_sampler_chain_add(
                    sampler.get(),
                    llama_sampler_init_dist(
                        samplerSeed));


                // ---------------------------------------------------------------------
                // 6. Feed the prompt into the model.
                // ---------------------------------------------------------------------

                llama_batch batch =
                    llama_batch_get_one(
                        promptTokens.data(),
                        static_cast<int32_t>(promptTokens.size()));


                // ---------------------------------------------------------------------
                // 7. Autoregressive generation loop.
                // ---------------------------------------------------------------------

                // Raw provider output.
                //
                // This may contain model-specific protocol text such as Qwen's <think> wrapper.
                // It is parsed before anything becomes part of Rose's normal conversation.
                std::string rawResponse;

                // Number of normal output tokens actually appended to the response.
                //
                // Use std::int32_t rather than plain int because ModelResponse also stores
                // generatedTokens as std::int32_t. Keeping the types identical avoids
                // unnecessary conversions later.
                std::int32_t generatedTokenCount{ 0 };

                // Records HOW generation stopped.
                //
                // false:
                //     We exhausted maxGeneratedTokens.
                //
                // true:
                //     The model emitted an end-of-generation token.
                bool reachedEndOfGeneration{ false };


                for (
                    std::int32_t generated = 0;
                    generated < request.maxGeneratedTokens;
                    ++generated)
                {
                    const int decodeResult =
                        llama_decode(
                            context.get(),
                            batch);

                    if (decodeResult != 0)
                    {
                        throw std::runtime_error{
                            "llama.cpp failed while decoding."
                        };
                    }


                    llama_token nextToken =
                        llama_sampler_sample(
                            sampler.get(),
                            context.get(),
                            -1);


                    if (llama_vocab_is_eog(vocab, nextToken))
                    {
                        reachedEndOfGeneration = true;
                        break;
                    }


                    rawResponse +=
                        tokenToText(
                            vocab,
                            nextToken);

                    ++generatedTokenCount;


                    // The generated token becomes the model input for the next iteration.
                    batch =
                        llama_batch_get_one(
                            &nextToken,
                            1);
                }


                // ---------------------------------------------------------------------
                // 8. Separate model-specific reasoning from the visible response.
                // ---------------------------------------------------------------------
                //
                // Qwen may emit:
                //
                //     <think>
                //     internal reasoning
                //     </think>
                //
                //     Visible answer
                //
                // parseModelOutput() converts that provider-specific representation into
                // Rose's provider-independent text + reasoning fields.
                ParsedModelOutput parsedOutput =
                    parseModelOutput(rawResponse);

                // ---------------------------------------------------------------------
                // 9. Return structured generation information.
                // ---------------------------------------------------------------------

                return ModelResponse{
                    .text = std::move(parsedOutput.text),
                    .reasoning = std::move(parsedOutput.reasoning),
                    .generatedTokens = generatedTokenCount,
                    .finishReason =
                        reachedEndOfGeneration
                            ? ModelFinishReason::EndOfGeneration
                            : ModelFinishReason::TokenLimit
                };
        }


        // -------------------------------------------------------------------------
        // Member order matters here.
        //
        // C++ destroys members in REVERSE declaration order.
        //
        // Therefore:
        //
        //     model is destroyed first
        //     backend is destroyed afterward
        //
        // This guarantees llama_model_free() runs while llama.cpp is still active.
        // -------------------------------------------------------------------------

        LlamaCppConfig config;

        BackendRuntime backend;

        ModelPtr model{ nullptr };
    };


    // =============================================================================
    // LlamaCppModelProvider public interface
    // =============================================================================

    LlamaCppModelProvider::LlamaCppModelProvider(
        LlamaCppConfig config)
        : impl_{
            std::make_unique<Impl>(
                std::move(config))
        }
    {
    }


    LlamaCppModelProvider::~LlamaCppModelProvider() = default;


    ModelResponse LlamaCppModelProvider::generate(
        const ModelRequest& request)
    {
        return impl_->generate(request);
    }

} // namespace rose::model
