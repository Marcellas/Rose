if(NOT DEFINED ROSE_SOURCE_DIR)
    message(FATAL_ERROR "ROSE_SOURCE_DIR was not provided.")
endif()

function(rose_require_source_text relative_path needle description)
    set(path "${ROSE_SOURCE_DIR}/${relative_path}")

    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "Rose regression check failed: missing ${relative_path} (${description}).")
    endif()

    file(READ "${path}" contents)
    string(FIND "${contents}" "${needle}" position)

    if(position EQUAL -1)
        message(FATAL_ERROR
            "Rose regression check failed: ${description}\n"
            "Expected '${needle}' in ${relative_path}.")
    endif()
endfunction()

# UI -> worker attachment path.
rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "SDL_EVENT_DROP_FILE"
    "chat drag/drop file handling")

rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "submitUserSubmission"
    "structured text + attachment submission")

rose_require_source_text(
    "src/ui/ChatBridge.h"
    "input::UserSubmission"
    "UserSubmission thread-boundary payload")

rose_require_source_text(
    "src/main.cpp"
    "attachmentIngestion.ingest"
    "worker-side attachment ingestion")

# Worker -> UI artifact path.
rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "ChatEventType::ArtifactReady"
    "ArtifactReady UI consumption")

rose_require_source_text(
    "src/ui/ArtifactCardStack.cpp"
    "IMG_LoadTexture"
    "generated-image preview loading")

rose_require_source_text(
    "src/ui/ArtifactCardStack.cpp"
    "ShellExecuteW"
    "artifact open/reveal integration")

rose_require_source_text(
    "src/main.cpp"
    "ArtifactReady"
    "worker-to-UI artifact event publication")

# Agent safety/routing baseline.
rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "CapabilityRoutingGuard"
    "registered-capability routing hardening")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "RequiresConfirmation"
    "confirmation pause in the bounded agent loop")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "registered-tool shorthand"
    "tolerant normalization of local-model ACTION=<tool id> shorthand")

# Composer/editor baseline.
rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "undoInputEdit"
    "composer Undo/Redo")

rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "recallPreviousInput"
    "submitted-prompt recall")

# Provider-neutral content policy baseline.
rose_require_source_text(
    "src/policy/ContentPolicy.h"
    "DevelopmentUnrestricted"
    "developer content mode definition")

rose_require_source_text(
    "src/policy/ContentPolicy.cpp"
    "SubjectLifeStage::Juvenile"
    "non-configurable juvenile maturity hard stop")

rose_require_source_text(
    "src/tools/GenerateImageTool.cpp"
    "evaluateImagePrompt"
    "content-policy enforcement before image provider dispatch")

rose_require_source_text(
    "src/tools/GenerateImageRegisteredTool.cpp"
    "subject_maturity"
    "structured adult-for-species maturity metadata")

rose_require_source_text(
    "src/core/RoseCore.cpp"
    "systemPromptFragment"
    "provider-neutral content-mode guidance for normal conversation")


# Image-generation intent/profile baseline.
rose_require_source_text(
    "src/tools/GenerateImageRegisteredTool.cpp"
    ".name = \"quality\""
    "intent-level image quality tool argument")

rose_require_source_text(
    "src/tools/GenerateImageRegisteredTool.cpp"
    ".name = \"aspect_ratio\""
    "intent-level image aspect-ratio tool argument")

rose_require_source_text(
    "src/imagegen/ImageGenerationProfiles.cpp"
    "makeStableDiffusion15Profiles"
    "provider/model-specific image profile set")

rose_require_source_text(
    "src/tools/GenerateImageTool.cpp"
    "resolveImageGenerationIntent"
    "image intent resolution before provider dispatch")



# Modern local image model integration.
rose_require_source_text(
    "src/imagegen/StableDiffusionCliGenerator.cpp"
    "L\"--diffusion-model\""
    "componentized modern image-model CLI support")

rose_require_source_text(
    "src/imagegen/StableDiffusionCliGenerator.cpp"
    "L\"--llm\""
    "modern image text-encoder CLI support")

rose_require_source_text(
    "src/imagegen/StableDiffusionCliGenerator.cpp"
    "L\"--vae\""
    "modern image VAE CLI support")

rose_require_source_text(
    "src/imagegen/LocalImageModelPreset.cpp"
    "flux2-klein-4b"
    "FLUX.2 Klein local-model auto-selection")

rose_require_source_text(
    "src/main.cpp"
    "selectLocalImageModelPreset"
    "startup local image-model selection")

rose_require_source_text(
    "src/main.cpp"
    "/imagemodel"
    "persistent explicit local image-model preference command")

rose_require_source_text(
    "src/imagegen/LocalImageModelPreference.cpp"
    "sd15-fallback"
    "persisted local image-model preference support")



# Image artifact provenance / backend diagnostics.
rose_require_source_text(
    "src/imagegen/StableDiffusionCliGenerator.cpp"
    ".sdcli.log.txt"
    "artifact-adjacent stable-diffusion.cpp process log retention")

rose_require_source_text(
    "src/imagegen/StableDiffusionCliGenerator.cpp"
    "model_topology"
    "image backend model-topology provenance")

rose_require_source_text(
    "src/tools/GenerateImageTool.cpp"
    "diagnostic_log_path"
    "generated-image metadata link to retained provider diagnostics")

rose_require_source_text(
    "src/tools/GenerateImageTool.cpp"
    "result.provenance"
    "provider-neutral generated-image provenance persistence")



# Local long-term memory + retrieval baseline.
rose_require_source_text(
    "src/main.cpp"
    "data/memory/long-term.rosemem"
    "local durable memory store wiring")

rose_require_source_text(
    "src/tools/RememberMemoryTool.cpp"
    "explicit-user-request"
    "explicit user memory provenance")

rose_require_source_text(
    "src/core/RoseCore.cpp"
    "rose_retrieved_memories"
    "bounded retrieved-memory context injection")

rose_require_source_text(
    "src/memory/LexicalMemoryRetriever.cpp"
    "queryCoverage"
    "offline lexical memory retrieval")

rose_require_source_text(
    "src/memory/MemoryFactNormalizer.cpp"
    "preference:"
    "semantic memory fact normalization")

rose_require_source_text(
    "src/memory/ConversationMemoryCandidateTracker.cpp"
    "conversation-derived:repeated-user-statement"
    "bounded temporary memory candidate promotion")

rose_require_source_text(
    "src/core/RoseCore.cpp"
    "observeUserMessage"
    "post-commit automatic memory observation")


# Safe filesystem organization + directory discovery baseline.
rose_require_source_text(
    "src/main.cpp"
    "MovePathTool"
    "confirmation-gated filesystem move tool registration")

rose_require_source_text(
    "src/tools/MovePathTool.cpp"
    "will not overwrite an existing destination"
    "non-overwriting filesystem move semantics")

rose_require_source_text(
    "src/tools/RecyclePathTool.cpp"
    "FOF_ALLOWUNDO"
    "Windows Recycle Bin deletion semantics")

rose_require_source_text(
    "src/tools/RecyclePathTool.cpp"
    "ToolRisk::Destructive"
    "destructive recycle-path classification")

rose_require_source_text(
    "src/tools/ScanDirectoryTreeTool.cpp"
    "recursive_directory_iterator"
    "bounded recursive directory inventory")

