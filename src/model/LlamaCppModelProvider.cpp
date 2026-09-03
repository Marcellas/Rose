#include "model/LlamaCppModelProvider.h"

#include "llama.h"

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rose::model
{

    namespace
    {

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


        // buildChatPrompt()
        // -----------------------------------------------------------------------------
        // Uses the chat template embedded in the GGUF model.
        //
        // Different model families expect different control tokens:
        //
        //     ChatML
        //     Mistral
        //     Gemma
        //     Llama
        //     etc.
        //
        // Rose should NOT hard-code those formats.
        //
        // Instead, the model provider asks llama.cpp for the template stored inside
        // the model and formats a generic "user" message.
        //
        // Later our Message structures will allow system/user/assistant/tool messages
        // to be supplied here instead of this single-message version.
        [[nodiscard]]
        std::string buildChatPrompt(
            const llama_model* model,
            std::string_view userText)
        {
            const char* chatTemplate =
                llama_model_chat_template(model, nullptr);

            if (chatTemplate == nullptr)
            {
                throw std::runtime_error{
                    "The selected GGUF model does not contain a chat template."
                };
            }

            // llama_chat_message stores raw C string pointers, so this std::string must
            // remain alive during both calls to llama_chat_apply_template().
            const std::string userMessage{ userText };

            const llama_chat_message message{
                "user",
                userMessage.c_str()
            };

            // First call: ask llama.cpp how much space the formatted prompt requires.
            const int32_t requiredSize =
                llama_chat_apply_template(
                    chatTemplate,
                    &message,
                    1,
                    true,
                    nullptr,
                    0);

            if (requiredSize < 0)
            {
                throw std::runtime_error{
                    "llama.cpp could not apply the model's chat template."
                };
            }

            // Add one spare byte for safety/null termination even though the returned
            // std::string is constructed from an explicit length.
            std::vector<char> buffer(
                static_cast<std::size_t>(requiredSize) + 1);

            const int32_t written =
                llama_chat_apply_template(
                    chatTemplate,
                    &message,
                    1,
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

            if (config.maxGeneratedTokens <= 0)
            {
                throw std::invalid_argument{
                    "Maximum generated token count must be greater than zero."
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
        std::string generate(std::string_view input)
        {
            if (input.empty())
            {
                return {};
            }

            // ---------------------------------------------------------------------
            // 1. Format the generic user message using the model's own chat format.
            // ---------------------------------------------------------------------

            const std::string prompt =
                buildChatPrompt(model.get(), input);


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
                    config.maxGeneratedTokens);

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
                llama_sampler_init_top_k(20));

            llama_sampler_chain_add(
                sampler.get(),
                llama_sampler_init_top_p(
                    0.95f,
                    1));

            llama_sampler_chain_add(
                sampler.get(),
                llama_sampler_init_temp(
                    0.6f));

            llama_sampler_chain_add(
                sampler.get(),
                llama_sampler_init_dist(
                    LLAMA_DEFAULT_SEED));


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
            //
            // Language models generate ONE TOKEN AT A TIME:
            //
            //     prompt
            //       |
            //       v
            //     model
            //       |
            //       v
            //     logits
            //       |
            //       v
            //     sampler chooses token
            //       |
            //       +-------> token is appended to output
            //       |
            //       +-------> token becomes input for next iteration
            //
            // Repeat until:
            //
            //     - end-of-generation token
            //     - maxGeneratedTokens reached

            std::string response;

            for (
                std::int32_t generated = 0;
                generated < config.maxGeneratedTokens;
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
                    break;
                }

                response +=
                    tokenToText(
                        vocab,
                        nextToken);

                // The token we just generated becomes the next model input.
                batch =
                    llama_batch_get_one(
                        &nextToken,
                        1);
            }

            return response;
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


    std::string LlamaCppModelProvider::generate(
        const std::string_view input)
    {
        return impl_->generate(input);
    }

} // namespace rose::model
