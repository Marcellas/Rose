\# Rose



Rose is a local-first, agentic desktop AI assistant written in C++20.



The project is focused on creating an AI assistant whose identity, memory,

tools, permissions, interface, and persistent state are independent of the

language model providing inference.



\## Goals



Rose is intended to support:



\- Conversation and assistance

\- Replaceable local and cloud model providers

\- Persistent searchable memory

\- File and document assistance

\- Agentic tool use

\- Explicit tool permissions

\- Voice input and output

\- Animated desktop avatar

\- Plugin/extensible tool support

\- Local-first operation



Internet-dependent features should degrade gracefully when offline.



\## Architecture



Major conceptual modules include:



\- RoseCore

\- ModelProvider

\- Agent

\- Planner

\- Memory

\- ToolRegistry

\- PermissionSystem

\- Avatar

\- Voice

\- Persistence

\- Plugins



The language model is a dependency of Rose rather than Rose herself.



\## Current Status



Rose is in very early development.



The initial milestone is a small vertical slice:



User → Rose → Local Model → Persistent Memory → Safe File Tool



\## Requirements



Initial development environment:



\- C++20

\- CMake

\- Microsoft Visual Studio / MSVC

\- Windows x64



\## Building



```powershell

cmake -S . -B build

cmake --build build --config Debug