# Bounded whole-directory content analysis + safe batch organization.
rose_require_source_text(
    "src/main.cpp"
    "AnalyzeDirectoryDocumentsTool"
    "whole-directory document analysis registration")

rose_require_source_text(
    "src/tools/AnalyzeDirectoryDocumentsTool.cpp"
    "rose_untrusted_document_excerpt"
    "bounded per-document content evidence")

rose_require_source_text(
    "src/tools/BatchMovePathsTool.cpp"
    "will not overwrite existing destination"
    "non-overwriting batch move preflight")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "analyze_directory_documents"
    "directory content routing away from exact-file reads")

rose_require_source_text(
    "src/agent/ToolConfirmation.cpp"
    "formatBatchMoveOperations"
    "expanded exact batch-move confirmation summary")

rose_require_source_text(
    "src/main.cpp"
    "PlanDirectoryDocumentRenamesTool"
    "context-safe whole-directory rename planning registration")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "rose_rename_plan"
    "compact persistent rename-plan handoff")

rose_require_source_text(
    "src/tools/DocumentRenameMetadata.cpp"
    "formatFilingBasename"
    "deterministic court-filing filename formatting")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "STRUCTURED_RESULTS"
    "persistent per-document rename-analysis results")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "expanded-retry"
    "bounded expanded-evidence classifier retry")

rose_require_source_text(
    "src/tools/AnalyzeDirectoryDocumentsTool.cpp"
    "[signal-context]"
    "adjacent filing-stamp evidence retention")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "field-recovery"
    "focused filing metadata ambiguity recovery")

rose_require_source_text(
    "src/tools/DocumentRenameMetadata.cpp"
    "parseFilingDateTimeProtocol"
    "independent filing date/time protocol parser")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "canonical_filename_pattern"
    "canonical filing filename summary contract")

rose_require_source_text(
    "src/tools/ApplyRenamePlanTool.cpp"
    "ROSE_RENAME_PLAN_V1"
    "validated Rose-owned rename-plan execution")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "plan_directory_document_renames"
    "directory rename recovery avoids exact-file fallback")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Using the compact Rose-owned rename plan"
    "context-safe planner-to-apply transition")

rose_require_source_text(
    "src/agent/ToolConfirmation.cpp"
    "formatRenamePlanPreview"
    "rename-plan confirmation scope preview")


# Binary/source identity diagnostics.
rose_require_source_text(
    "src/main.cpp"
    "currentBuildIdentity"
    "startup binary/source identity")

rose_require_source_text(
    "src/main.cpp"
    "submittedText == \"/build\""
    "chat-visible build identity diagnostic")

rose_require_source_text(
    "src/core/BuildIdentity.cpp"
    "ROSE_BUILD_GIT_REVISION"
    "generated Git revision consumption")

rose_require_source_text(
    "scripts/GenerateRoseBuildIdentity.cmake"
    "status"
    "fresh Git dirty-state generation")


rose_require_source_text(
    "src/tools/GenerateImageRegisteredTool.cpp"
    "AuthoritativeCompletion"
    "image generation authoritative completion mode")

rose_require_source_text(
    "src/main.cpp"
    "commitAuthoritativeTurn"
    "authoritative tool response persistence")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "authoritativeResponseIfComplete"
    "agent completion authority propagation")

rose_require_source_text(
    "src/tools/GenerateImageRegisteredTool.cpp"
    "Generated the image successfully with profile"
    "image profile diagnostics in tool completion")


# Multi-state animated avatar presentation. Runtime consumes pre-baked PNG atlases
# so WebM authoring assets never add a video-decoder dependency to the MVP.
rose_require_source_text(
    "src/avatar/SdlAvatar.h"
    "loadAnimationAtlas"
    "state animation atlas loading")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "RoseIdleBook.atlas.png"
    "animated idle presentation")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "RoseTalking.atlas.png"
    "speaking-state animation mapping")

rose_require_source_text(
    "src/main.cpp"
    "loadDefaultAnimations"
    "avatar animation startup wiring")

rose_require_source_text(
    "src/avatar/AvatarPresentationTimeline.cpp"
    "AvatarPresentationPhase::StandToSit"
    "posture-aware stand-to-sit presentation transition")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "RoseStandup.atlas.png"
    "forward/reverse posture transition asset wiring")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "frameBlendForElapsed"
    "low-FPS avatar frame timing")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "clip.frameTextures.push_back(std::move(frameTexture))"
    "isolated textures for atlas cells")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "clip->frameTextures[static_cast<std::size_t>(renderedFrameIndex_)]"
    "one displayed source frame without cross-fade ghosts")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "2.0f,\n            true);"
    "looping seated idle animation")

rose_require_source_text(
    "src/main.cpp"
    "Keyboard/chat waiting is a resting state"
    "text-chat idle posture instead of perpetual listening")


# Batch 17 deterministic court-filing extraction and contextual OCR repair.
rose_require_source_text(
    "src/tools/ContextualOcrRepair.cpp"
    "legal-phrase-context"
    "context-aware OCR repair provenance")

rose_require_source_text(
    "src/tools/CourtFilingMetadataExtractor.cpp"
    "filingSignal"
    "deterministic filing-stamp extraction")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "resolved_deterministically"
    "deterministic filing metadata planner telemetry")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "FilingClassifier"
    "bounded classifier response diagnostics")


# Batch 18 canonical filing titles and low-reasoning classifier mode.
rose_require_source_text(
    "src/tools/CourtFilingMetadataExtractor.cpp"
    "canonicalizeCourtFilingType"
    "canonical court filing title boundary validation")

rose_require_source_text(
    "src/tools/CourtFilingMetadataExtractor.cpp"
    "candidate resembles prose or does not match filing-title grammar"
    "prose rejection before rename planning")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "/no_think"
    "bounded Qwen classifier reasoning control")

rose_require_source_text(
    "src/tools/PlanDirectoryDocumentRenamesTool.cpp"
    "filing type failed canonical-title validation"
    "model filing-type canonical validation")

# Batch 19 desktop interaction shell. Pointer behavior stays separate from chat,
# dragging requires intentional movement, and right-click exposes Rose-owned UI.
rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "dragThresholdPixels_"
    "avatar click-vs-drag threshold")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "ContextMenuRequested"
    "avatar right-click context-menu gesture")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "Demonstrate animations"
    "desktop capability context menu")

rose_require_source_text(
    "src/main.cpp"
    "Closing chat now means \"hide chat\""
    "presence-first hidden/reopenable chat behavior")

rose_require_source_text(
    "src/avatar/AvatarPreviewController.cpp"
    "startDemo"
    "integrated avatar animation demonstration")

# Batch 20 project/discussion persistence foundation.
rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "createDiscussion"
    "persistent discussion catalog")

rose_require_source_text(
    "src/persistence/DiscussionConversationStore.cpp"
    "selectDiscussion"
    "per-discussion conversation store adapter")

# Batch 21 desktop workspace integration. UI commands cross the existing worker
# boundary, persistent discussions can replace the visible transcript, and the
# desktop menu uses compact fly-out categories instead of another flat list.
rose_require_source_text(
    "src/ui/ChatBridge.cpp"
    "waitForWorkerRequest"
    "UI-to-worker workspace command queue")

rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "ConversationReplaced"
    "persistent discussion transcript replacement")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "MenuCategory::Projects"
    "desktop fly-out project menu")

rose_require_source_text(
    "src/main.cpp"
    "workspaceSnapshotSince"
    "worker-owned workspace snapshot handoff")

# Batch 22 persistent local background reminders. Background execution is
# deliberately reminder-only and posts through a distinct Notification event.
rose_require_source_text(
    "src/jobs/PersistentJobScheduler.cpp"
    "repository.dueJobIds"
    "durable background scheduler")

rose_require_source_text(
    "src/main.cpp"
    "ChatEventType::Notification"
    "background reminder notification path")

# Batch 23 optional-integration security foundation. Non-secret capability gates
# are persisted locally while provider secrets stay behind the OS credential vault.
rose_require_source_text(
    "src/integrations/IntegrationPermissionRepository.cpp"
    "setAllowed"
    "fail-closed local integration capability gate")

rose_require_source_text(
    "src/integrations/WindowsCredentialVault.cpp"
    "CRED_TYPE_GENERIC"
    "Windows Credential Manager secret boundary")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "Integrations / permissions"
    "desktop integration security entry")

# Batch 24 read-only Outlook provider. OAuth/Graph work stays off the UI/model
# workers, refresh credentials remain in Credential Manager, and mail operations
# are still guarded by the local mail.read capability.
rose_require_source_text(
    "src/integrations/OutlookIntegrationWorker.cpp"
    "std::stop_token"
    "dedicated Outlook integration worker")

rose_require_source_text(
    "src/integrations/OutlookClient.h"
    "integrations.outlook.refresh-token"
    "secure Outlook refresh-token vault key")

rose_require_source_text(
    "src/main.cpp"
    "Enable it with /integration allow outlook mail.read"
    "local Outlook mail.read gate enforcement")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "Outlook mail"
    "desktop Outlook entry")

# Persistent project knowledge + extensible content-reading baseline.
rose_require_source_text(
    "src/main.cpp"
    "data/knowledge/project-knowledge.roseidx"
    "local durable project-knowledge index wiring")

rose_require_source_text(
    "src/main.cpp"
    "formatProjectKnowledgeContext"
    "bounded active-project retrieval context injection")

rose_require_source_text(
    "src/main.cpp"
    "not instructions"
    "project-source prompt-injection boundary")

rose_require_source_text(
    "src/knowledge/ProjectContentReader.h"
    "IProjectContentReader"
    "format-neutral project content reader extension point")

rose_require_source_text(
    "src/knowledge/Utf8TextProjectContentReader.cpp"
    ".vcxproj"
    "Visual Studio project-file knowledge support")

rose_require_source_text(
    "src/knowledge/ProjectKnowledgeWorker.cpp"
    "std::stop_token"
    "cancellable background project indexing")

# Batch 26 shared local tool execution + persistent structured journal.
rose_require_source_text(
    "src/agent/ToolExecutionService.cpp"
    "Tool execution started."
    "shared permission/audit execution boundary")

rose_require_source_text(
    "src/agent/FileAgentJournalStore.cpp"
    "ROSE_AGENT_JOURNAL_V1"
    "persistent bounded agent execution journal")

rose_require_source_text(
    "src/knowledge/ProjectKnowledgeCommand.cpp"
    "/knowledge remove-root"
    "reversible project knowledge root approval")

rose_require_source_text(
    "src/tools/ToolRegistry.cpp"
    "removeTool"
    "reversible dynamic tool registration")

# Current add/remove symmetry contract. New public add-style APIs should receive an
# intentional inverse unless the domain genuinely has no reversible counterpart.
rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "removeProjectApprovedRoot"
    "add/remove project approved-root symmetry")

rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "removeProjectIndexedDocument"
    "add/remove indexed-document symmetry")

rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "removeProjectMemoryRecord"
    "add/remove project-memory symmetry")

rose_require_source_text(
    "src/ui/SdlChatWindow.cpp"
    "removeDroppedFile"
    "add/remove staged attachment symmetry")

# Batch 27 reversible workspace lifecycle. User-visible Add/Create operations have
# a non-destructive Remove path plus Restore, and removed records are excluded from
# normal activation while their durable metadata/transcripts remain intact.
rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "restoreDiscussion"
    "reversible discussion removal")

rose_require_source_text(
    "src/workspace/WorkspaceRepository.cpp"
    "restoreProject"
    "reversible project removal")

rose_require_source_text(
    "src/workspace/FileWorkspaceStore.cpp"
    "oldestSupportedFormatVersion"
    "backward-compatible workspace soft-removal schema")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "REMOVED DISCUSSIONS"
    "desktop discussion restore surface")

rose_require_source_text(
    "src/ui/SdlRoseContextMenu.cpp"
    "REMOVED PROJECTS"
    "desktop project restore surface")

rose_require_source_text(
    "src/workspace/WorkspaceCommand.cpp"
    "/discussion remove"
    "workspace discussion lifecycle commands")

# Batch 28 format-aware document/image understanding.
rose_require_source_text(
    "src/files/FileFormatCatalog.cpp"
    "OfficeSpreadsheetOpenXml"
    "central file-format recognition including modern Office")

rose_require_source_text(
    "src/documents/OpenXmlDocumentExtractor.cpp"
    "sheet="
    "structured Word/Excel/PowerPoint Open XML extraction")

rose_require_source_text(
    "src/knowledge/ProjectContentReader.cpp"
    "ProjectContentReaderRegistry::remove"
    "add/remove symmetry for project content readers")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    "read_pdf"
    "agent-readable PDF tool")

rose_require_source_text(
    "src/tools/ReadOfficeDocumentRegisteredTool.cpp"
    "read_office_document"
    "agent-readable Office Open XML tool")

rose_require_source_text(
    "src/tools/InspectImageRegisteredTool.cpp"
    "inspect_image"
    "on-demand semantic image inspection tool")

# Batch 28 large-document context safety. Large PDF/Office reads must be
# hierarchically condensed before they re-enter the bounded Agent context.
rose_require_source_text(
    "src/documents/ContextSafeDocumentSynthesizer.cpp"
    "reductionGroupSize"
    "hierarchical context-safe document synthesis")

rose_require_source_text(
    "src/tools/ReadOfficeDocumentRegisteredTool.cpp"
    "content_mode="
    "Office reader context-safe completion metadata")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    "content_mode="
    "PDF reader context-safe completion metadata")

# Batch 28 routing follow-up. Exact-file analysis language such as "identify
# every ..." must not be mistaken for a directory batch, and local models may
# not manufacture absolute paths for exact-file readers.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "every file"
    "explicit directory-scope routing rather than generic quantifiers")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "pathArgumentGrounded"
    "model-invented exact-file path rejection")

# Batch 29 project-scoped bare filename resolution.
rose_require_source_text(
    "src/files/ProjectFileResolver.cpp"
    "active_project_approved_roots"
    "project-scoped file reference resolution")

rose_require_source_text(
    "src/main.cpp"
    "projectFileResolver.resolve"
    "active project file-resolution context injection")


# Batch 30 ZIP archive lifecycle + manifest understanding.
rose_require_source_text(
    "src/archives/ZipArchiveService.cpp"
    "ZIP entry escapes extraction root."
    "ZIP path-traversal-safe extraction preflight")

