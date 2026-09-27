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
    "60 Hz temporal interpolation for low-FPS avatar atlases")

rose_require_source_text(
    "src/avatar/SdlAvatar.cpp"
    "Do NOT dim both neighboring frames at the same time"
    "opacity-preserving avatar frame interpolation")

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
