# BTD5 Mod Loader developer commands
#
# Override the default configuration when needed:
#   make build CONFIG=Release

.DEFAULT_GOAL := help
.NOTPARALLEL:

CONFIG ?= Debug
POWERSHELL := powershell -NoProfile -ExecutionPolicy Bypass
DOTNET := .\.tools\dotnet-10.0.400\dotnet.exe
MANAGER_PROJECT := .\src\manager\BTD5ModLoader.Manager\BTD5ModLoader.Manager.csproj
MANAGER_EXE := .\src\manager\BTD5ModLoader.Manager\bin\$(CONFIG)\net10.0-windows\BTD5ModLoader.Manager.exe
STAGED_MANAGER_EXE := .\out\stage\$(CONFIG)\BTD5ModLoader.Manager.exe

.PHONY: help dev build build-ui ui run-ui stage run build-and-run test test-ui check analyze format release prepare-release clean

help:
	@echo BTD5 Mod Loader development commands
	@echo   make dev               Build and run only the manager UI (fastest loop)
	@echo   make build-ui          Build only the manager UI
	@echo   make run-ui            Build and run only the manager UI
	@echo   make build             Build the complete C++ and C# project
	@echo   make stage             Build and assemble the complete runnable bundle
	@echo   make run               Build, stage, and run the complete bundle
	@echo   make test-ui           Run the fast manager integration tests
	@echo   make test              Build and run native and managed tests
	@echo   make check             Run tests and formatting checks
	@echo   make analyze           Run native and managed static analysis
	@echo   make format            Verify source formatting
	@echo   make release           Build and stage an optimized release bundle
	@echo   make prepare-release VERSION=x.y.z  Validate and package a release candidate
	@echo   make clean             Remove generated build output
	@echo Configuration defaults to Debug. Example: make run CONFIG=Release

# Fast manager UI development loop. Native components are not rebuilt or staged.
dev: run-ui

build-ui:
	$(DOTNET) build $(MANAGER_PROJECT) --configuration $(CONFIG) --nologo

# Short alias for scripts and muscle memory.
ui: build-ui

run-ui: build-ui
	$(POWERSHELL) -Command "& '$(MANAGER_EXE)'"

# Complete compile without staging or launching.
build:
	$(POWERSHELL) -File .\scripts\build.ps1 -Configuration $(CONFIG)

# Assemble the manager, native loader, symbols, and sample mods in out/stage.
stage: build
	$(POWERSHELL) -File .\scripts\stage.ps1 -Configuration $(CONFIG)

# Complete end-to-end local run.
run: stage
	$(POWERSHELL) -Command "& '$(STAGED_MANAGER_EXE)'"

# Backward-compatible name from the original Makefile.
build-and-run: run

# Fast tests for manager, package, profile, and installation workflow changes.
test-ui:
	$(DOTNET) run --project .\tests\managed\BTD5ModLoader.Manager.Core.Tests\BTD5ModLoader.Manager.Core.Tests.csproj --configuration $(CONFIG)

# test.ps1 intentionally performs a cleanly ordered full build before testing.
test:
	$(POWERSHELL) -File .\scripts\test.ps1 -Configuration $(CONFIG)

format:
	$(POWERSHELL) -File .\scripts\check-format.ps1

check: test format

analyze:
	$(POWERSHELL) -File .\scripts\analyze.ps1

release:
	$(POWERSHELL) -File .\scripts\build.ps1 -Configuration Release
	$(POWERSHELL) -File .\scripts\stage.ps1 -Configuration Release

prepare-release:
	$(POWERSHELL) -Command "if ([string]::IsNullOrWhiteSpace('$(VERSION)')) { throw 'Set VERSION, for example: make prepare-release VERSION=0.1.0' }"
	$(POWERSHELL) -File .\scripts\prepare-release.ps1 -Version $(VERSION)

clean:
	$(POWERSHELL) -Command "$$paths = @('.\out', '.\src\manager\BTD5ModLoader.Manager\bin', '.\src\manager\BTD5ModLoader.Manager\obj', '.\src\manager\BTD5ModLoader.Manager.Core\bin', '.\src\manager\BTD5ModLoader.Manager.Core\obj', '.\tests\managed\BTD5ModLoader.Manager.Core.Tests\bin', '.\tests\managed\BTD5ModLoader.Manager.Core.Tests\obj', '.\tests\managed\BTD5ModLoader.LiveSmoke\bin', '.\tests\managed\BTD5ModLoader.LiveSmoke\obj'); foreach ($$path in $$paths) { if (Test-Path -LiteralPath $$path) { Remove-Item -LiteralPath $$path -Recurse -Force } }"