rose_require_source_text(
    "src/tools/CreateZipArchiveTool.cpp"
    "create_zip_archive"
    "confirmation-gated ZIP creation tool")

rose_require_source_text(
    "src/tools/ExtractZipArchiveTool.cpp"
    "extract_zip_archive"
    "confirmation-gated ZIP extraction tool")

rose_require_source_text(
    "src/tools/AttachmentIngestion.cpp"
    "ZIP MANIFEST BEGIN"
    "ZIP attachment manifest understanding")

rose_require_source_text(
    "src/knowledge/ZipArchiveProjectContentReader.cpp"
    "archive/zip-manifest"
    "project knowledge ZIP manifest reader")

# Batch 31 local media understanding. Media inspection stays optional/runtime-
# detected, samples bounded representative frames, and never pretends that visual
# keyframes are a full spoken-audio transcript.
rose_require_source_text(
    "src/media/MediaService.cpp"
    "FFmpeg media inspection is unavailable"
    "optional local FFmpeg media backend")

rose_require_source_text(
    "src/tools/InspectMediaRegisteredTool.cpp"
    "representative visual frames"
    "bounded semantic video/animated-image inspection")

rose_require_source_text(
    "src/media/MediaService.cpp"
    "contact-sheet.png"
    "single contact-sheet media sampling pass")

rose_require_source_text(
    "src/tools/InspectMediaRegisteredTool.cpp"
    "rose_untrusted_media_contact_sheet"
    "single semantic vision pass over representative media frames")

rose_require_source_text(
    "src/knowledge/MediaProjectContentReader.cpp"
    "Semantic frame analysis is intentionally performed on demand"
    "metadata-only project media indexing")

rose_require_source_text(
    "src/knowledge/ProjectKnowledgeIndexer.cpp"
    "usesWholeFileByteBudget"
    "streaming metadata readers bypass the in-memory document byte budget")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "inspect_media"
    "format-aware direct media routing")


# Batch 32 native Windows media fallback. FFmpeg remains preferred, but ordinary
# Windows media inspection must not hard-fail merely because ffmpeg.exe is absent.
rose_require_source_text(
    "src/media/MediaService.cpp"
    "MFCreateSourceReaderFromURL"
    "Windows Media Foundation local media fallback")

rose_require_source_text(
    "src/media/MediaService.cpp"
    "WindowsMediaFoundationService"
    "native Windows media backend ownership")

rose_require_source_text(
    "src/media/MediaService.cpp"
    "LocalMediaService::inspect"
    "FFmpeg-to-native media fallback chain")

rose_require_source_text(
    "src/media/MediaService.cpp"
    "CreateStreamOnHGlobal"
    "in-memory native contact-sheet PNG encoding")

rose_require_source_text(
    "src/main.cpp"
    "rose::media::LocalMediaService mediaService"
    "provider-neutral local media service wiring")


# Batch 33 read-only databases + Windows shortcuts. Understanding a database or
# shortcut never implies write/launch authority.
rose_require_source_text(
    "src/tools/InspectDatabaseRegisteredTool.cpp"
    "bounded and does not imply the entire database"
    "bounded read-only database inspection")

rose_require_source_text(
    "src/database/DatabaseService.cpp"
    "SQLITE_OPEN_READONLY_VALUE"
    "SQLite read-only backend enforcement")

rose_require_source_text(
    "src/tools/InspectShortcutRegisteredTool.cpp"
    "did not launch"
    "shortcut inspection without target launch")

rose_require_source_text(
    "src/shortcuts/ShortcutService.cpp"
    "STGM_READ"
    "Windows Shell Link read-only loading")

rose_require_source_text(
    "src/knowledge/ProjectContentReader.cpp"
    "DatabaseProjectContentReader"
    "Project Knowledge database schema reader registration")

rose_require_source_text(
    "src/knowledge/ProjectContentReader.cpp"
    "ShortcutProjectContentReader"
    "Project Knowledge shortcut metadata reader registration")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "inspect_database"
    "format-aware direct database routing")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "inspect_shortcut"
    "format-aware direct shortcut routing")

# Batch 34 controlled process effects. Launch/close are explicit capabilities;
# close stays graceful-only and never hides force termination behind a friendly name.
rose_require_source_text(
    "src/process/ProcessService.cpp"
    "CreateProcessW"
    "exact Windows process launch backend")

rose_require_source_text(
    "src/process/ProcessService.cpp"
    "PostMessageW(window, WM_CLOSE"
    "graceful Windows process close boundary")

rose_require_source_text(
    "src/tools/LaunchProgramTool.cpp"
    ".risk = ToolRisk::ExternalEffect"
    "launch_program external-effect classification")

rose_require_source_text(
    "src/tools/CloseProcessTool.cpp"
    ".risk = ToolRisk::Destructive"
    "close_process confirmation-forced risk classification")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded close_process pid"
    "process close PID grounding")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "launch_program"
    "direct exact-program launch routing")

message(STATUS "Rose cumulative source regression contract: PASS")

# Batch 35 controlled Office mutation. Reading and writing remain separate
# capabilities; every structural add has an explicit inverse and mutations are
# performed against a private copy before Rose replaces the original.
rose_require_source_text(
    "src/documents/OfficeDocumentMutationService.cpp"
    "replaceFileTransactionally"
    "transactional Office edit replacement boundary")

rose_require_source_text(
    "src/tools/CreateOfficeDocumentTool.cpp"
    ".risk = ToolRisk::LocalWrite"
    "Office creation local-write classification")

rose_require_source_text(
    "src/tools/EditOfficeDocumentTool.cpp"
    "append_word_text/remove_word_text"
    "Word append/remove symmetry")

rose_require_source_text(
    "src/tools/EditOfficeDocumentTool.cpp"
    "set_excel_cell/clear_excel_cell"
    "Excel set/clear symmetry")

rose_require_source_text(
    "src/tools/EditOfficeDocumentTool.cpp"
    "append_powerpoint_slide/remove_powerpoint_slide"
    "PowerPoint append/remove symmetry")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit Office mutation request."
    "Office mutation grounding and intent guard")

message(STATUS "Rose Batch 35 Office mutation regression contract: PASS")

# Batch 36 controlled PDF mutation. Reading and writing remain separate;
# structural/text/annotation add operations have explicit inverse paths and
# existing PDFs are replaced only after a successful sibling-temp save.
rose_require_source_text(
    "src/documents/PdfDocumentMutationService.cpp"
    "transactionalReplace"
    "transactional PDF replacement boundary")

rose_require_source_text(
    "src/documents/PdfiumRuntime.cpp"
    "gPdfiumLeaseCount"
    "shared PDFium runtime lifetime")

rose_require_source_text(
    "src/tools/CreatePdfDocumentTool.cpp"
    ".risk = ToolRisk::LocalWrite"
    "PDF creation local-write classification")

rose_require_source_text(
    "src/tools/EditPdfDocumentTool.cpp"
    "append_text_page/remove_page_range"
    "PDF page add/remove symmetry")

rose_require_source_text(
    "src/tools/EditPdfDocumentTool.cpp"
    "add_text_to_page/remove_page_object"
    "PDF text object add/remove symmetry")

rose_require_source_text(
    "src/tools/EditPdfDocumentTool.cpp"
    "add_text_annotation/remove_annotation"
    "PDF annotation add/remove symmetry")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit PDF mutation request."
    "PDF mutation grounding and intent guard")

message(STATUS "Rose Batch 36 PDF mutation regression contract: PASS")

# Batch 38 controlled UTF-8 text/source mutation. Exact-file reading and writing
# remain separate; replace/remove fail closed on missing or ambiguous matches and
# the service stages a fully written sibling before replacing the original.
rose_require_source_text(
    "src/files/TextFileMutationService.cpp"
    "uniqueOccurrenceOffset"
    "unique exact-match text mutation precondition")

rose_require_source_text(
    "src/files/TextFileMutationService.cpp"
    "ReplaceFileW"
    "transactional Windows text replacement boundary")

rose_require_source_text(
    "src/tools/EditTextFileTool.cpp"
    ".risk = ToolRisk::LocalWrite"
    "text mutation local-write classification")

rose_require_source_text(
    "src/tools/EditTextFileTool.cpp"
    "replace_text, append_text, remove_text"
    "text replace/append/remove mutation surface")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit text mutation request."
    "text mutation path and intent grounding")

rose_require_source_text(
    "src/main.cpp"
    "EditTextFileTool"
    "text mutation tool registration")

message(STATUS "Rose Batch 38 text mutation regression contract: PASS")

# Batch 39 controlled CMake build execution. Source mutation and execution stay
# separate permission boundaries; builds use cmake.exe directly, require an
# existing source-matched build tree, capture bounded diagnostics, and remain
# confirmation-gated because project build rules may execute code.
rose_require_source_text(
    "src/development/BoundedProcessRunner.cpp"
    "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE"
    "bounded Windows build-process lifetime")

rose_require_source_text(
    "src/development/ConfiguredCMakeProject.cpp"
    "CMAKE_HOME_DIRECTORY:INTERNAL="
    "source-matched configured CMake build tree")

rose_require_source_text(
    "src/tools/BuildCMakeProjectTool.cpp"
    ".risk = ToolRisk::ExternalEffect"
    "CMake build external-effect classification")

rose_require_source_text(
    "src/tools/BuildCMakeProjectTool.cpp"
    "ToolResponseMode::RequiresModelSynthesis"
    "compiler diagnostics returned to the coding agent")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit CMake build request."
    "CMake build source and intent grounding")

rose_require_source_text(
    "src/main.cpp"
    ".maximumToolExecutions ="
    "bounded one-repair coding workflow ceiling")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "Allowed repeated developer validation after an intervening local mutation."
    "bounded post-mutation developer validation retry")

message(STATUS "Rose Batch 39 controlled CMake build regression contract: PASS")

# Batch 39 hotfix: deterministic recovery must preserve explicit build options
# and must never reinterpret a CMake project directory as read_text_file after
# a build request or a non-executing build explanation.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "request.arguments.emplace(\"target\", *target)"
    "deterministic CMake recovery preserves explicit target")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "request.arguments.emplace(\"jobs\", *jobs)"
    "deterministic CMake recovery preserves explicit job count")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "suppressDevelopmentPathFileRecovery"
    "CMake project directories cannot fall through to text-file recovery")

message(STATUS "Rose Batch 39 CMake recovery hotfix regression contract: PASS")


# Batch 40 controlled CTest execution. Build and test execution share one private
# bounded process primitive, while ctest remains a separate confirmation-gated
# capability so compilation success is never silently equated with test success.
rose_require_source_text(
    "src/development/BoundedProcessRunner.cpp"
    "CreateProcessW"
    "shared shell-free bounded developer process execution")

rose_require_source_text(
    "src/development/CMakeTestService.cpp"
    "CTestTestfile.cmake"
    "registered-test-only configured build boundary")

rose_require_source_text(
    "src/development/CMakeTestService.cpp"
    "--no-tests=error"
    "zero matched tests fail closed")

rose_require_source_text(
    "src/tools/RunCMakeTestsTool.cpp"
    ".risk = ToolRisk::ExternalEffect"
    "CTest execution external-effect classification")

rose_require_source_text(
    "src/tools/RunCMakeTestsTool.cpp"
    "ToolResponseMode::RequiresModelSynthesis"
    "test diagnostics returned to the coding agent")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit CTest request."
    "CTest source and execution-intent grounding")

rose_require_source_text(
    "src/main.cpp"
    ".maximumToolExecutions = 7"
    "bounded build-test-repair coding workflow ceiling")

rose_require_source_text(
    "CMakeLists.txt"
    "ROSE_CTEST_TARGETS"
    "Rose test targets registered with CTest")

message(STATUS "Rose Batch 40 controlled CTest regression contract: PASS")


# Batch 40 hotfix: CTest execution intent is classified once and shared by both
# model-selected validation and deterministic direct-request recovery. This
# specifically protects "Run all tests ..." and "Run test <name> ..." from
# falling back to conversational promises with no pending action.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "lowerUser.starts_with(\"run all test\")"
    "run-all-tests execution intent")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "CapabilityRoutingGuard::explicitCMakeTestExecutionIntent"
    "shared CTest execution-intent validation")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "run-all-tests requests must be recognized as explicit CTest execution intent"
    "CTest recovery regression test")

message(STATUS "Rose Batch 40 CTest routing hotfix regression contract: PASS")


# Batch 41 diagnostic source windows. read_text_file remains exact-file and
# confirmation-gated, but can now inspect bounded line windows around compiler or
# CTest diagnostics even when the relevant source is beyond the ordinary prefix.
rose_require_source_text(
    "src/tools/ReadTextFileRegisteredTool.cpp"
    "name = \"start_line\""
    "diagnostic text-read start-line parameter")

rose_require_source_text(
    "src/tools/ReadTextFileRegisteredTool.cpp"
    "line_count requires start_line"
    "bounded source-window argument relationship")

rose_require_source_text(
    "src/tools/ReadFileTool.h"
    "maximumTextRangeScanBytes{ 4u * 1024u * 1024u }"
    "bounded source-window scan ceiling")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "request.arguments.emplace("
    "deterministic source-window argument recovery")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "prefer a narrow one-based start_line plus line_count window"
    "coding-agent diagnostic source-window guidance")

rose_require_source_text(
    "src/agent/ToolObservation.cpp"
    "reconfigure_cmake_project, build_cmake_project, or run_cmake_tests"
    "post-mutation configure/build/test retry guidance")

message(STATUS "Rose Batch 41 diagnostic source-window regression contract: PASS")


# Batch 42 structured developer diagnostics. Build/test output remains bounded
# evidence, while Rose separately derives one project-contained source location
# into Rose-owned trusted metadata. Explicit repair/debug workflows may recover a
# narrow read_text_file window from that metadata; plain build/test requests do
# not silently expand into source inspection.
rose_require_source_text(
    "src/development/DiagnosticExtraction.cpp"
    "groundedProjectFile"
    "project-contained source diagnostic grounding")

rose_require_source_text(
    "src/development/DiagnosticExtraction.cpp"
    "metadata_kind=source_diagnostic"
    "Rose-owned structured diagnostic metadata")

rose_require_source_text(
    "src/tools/ToolTypes.h"
    "trustedMetadata"
    "trusted tool metadata channel separated from raw output")

rose_require_source_text(
    "src/agent/AgentLoop.h"
    "latestTrustedToolMetadata"
    "trusted diagnostic routing state kept outside ordinary transient context")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "recoverDiagnosticSourceReadRequest"
    "failed validation to source-window deterministic recovery")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "diagnosticRead"
    "bounded agent loop diagnostic follow-up integration")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "authority from arbitrary raw configure/compiler/test output text."
    "raw diagnostic output trust-boundary guidance")

message(STATUS "Rose Batch 42 structured diagnostic routing regression contract: PASS")


# Batch 43 patch-oriented source editing. Exact string replacement remains
# available, while narrow coding repairs can now replace a bounded contiguous
# source-line range only when the caller supplies the exact observed preimage.
# The service normalizes CRLF/LF for comparison, preserves the file's line-ending
# style, and keeps the existing transactional/concurrent-change protections.
rose_require_source_text(
    "src/files/TextFileMutationService.cpp"
    "replace_line_range preimage mismatch"
    "line-range source preimage verification")

rose_require_source_text(
    "src/files/TextFileMutationService.cpp"
    "maximumLinePatchLines{ 200u }"
    "bounded line-patch size")

rose_require_source_text(
    "src/tools/EditTextFileTool.cpp"
    "operation == \"replace_line_range\""
    "line-range mutation tool adapter")

rose_require_source_text(
    "src/tools/EditTextFileTool.cpp"
    "case 's':"
    "single-line control protocol edge-space escape")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "expected_text is valid for one empty source line."
    "empty optional preimage protocol support")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Model omitted or emptied a required tool argument"
    "required tool arguments remain non-empty")

message(STATUS "Rose Batch 43 patch-oriented source editing regression contract: PASS")


# Batch 43 recovery-gate hotfix. Deterministic direct recovery is more specific
# than the broad likelyToolBackedRequest heuristic and therefore must not be
# suppressed when that heuristic misses a newly supported phrase. This is what
# allows an explicit line mutation to recover its prerequisite source-window
# read after the control model incorrectly chooses RESPOND.
rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "Deterministic recovery is the stronger signal"
    "direct deterministic recovery is not heuristic-gated")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "|| directRequest.has_value()"
    "recovered direct request participates in tool-backed fallback")

message(STATUS "Rose Batch 43 recovery-gate hotfix regression contract: PASS")
# Batch 44 numbered source provenance. Source observations carry per-line hashes
# so a later narrow patch can be rebound to any observed contiguous subrange.
rose_require_source_text(
    "src/files/SourceWindowDigest.cpp"
    "sourceWindowSha256FromLineDigests"
    "per-line source provenance aggregation")

rose_require_source_text(
    "src/agent/SourceWindowBinding.cpp"
    "expected_digest"
    "Rose-owned observed-source preimage binding")

message(STATUS "Rose Batch 44 numbered source provenance regression contract: PASS")
# Batch 45 explicit repair planning. A grounded diagnostic may survive exactly one
# matching source-window read, then a provenance-bound patch receives a reviewable
# plan before the confirmation-gated mutation executes.
rose_require_source_text(
    "src/agent/SourceRepairPlan.cpp"
    "Source repair plan:"
    "reviewable source repair plan formatting")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "Prepared a provenance-bound source repair plan before mutation."
    "repair plan journal event before confirmation")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "latestDiagnosticMetadata"
    "grounded diagnostic lifetime across matching source read")

message(STATUS "Rose Batch 45 unified repair planning regression contract: PASS")
# Batch 46 deterministic post-repair revalidation. Rose retains the exact failed
# build/test request that produced the grounded diagnostic and may replay that
# request only after the provenance-bound repair succeeds. The replay remains
# confirmation-gated and never reconstructs validation arguments from model text.
rose_require_source_text(
    "src/agent/RepairValidationReplay.cpp"
    "failedValidationRequest = request"
    "exact failed validation request retained for repair verification")

rose_require_source_text(
    "src/agent/RepairValidationReplay.cpp"
    "plan.diagnostic->producerTool"
    "repair diagnostic producer must match retained validation")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "pendingRepairValidationRequest"
    "post-repair validation recovered from Rose-owned state")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "Deterministic post-repair validation replay proposed the exact failed configure/build/test request."
    "repair validation replay journal evidence")

message(STATUS "Rose Batch 46 deterministic repair revalidation regression contract: PASS")
# Batch 47 repair outcome tracking. Applying a provenance-bound patch is not the
# same as proving the repair. Rose correlates the exact post-repair validation
# with the patch and records applied/succeeded/failed outcome events separately.
rose_require_source_text(
    "src/agent/RepairOutcomeTracker.cpp"
    "patch_applied_pending_validation"
    "source repair application remains explicitly unproven before validation")

rose_require_source_text(
    "src/agent/RepairOutcomeTracker.cpp"
    "validation_succeeded"
    "exact correlated validation can prove a tracked repair")

rose_require_source_text(
    "src/agent/RepairOutcomeTracker.cpp"
    "validation_failed"
    "failed correlated validation preserves unproven repair outcome")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "AgentEventType::RepairApplied"
    "repair application black-box event")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "AgentEventType::RepairValidated"
    "successful repair validation black-box event")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "AgentEventType::RepairValidationFailed"
    "failed repair validation black-box event")

message(STATUS "Rose Batch 47 repair outcome tracking regression contract: PASS")
# Batch 48 controlled CMake reconfiguration. Rose may refresh generation only
# for an already-configured, source-matched <source>/build tree. The tool exposes
# no generator/cache/toolchain/preset argument surface and remains confirmation-
# gated because CMake configure scripts/dependency discovery may execute effects.
rose_require_source_text(
    "src/development/CMakeConfigureService.cpp"
    "validateConfiguredCMakeProject(request.sourceDirectory)"
    "reconfigure requires an existing source-matched CMake cache")

rose_require_source_text(
    "src/development/CMakeConfigureService.cpp"
    [=[arguments.push_back(L"-S")]=]
    "CMake reconfiguration invokes the exact source/build form directly")

rose_require_source_text(
    "src/tools/ReconfigureCMakeProjectTool.cpp"
    ".risk = ToolRisk::ExternalEffect"
    "CMake reconfiguration external-effect classification")

rose_require_source_text(
    "src/tools/ReconfigureCMakeProjectTool.cpp"
    "reconfigure_cmake_project does not accept argument"
    "CMake reconfiguration rejects arbitrary extra argument authority")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected ungrounded or non-explicit CMake reconfigure request."
    "CMake reconfiguration path and execution-intent grounding")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    ".toolId = \"reconfigure_cmake_project\""
    "deterministic explicit CMake reconfiguration recovery")

rose_require_source_text(
    "src/agent/RepairValidationReplay.cpp"
    "request.toolId == \"reconfigure_cmake_project\""
    "CMake reconfiguration participates in provenance-bound repair validation")

rose_require_source_text(
    "src/main.cpp"
    "ReconfigureCMakeProjectTool"
    "CMake reconfiguration tool registration")

message(STATUS "Rose Batch 48 controlled CMake reconfiguration regression contract: PASS")
# Batch 48 runtime command guard hotfix. /exit must perform the same real local
# shutdown as /quit, and unknown slash commands must never reach the model and
# masquerade as completed application-side effects.
rose_require_source_text(
    "src/main.cpp"
    [=[commandText == "/exit"]=]
    "real /exit alias for local Rose shutdown")

rose_require_source_text(
    "src/main.cpp"
    "Unknown Rose command. Use /tools"
    "unknown slash commands fail closed before model inference")

message(STATUS "Rose Batch 48 runtime command-guard hotfix regression contract: PASS")


# Batch 49 bounded multi-file coding workspace. Source-window provenance is no
# longer limited to only the most recent read. Rose may retain a small bounded set
# of validated windows across several files in one Agent run, while successful
# edits invalidate only the mutated path before any later line-range patch.
rose_require_source_text(
    "src/agent/CodingTaskWorkspace.cpp"
    "maximumRetainedSourceWindows"
    "bounded multi-file source-window retention")

rose_require_source_text(
    "src/agent/CodingTaskWorkspace.cpp"
    "invalidateSourceWindowsForPath"
    "successful edits invalidate stale source provenance for that exact path")

rose_require_source_text(
    "src/agent/CodingTaskWorkspace.cpp"
    "metadata_kind=coding_task_workspace"
    "compact trusted multi-file coding scope metadata")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "sourceWindowForRequest"
    "AgentLoop selects retained provenance for the file currently being edited")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "observeCodingMutation"
    "AgentLoop updates multi-file provenance after completed source mutations")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "metadata_kind=coding_task_workspace"
    "control model understands retained multi-file source evidence without treating it as source contents")

rose_require_source_text(
    "CMakeLists.txt"
    "RoseCodingTaskWorkspaceTest"
    "bounded multi-file coding workspace unit test registration")

message(STATUS "Rose Batch 49 bounded multi-file coding workspace regression contract: PASS")


# Batch 50 explicit multi-file coding plans. Once Rose has observed multiple
# source files, the first source mutation is blocked until the control model
# produces one bounded, grounded advisory plan. The plan never grants execution
# authority, but it is surfaced alongside the exact confirmation request.
rose_require_source_text(
    "src/agent/CodingTaskPlan.cpp"
    "maximumCodingTaskPlanSteps"
    "bounded multi-file coding plan size")

rose_require_source_text(
    "src/agent/CodingTaskPlan.cpp"
    "authority=false"
    "coding plan is explicitly non-authoritative")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "ACTION=PLAN"
    "control protocol supports one explicit coding-plan action")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Rejected coding plan containing an unregistered tool or ungrounded path."
    "coding plan paths remain grounded and registered")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "AgentEventType::CodingPlanRequired"
    "multi-file writes are blocked until a review plan exists")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "AgentEventType::CodingPlanCreated"
    "accepted coding plans are visible in the black-box journal")

rose_require_source_text(
    "src/agent/FileAgentJournalStore.cpp"
    "AgentEventType::CodingPlanCreated"
    "persistent journal accepts appended Batch 50 coding-plan events")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "formatCodingTaskPlanForConfirmation"
    "coding plan is shown before an exact consequential action is confirmed")

rose_require_source_text(
    "CMakeLists.txt"
    "RoseCodingTaskPlanTest"
    "explicit multi-file coding plan unit test registration")

message(STATUS "Rose Batch 50 explicit multi-file coding plan regression contract: PASS")

# Batch 50 multi-turn source-creation hotfix. An unresolved tool-backed request
# may carry only bounded USER-authored text into a nearby clarification turn.
# Assistant prose never becomes authority. Source creation may synthesize a small
# file body and one basename directly inside an explicitly user-grounded directory.
rose_require_source_text(
    "src/agent/UserTaskContinuation.cpp"
    "maximumUserTaskContinuationTurns"
    "bounded unresolved user-task continuation lifetime")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "preserveUserTaskContinuation"
    "AgentLoop exposes unresolved tool intent for nearby user clarification")

rose_require_source_text(
    "src/main.cpp"
    "shouldReuseUserTaskContinuation"
    "main reuses continuation only through bounded user-only state")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "rose_prior_user_task_context"
    "control routing distinguishes prior user authority from assistant/tool text")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "parentDirectoryGroundedByUser"
    "new text-file basename remains inside an explicitly user-grounded directory")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "request.maxGeneratedTokens = requiredPath.empty() ? 1536 : 3072;"
    "focused source-file drafting receives a bounded generation budget")

rose_require_source_text(
    "CMakeLists.txt"
    "RoseUserTaskContinuationTest"
    "multi-turn user-task continuation unit test registration")

message(STATUS "Rose Batch 50 multi-turn source-creation hotfix regression contract: PASS")

# Batch 50 routing/confirmation hotfix. Confirm must resume a pending run instead
# of falling through the unknown-slash guard; explicit existing directories must
# route to directory analysis; and source creation gets one focused synthesis pass
# when the broad control router incorrectly answers conversationally.
rose_require_source_text(
    "src/main.cpp"
    "!confirmedAgentRun.has_value()"
    "confirmed /confirm bypasses the unknown slash-command guard")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "isExistingNonSymlinkDirectory"
    "existing directory paths are classified before exact-file recovery")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "An existing directory must never fall through to an exact-file reader."
    "directory paths fail closed instead of reaching read_text_file")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Focused create_text_file draft output:"
    "explicit source creation uses a focused drafting pass")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "The registered capability contract is authoritative: do NOT tell"
    "final conversational fallback cannot deny a registered capability")

message(STATUS "Rose Batch 50 routing/confirmation hotfix regression contract: PASS")


# Batch 50 routing/context hotfix 3. Source creation drafts before the all-tools
# router so a large generation reservation cannot overflow the 8K local context.
# Unquoted Windows paths followed by prose preserve the deepest existing path,
# while large directory evidence skips a redundant routing pass and sheds the
# capability catalog before final user-facing synthesis.
rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Source creation gets the small focused prompt FIRST."
    "source creation drafts before broad tool routing")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "CONTENT_BEGIN"
    "focused source drafting supports multiline file contents")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "Remove one character, not one word."
    "unquoted existing paths followed by prose preserve the deepest path")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "toolCompletesWithFinalSynthesis"
    "large terminal directory analysis bypasses redundant control rerouting")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "finalResponseTransientContext"
    "final synthesis drops the redundant full capability contract after real tool execution")

rose_require_source_text(
    "src/tools/AnalyzeDirectoryDocumentsTool.h"
    "maximumObservationBytes{ 10u * 1024u }"
    "directory evidence remains inside a conservative final-context budget")

message(STATUS "Rose Batch 50 routing/context hotfix 3 regression contract: PASS")


# Batch 50 routing/context hotfix 4. Existing-prefix recovery for unquoted Windows
# paths must stop only at a real prompt boundary. Otherwise an intentionally
# nonexistent exact-file path can collapse to an existing parent/root directory
# and be misrouted as directory analysis before source-window arguments survive.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "promptBoundaryAfterPathPrefix"
    "existing Windows path-prefix recovery requires a prompt boundary")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "an exact-file request into a directory request on Windows."
    "nonexistent exact-file paths cannot collapse to an existing parent/root")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "preserve the exact nonexistent file path and explicit source-window line arguments"
    "deterministic source-window recovery retains exact path and line metadata")

message(STATUS "Rose Batch 50 routing/context hotfix 4 regression contract: PASS")


# Batch 51. Software-project directory diagnosis must remain a multi-step coding
# workflow instead of being collapsed into the terminal document-batch analyzer.
# Directory discovery also publishes Rose-grounded absolute child paths so later
# reads/scans can stay within the confirmed project tree without model invention.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "looksLikeDevelopmentProjectDirectory"
    "software project roots are detected separately from document corpora")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "A software project is not a document corpus."
    "project diagnosis begins with bounded root discovery")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "toolUsesPathKindCanonicalization"
    "path-type canonicalization does not overwrite legitimate later project discovery steps")

rose_require_source_text(
    "src/tools/ListDirectoryTool.cpp"
    "| absolute_path="
    "immediate directory discovery publishes grounded child paths")

rose_require_source_text(
    "src/tools/ScanDirectoryTreeTool.cpp"
    "| absolute_path="
    "recursive inventory publishes grounded child paths")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "completed bounded project inventory must return control to the multi-step agent"
    "project discovery cannot fall through into terminal document analysis")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "a Rose-grounded child discovered under a project root must remain targetable"
    "project child reads survive root-directory canonicalization")

message(STATUS "Rose Batch 51 project-diagnosis discovery regression contract: PASS")


# Batch 52. Hidden tool routing must fit the configured local context as Rose's
# registry grows. ToolSelectionAgent already owns ToolRegistry, so the full prose
# capability contract is projected out of the routing copy while real observations
# remain available for grounding. The system prompt carries compact cross-tool
# invariants plus descriptor/schema rows instead of duplicating per-tool manuals.
rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "The original agentContext remains intact for path grounding"
    "control-context projection removes duplicate capability prose without weakening grounding")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Registered tools (descriptor text is authoritative):"
    "hidden router uses a compact descriptor-driven capability catalog")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "CAPABILITY_CONTRACT_SENTINEL_SHOULD_NOT_REACH_ROUTER"
    "routing projection is regression-tested against capability-contract duplication")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "one bounded recursive inventory is a safe deterministic continuation"
    "project diagnosis has a read-only scan fallback when the model still responds early")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "a project diagnosis that still lacks evidence should have one bounded recursive discovery fallback"
    "project diagnosis fallback remains bounded and deterministic")

message(STATUS "Rose Batch 52 bounded control-routing regression contract: PASS")


# Batch 53. Read-only project diagnosis must not stall when a small local model
# wraps one grounded read in ACTION=PLAN. Recursive discovery must also fit beside
# the hidden router/final synthesis prompt instead of consuming the entire 8K model
# context with generated-tree paths.
rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Recovered a single read_text_file action that the control model wrapped in ACTION=PLAN."
    "single read-only plan misuse is recovered as the direct grounded read action")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "Never use PLAN merely to request one read-only action"
    "hidden router explicitly reserves PLAN for mutation-bearing coding plans")

rose_require_source_text(
    "src/tools/ScanDirectoryTreeTool.h"
    "std::size_t maximumOutputBytes"
    "recursive discovery exposes a bounded output budget for local model context")

rose_require_source_text(
    "src/tools/ScanDirectoryTreeTool.cpp"
    "conventionalGeneratedTree"
    "bounded recursive discovery prioritizes useful paths before generated trees")

rose_require_source_text(
    "src/agent/ToolSelectionAgent_test.cpp"
    "a one-step read-only PLAN should recover as the grounded read_text_file action instead of stalling diagnosis"
    "read-only plan recovery is regression-tested")

rose_require_source_text(
    "src/tools/FilesystemTools_test.cpp"
    "default scan observation must stay near the 8 KiB model-context budget"
    "default recursive discovery is regression-tested against context blowout")

message(STATUS "Rose Batch 53 bounded discovery continuation regression contract: PASS")


# Batch 54. Preserve exact read bounds when a small local model packs optional
# read_text_file arguments into a one-step PLAN note. Duplicate read-only actions
# receive one bounded re-route instead of immediately ending a broad diagnosis,
# and recursive discovery subsumes a later deterministic root listing.
rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "start_line=1;line_count=20"
    "semicolon-packed read-only PLAN shorthand is recognized")

rose_require_source_text(
    "src/agent/ToolSelectionAgent.cpp"
    "while preserving documented read bounds"
    "one-step read-only PLAN recovery keeps source-window arguments")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "repeated_read_only_tool_request"
    "duplicate read-only actions receive one bounded control re-check")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "completedRecursiveScanCoversListing"
    "recursive discovery prevents a redundant deterministic root listing")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "reason=overlapping_text_read"
    "overlapping source-window reads receive one bounded non-overlap re-route")

rose_require_source_text(
    "src/tools/ScanDirectoryTreeTool.h"
    "maximumOutputBytes{ 6u * 1024u }"
    "recursive discovery leaves additional context headroom for follow-up reads")

message(STATUS "Rose Batch 54R bounded read-only continuation regression contract: PASS")


# Batch 55. A broad software-project diagnosis should progress from bounded
# discovery to confirmation-gated validation instead of stopping with generic
# possibilities. A successful configured build may be followed by CTest, while a
# failed build remains available for the existing diagnostic-source recovery path.
rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "projectDiagnosisIntent"
    "broad project diagnosis has an explicit deterministic intent classifier")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "diagnosisBuildIntent"
    "project diagnosis can propose a configured build after read-only discovery")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "latestToolObservationSuccess"
    "diagnostic validation continuation uses Rose-owned tool-success evidence")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "diagnosisTestIntent"
    "successful diagnostic builds can continue to registered CTest validation")

rose_require_source_text(
    "src/agent/AgentLoop.cpp"
    "directRequestIsRedundantListing"
    "failed validation diagnostics are not masked by redundant root-list recovery")

message(STATUS "Rose Batch 55 project validation continuation regression contract: PASS")


# Batch 56. Large exact PDFs may be accepted up to a 512 MiB safety ceiling,
# while explicit first-N/range requests stay page-bounded before extraction.
# Multi-PDF review preserves a separate page window per named file. An
# unbounded page request advances through bounded extraction windows.
rose_require_source_text(
    "src/tools/ReadFileTool.h"
    "maximumBinarySafetyBytes{ 512u * 1024u * 1024u }"
    "binary-document safety ceiling accepts the known 281 MiB PDF without becoming unbounded")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    "maximumPdfBinaryBytes"
    "PDF reading opts into the reviewed 512 MiB allowance without widening every binary reader")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    ".name = \"page_count\""
    "single-PDF reading exposes an explicit bounded page window")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    "pagesPerWindow"
    "large PDF review advances through bounded extraction windows")

rose_require_source_text(
    "src/tools/ReadPdfRegisteredTool.cpp"
    "analysis_windows="
    "large PDF review reports the number of processed page windows")

rose_require_source_text(
    "src/tools/PdfTextExtractor.cpp"
    "firstPageIndex ="
    "PDF extraction starts at the requested one-based page window")

rose_require_source_text(
    "src/agent/CapabilityRoutingGuard.cpp"
    "Read the first 25 pages of this file"
    "natural-language first-N-page intent is associated with the named PDF")

rose_require_source_text(
    "src/tools/SearchAndApproval_test.cpp"
    "per-file PDF page-window routing did not preserve first-25-page intent"
    "multi-PDF page-window routing is regression-tested")

rose_require_source_text(
    "src/tools/FilesystemTools_test.cpp"
    "binary per-call override exceeded the configured safety ceiling"
    "large-container binary allowance remains capped and tool-specific")

message(STATUS "Rose Batch 56 large-PDF page-window regression contract: PASS")
